#include "stdafx.h"
#include <logging.h>
#include <RimeWithWeasel.h>
#include <StringAlgorithm.hpp>
#include <WeaselConstants.h>
#include <WeaselUtility.h>

#include <atlbase.h>
#include <msctf.h>
#include <WeaselTsfIdentity.h>

#include <filesystem>
#include <cstdlib>
#include <map>
#include <array>
#include <vector>
#include <regex>
#include <rime_api.h>

#define TRANSPARENT_COLOR 0x00000000
#define ARGB2ABGR(value)                                 \
  ((value & 0xff000000) | ((value & 0x000000ff) << 16) | \
   (value & 0x0000ff00) | ((value & 0x00ff0000) >> 16))
#define RGBA2ABGR(value)                                   \
  (((value & 0xff) << 24) | ((value & 0xff000000) >> 24) | \
   ((value & 0x00ff0000) >> 8) | ((value & 0x0000ff00) << 8))
typedef enum { COLOR_ABGR = 0, COLOR_ARGB, COLOR_RGBA } ColorFormat;

using namespace weasel;

#include <WeaselModeDebug.h>

static RimeApi* rime_api;
WeaselSessionId _GenerateNewWeaselSessionId(const SessionStatusMap& sm, DWORD pid) {
  if (sm.empty())
    return (WeaselSessionId)(pid + 1);
  return (WeaselSessionId)(sm.rbegin()->first + 1);
}

int expand_ibus_modifier(int m) {
  return (m & 0xff) | ((m & 0xff00) << 16);
}

// The keypad-input Lua processor keeps the text from the first physical
// keypad digit onward out of context.input, so Rime continues matching only
// the active pinyin segment.  Its confirmed Chinese segments live in a
// virtual prefix, while the unconverted keypad tail and its cursor follow the
// real preedit.  Together they form one visible, still-uncommitted bar.
static std::string get_keypad_property(RimeSessionId session_id, const char* name) {
  if (!rime_api || !rime_api->get_property) return {};
  for (size_t size = 256; size <= 1024 * 1024; size *= 2) {
    std::vector<char> value(size, 0);
    if (!rime_api->get_property(session_id, name, value.data(), static_cast<int>(size))) return {};
    if (value.back() == 0) return std::string(value.data());
  }
  return {}; // never publish a truncated UTF-8 fragment
}
static std::string get_keypad_preedit_prefix(RimeSessionId id) {
  return get_keypad_property(id, "weasel_keypad_prefix");
}
static std::string get_keypad_preedit_suffix(RimeSessionId id) {
  return get_keypad_property(id, "weasel_keypad_suffix");
}

static int get_keypad_preedit_cursor(RimeSessionId session_id,
                                        const std::string& suffix) {
  if (suffix.empty()) {
    return 0;
  }
  char value[32] = {};
  const Bool found = rime_api && rime_api->get_property &&
                     rime_api->get_property(session_id, "weasel_keypad_cursor",
                                            value, sizeof(value));
  value[sizeof(value) - 1] = '\0';
  if (!found) {
    return suffix.size();
  }
  long cursor = std::strtol(value, nullptr, 10);
  if (cursor < 0) {
    // Lua publishes -1 when navigation crosses back into the real pinyin.
    // In that case use Rime's formatted preedit caret, not the suffix origin.
    return -1;
  }
  const long end = static_cast<long>(suffix.size());
  if (cursor > end) {
    cursor = end;
  }
  return static_cast<int>(cursor);
}

RimeWithWeaselHandler::RimeWithWeaselHandler(UI* ui)
    : m_ui(ui),
      m_active_session(0),
      m_disabled(true),
      m_current_dark_mode(false),
      m_global_ascii_mode(false),
      m_show_notifications_time(1200),
      _UpdateUICallback(NULL) {
  m_ui->InServer() = true;
  rime_api = rime_get_api();
  assert(rime_api);
  m_pid = GetCurrentProcessId();
  uint16_t msbit = 0;
  for (auto i = 31; i >= 0; i--) {
    if (m_pid & (1 << i)) {
      msbit = i;
      break;
    }
  }
  m_pid = (m_pid << (31 - msbit));
  _Setup();
}

RimeWithWeaselHandler::~RimeWithWeaselHandler() {
  m_show_notifications.clear();
  m_session_status_map.clear();
  m_app_options.clear();
}

bool add_session = false;
void _UpdateUIStyle(RimeConfig* config, UI* ui, bool initialize);
bool _UpdateUIStyleColor(RimeConfig* config,
                         UIStyle& style,
                         const std::string& color = std::string());
void _LoadAppOptions(RimeConfig* config, AppOptionsByAppName& app_options);
void _LoadSsfSkinSettings(RimeConfig* config, UIStyle& style);



void _RefreshTrayIcon(const RimeSessionId session_id,
                      const std::function<void()> _UpdateUICallback) {
  // Dangerous, don't touch
  static char app_name[256] = {0};
  auto ret = rime_api->get_property(session_id, "client_app", app_name,
                                    sizeof(app_name) - 1);
  if (!ret || u8tow(app_name) == std::wstring(L"explorer.exe"))
    boost::thread th([=]() {
      ::Sleep(100);
      if (_UpdateUICallback)
        _UpdateUICallback();
    });
  else if (_UpdateUICallback)
    _UpdateUICallback();
}

void RimeWithWeaselHandler::_Setup() {
  RIME_STRUCT(RimeTraits, weasel_traits);
  std::string shared_dir = wtou8(WeaselSharedDataPath().wstring());
  std::string user_dir = wtou8(WeaselUserDataPath().wstring());
  weasel_traits.shared_data_dir = shared_dir.c_str();
  weasel_traits.user_data_dir = user_dir.c_str();
  weasel_traits.prebuilt_data_dir = weasel_traits.shared_data_dir;
  std::string distribution_name = wtou8(get_weasel_ime_name());
  weasel_traits.distribution_name = distribution_name.c_str();
  weasel_traits.distribution_code_name = WEASEL_CODE_NAME;
  weasel_traits.distribution_version = WEASEL_VERSION;
  weasel_traits.app_name = "rime.weasel";
  std::string log_dir = WeaselLogPath().u8string();
  weasel_traits.log_dir = log_dir.c_str();
  rime_api->setup(&weasel_traits);
  rime_api->set_notification_handler(&RimeWithWeaselHandler::OnNotify, this);
}

void RimeWithWeaselHandler::Initialize() {
  m_disabled = _IsDeployerRunning();
  if (m_disabled) {
    return;
  }

  LOG(INFO) << "Initializing la rime.";
  rime_api->initialize(NULL);
  if (rime_api->start_maintenance(/*full_check = */ False)) {
    m_disabled = true;
    rime_api->join_maintenance_thread();
  }

  RimeConfig config = {NULL};
  if (rime_api->config_open("weasel", &config)) {
    if (m_ui) {
      _UpdateUIStyle(&config, m_ui, true);
      _UpdateShowNotifications(&config, true);
      m_current_dark_mode = IsUserDarkMode();
      if (m_current_dark_mode) {
        const int BUF_SIZE = 255;
        char buffer[BUF_SIZE + 1] = {0};
        if (rime_api->config_get_string(&config, "style/color_scheme_dark",
                                        buffer, BUF_SIZE)) {
          std::string color_name(buffer);
          _UpdateUIStyleColor(&config, m_ui->style(), color_name);
        }
      }
      m_base_style = m_ui->style();
  DebugStream() << L"[SSF] m_base_style captured (line 135 site): skin='"
                << m_base_style.ssf_skin
                << L"' enabled=" << (m_base_style.ssf_enabled ? 1 : 0) << L"\n";
    }
    Bool global_ascii = false;
    if (rime_api->config_get_bool(&config, "global_ascii", &global_ascii))
      m_global_ascii_mode = !!global_ascii;
    if (!rime_api->config_get_int(&config, "show_notifications_time",
                                  &m_show_notifications_time))
      m_show_notifications_time = 1200;
    _LoadAppOptions(&config, m_app_options);
    rime_api->config_close(&config);
  }
  m_last_schema_id.clear();
}

void RimeWithWeaselHandler::Finalize() {
  m_active_session = 0;
  m_disabled = true;
  m_session_status_map.clear();
  LOG(INFO) << "Finalizing la rime.";
  rime_api->finalize();
}

DWORD RimeWithWeaselHandler::FindSession(WeaselSessionId ipc_id) {
  if (m_disabled)
    return 0;
  Bool found = rime_api->find_session(to_session_id(ipc_id));
  DLOG(INFO) << "Find session: session_id = " << to_session_id(ipc_id)
             << ", found = " << found;
  return found ? (ipc_id) : 0;
}

DWORD RimeWithWeaselHandler::AddSession(LPWSTR buffer, EatLine eat) {
  if (m_disabled) {
    DLOG(INFO) << "Trying to resume service.";
    EndMaintenance();
    if (m_disabled)
      return 0;
  }
  RimeSessionId session_id = (RimeSessionId)rime_api->create_session();
  if (m_global_ascii_mode) {
    rime_api->set_option(session_id, "ascii_mode", m_shared_ascii_mode);
  }

  WeaselSessionId ipc_id =
      _GenerateNewWeaselSessionId(m_session_status_map, m_pid);
  DLOG(INFO) << "Add session: created session_id = " << session_id
             << ", ipc_id = " << ipc_id;
  SessionStatus& session_status = new_session_status(ipc_id);
  session_status.style = m_base_style;
  session_status.session_id = session_id;
  // A brand new session means the user is starting to type with Weasel again,
  // which in practice is what "I switched back from another keyboard" looks
  // like here: switching layouts does not deliver OnActivated(FALSE/TRUE) to the
  // TSF (verified with logging), so the profile-activation reset in FocusIn()
  // never runs and a previously selected English state survived the switch.
  // Starting every new session in Chinese satisfies "switching to Weasel means
  // Chinese" without touching an in-session Shift or language-bar toggle, which
  // act on the existing session.
  if (m_global_ascii_mode) {
    m_shared_ascii_mode = false;
    if (rime_api->get_option(session_id, "ascii_mode"))
      rime_api->set_option(session_id, "ascii_mode", false);
  }
  _ReadClientInfo(ipc_id, buffer);

  RIME_STRUCT(RimeStatus, status);
  if (rime_api->get_status(session_id, &status)) {
    std::string schema_id = status.schema_id;
    m_last_schema_id = schema_id;
    _LoadSchemaSpecificSettings(ipc_id, schema_id);
    _LoadAppInlinePreeditSet(ipc_id, true);
    _UpdateInlinePreeditStatus(ipc_id);
    _RefreshTrayIcon(session_id, _UpdateUICallback);
    session_status.status = status;
    session_status.__synced = false;
    rime_api->free_status(&status);
  }
  m_ui->style() = session_status.style;
  DebugStream() << L"[SSF] WRITE-A (_Respond): ui->style skin='"
                << m_ui->style().ssf_skin << L"'\n";
  // show session's welcome message :-) if any
  if (eat) {
    _Respond(ipc_id, eat);
  }
  add_session = true;
  _UpdateUI(ipc_id);
  add_session = false;
  m_active_session = ipc_id;
  return ipc_id;
}

DWORD RimeWithWeaselHandler::RemoveSession(WeaselSessionId ipc_id) {
  if (m_ui)
    m_ui->Hide();
  if (m_disabled)
    return 0;
  DLOG(INFO) << "Remove session: session_id = " << to_session_id(ipc_id);
  // TODO: force committing? otherwise current composition would be lost
  rime_api->destroy_session(to_session_id(ipc_id));
  m_session_status_map.erase(ipc_id);
  m_active_session = 0;
  return 0;
}

void RimeWithWeaselHandler::UpdateColorTheme(BOOL darkMode) {
  RimeConfig config = {NULL};
  if (rime_api->config_open("weasel", &config)) {
    if (m_ui) {
      _UpdateUIStyle(&config, m_ui, true);
      m_current_dark_mode = darkMode;
      if (darkMode) {
        const int BUF_SIZE = 255;
        char buffer[BUF_SIZE + 1] = {0};
        if (rime_api->config_get_string(&config, "style/color_scheme_dark",
                                        buffer, BUF_SIZE)) {
          std::string color_name(buffer);
          _UpdateUIStyleColor(&config, m_ui->style(), color_name);
        }
      }
      m_base_style = m_ui->style();
  DebugStream() << L"[SSF] m_base_style captured (line 135 site): skin='"
                << m_base_style.ssf_skin
                << L"' enabled=" << (m_base_style.ssf_enabled ? 1 : 0) << L"\n";
    }
    rime_api->config_close(&config);
  }

  for (auto& pair : m_session_status_map) {
    RIME_STRUCT(RimeStatus, status);
    if (rime_api->get_status(to_session_id(pair.first), &status)) {
      _LoadSchemaSpecificSettings(pair.first, std::string(status.schema_id));
      _LoadAppInlinePreeditSet(pair.first, true);
      _UpdateInlinePreeditStatus(pair.first);
      pair.second.status = status;
      pair.second.__synced = false;
      rime_api->free_status(&status);
    }
  }
  m_ui->style() = get_session_status(m_active_session).style;
  DebugStream() << L"[SSF] WRITE-B (SelectSession): ui->style skin='"
                << m_ui->style().ssf_skin << L"'\n";
}

BOOL RimeWithWeaselHandler::ProcessKeyEvent(KeyEvent keyEvent,
                                            WeaselSessionId ipc_id,
                                            EatLine eat) {
  DLOG(INFO) << "Process key event: keycode = " << keyEvent.keycode
             << ", mask = " << keyEvent.mask << ", ipc_id = " << ipc_id;
  if (m_disabled)
    return FALSE;
  RimeSessionId session_id = to_session_id(ipc_id);
  const bool is_keypad_literal =
      !(keyEvent.mask & ibus::Modifier::RELEASE_MASK) &&
      ((keyEvent.keycode >= ibus::Keycode::KP_0 &&
        keyEvent.keycode <= ibus::Keycode::KP_9) ||
       keyEvent.keycode == ibus::Keycode::KP_Divide ||
       keyEvent.keycode == ibus::Keycode::KP_Multiply ||
       keyEvent.keycode == ibus::Keycode::KP_Subtract ||
       keyEvent.keycode == ibus::Keycode::KP_Add ||
       keyEvent.keycode == ibus::Keycode::KP_Decimal);
  if (is_keypad_literal) {
    // Lua handles KP_* as literal virtual input.  Rime can still report a
    // delayed ascii_mode option notifier in the same refresh; that notifier
    // must never create a CN/EN overlay for a number key.
    SessionStatus& session_status = get_session_status(ipc_id);
    session_status.suppress_mode_tip_once = true;
    session_status.mode_tip_requested = false;
  }
  // Rime emits option_update even when the value is unchanged. Reapplying
  // ascii_mode on key-up rebuilds the menu and undoes the preceding page turn.
  if (m_global_ascii_mode &&
      !!rime_api->get_option(session_id, "ascii_mode") != m_shared_ascii_mode)
    rime_api->set_option(session_id, "ascii_mode", m_shared_ascii_mode);
  const bool ascii_before_key = !!rime_api->get_option(session_id, "ascii_mode");
  Bool handled = rime_api->process_key(session_id, keyEvent.keycode,
                                       expand_ibus_modifier(keyEvent.mask));
  // vim_mode when keydown only
  if (!handled && !(keyEvent.mask & ibus::Modifier::RELEASE_MASK)) {
    bool isVimBackInCommandMode =
        (keyEvent.keycode == ibus::Keycode::Escape) ||
        ((keyEvent.mask & (1 << 2)) &&
         (keyEvent.keycode == ibus::Keycode::XK_c ||
          keyEvent.keycode == ibus::Keycode::XK_C ||
          keyEvent.keycode == ibus::Keycode::XK_bracketleft));
    if (isVimBackInCommandMode &&
        rime_api->get_option(session_id, "vim_mode") &&
        !rime_api->get_option(session_id, "ascii_mode")) {
      rime_api->set_option(session_id, "ascii_mode", True);
    }
  }
  bool ascii_after_key = !!rime_api->get_option(session_id, "ascii_mode");
  if (m_global_ascii_mode) {
    const bool shift = keyEvent.keycode == ibus::Shift_L ||
                       keyEvent.keycode == ibus::Shift_R;
    if (m_base_style.ssf_enabled && !shift && ascii_after_key != ascii_before_key) {
      ModeDbg(L"[MODEDBG] ProcessKeyEvent: REVERTING ascii_mode " +
              std::to_wstring(ascii_after_key ? 1 : 0) + L" -> " +
              std::to_wstring(ascii_before_key ? 1 : 0) + L" (keycode=" +
              std::to_wstring(keyEvent.keycode) + L", shared=" +
              std::to_wstring(m_shared_ascii_mode ? 1 : 0) + L")");
      rime_api->set_option(session_id, "ascii_mode", ascii_before_key);
      ascii_after_key = ascii_before_key;
    }
    m_shared_ascii_mode = ascii_after_key;
    if (ascii_before_key != ascii_after_key)
      for (auto& pair : m_session_status_map)
        rime_api->set_option(pair.second.session_id, "ascii_mode", m_shared_ascii_mode);
  }
  if (!is_keypad_literal && ascii_before_key != ascii_after_key) {
    // A keyboard toggle need not use SetOption(), and its option notification
    // may be followed by another Rime notification. Preserve the actual
    // transition independently of that single-slot notification mailbox.
    auto& session_status = get_session_status(ipc_id);
    session_status.mode_tip_last_ascii_mode = ascii_before_key;
    session_status.mode_tip_ascii_initialized = true;
    session_status.mode_tip_requested = true;
  }
  _Respond(ipc_id, eat);
  _UpdateUI(ipc_id);
  m_active_session = ipc_id;
  return (BOOL)handled;
}

void RimeWithWeaselHandler::CommitComposition(WeaselSessionId ipc_id) {
  DLOG(INFO) << "Commit composition: ipc_id = " << ipc_id;
  if (m_disabled)
    return;
  rime_api->commit_composition(to_session_id(ipc_id));
  _UpdateUI(ipc_id);
  m_active_session = ipc_id;
}

void RimeWithWeaselHandler::ClearComposition(WeaselSessionId ipc_id) {
  DLOG(INFO) << "Clear composition: ipc_id = " << ipc_id;
  if (m_disabled)
    return;
  rime_api->clear_composition(to_session_id(ipc_id));
  // A late focus-loss callback from the old app must not steal the new
  // field's active session or erase its travelling mode indicator.
  if (m_active_session != ipc_id) return;
  _UpdateUI(ipc_id);
  m_active_session = ipc_id;
}

void RimeWithWeaselHandler::SelectCandidateOnCurrentPage(
    size_t index,
    WeaselSessionId ipc_id) {
  DLOG(INFO) << "select candidate on current page, ipc_id = " << ipc_id
             << ", index = " << index;
  if (m_disabled)
    return;
  const RimeSessionId session_id = to_session_id(ipc_id);
  if (!get_keypad_preedit_prefix(session_id).empty() ||
      !get_keypad_preedit_suffix(session_id).empty()) {
    RIME_STRUCT(RimeContext, context);
    if (!rime_api->get_context(session_id, &context)) return;
    const bool valid = index < static_cast<size_t>(context.menu.num_candidates);
    rime_api->free_context(&context);
    if (!valid) return;
    // Mouse selection must follow the same deferred-commit processor as the
    // keyboard, otherwise clicking bypasses the virtual prefix and digits.
    rime_api->highlight_candidate_on_current_page(session_id, index);
    rime_api->process_key(session_id, ibus::Keycode::space, 0);
  } else {
    rime_api->select_candidate_on_current_page(session_id, index);
  }
}

bool RimeWithWeaselHandler::HighlightCandidateOnCurrentPage(
    size_t index,
    WeaselSessionId ipc_id,
    EatLine eat) {
  DLOG(INFO) << "highlight candidate on current page, ipc_id = " << ipc_id
             << ", index = " << index;
  bool res = rime_api->highlight_candidate_on_current_page(
      to_session_id(ipc_id), index);
  _Respond(ipc_id, eat);
  _UpdateUI(ipc_id);
  return res;
}

bool RimeWithWeaselHandler::ChangePage(bool backward,
                                       WeaselSessionId ipc_id,
                                       EatLine eat) {
  DLOG(INFO) << "change page, ipc_id = " << ipc_id
             << (backward ? "backward" : "foreward");
  bool res = rime_api->change_page(to_session_id(ipc_id), backward);
  _Respond(ipc_id, eat);
  _UpdateUI(ipc_id);
  return res;
}

void RimeWithWeaselHandler::FocusIn(DWORD client_caps, WeaselSessionId ipc_id) {
  DLOG(INFO) << "Focus in: ipc_id = " << ipc_id
             << ", client_caps = " << client_caps;
  if (m_disabled)
    return;
  if (client_caps & 0x80000000u) {
    ModeDbg(L"[MODEDBG] FocusIn: PROFILE activation, shared_before=" +
            std::to_wstring(m_shared_ascii_mode ? 1 : 0) + L" sessions=" +
            std::to_wstring(m_session_status_map.size()));
    // Profile activation: the user just selected Weasel, so always start in
    // Chinese.  Previously this only ran when m_profile_active had been cleared,
    // so a stale English state could survive the switch back and the first
    // thing the user typed came out as ASCII.  Clearing unconditionally makes
    // "switch to Weasel" mean "Chinese", which is what a Chinese IME should do.
    m_shared_ascii_mode = false;
    for (auto& pair : m_session_status_map) {
      if (rime_api->get_option(pair.second.session_id, "ascii_mode"))
        rime_api->set_option(pair.second.session_id, "ascii_mode", false);
      pair.second.mode_tip_requested = false;
    }
    m_profile_active = true;
  }
  if (m_ui) m_ui->Hide();
  m_active_session = ipc_id;
  const auto session = m_session_status_map.find(ipc_id);
  if (m_global_ascii_mode && session != m_session_status_map.end() &&
      !!rime_api->get_option(session->second.session_id, "ascii_mode") !=
          m_shared_ascii_mode)
    rime_api->set_option(session->second.session_id, "ascii_mode", m_shared_ascii_mode);
  if (session == m_session_status_map.end() ||
      !session->second.style.ssf_enabled || session->second.style.ssf_skin.empty()) {
    m_focus_tip_pending = 0;
    _UpdateUI(ipc_id);
    return;
  }
  // Focus changes dismiss the tip; only a language toggle shows a new one.
  m_focus_tip_pending = 0;
}

void RimeWithWeaselHandler::ToggleIdleMode() {
  if (!CanToggleIdleMode()) return;
  m_shared_ascii_mode = !m_shared_ascii_mode;
  for (auto& pair : m_session_status_map)
    rime_api->set_option(pair.second.session_id, "ascii_mode", m_shared_ascii_mode);
  // No editable caret exists: retain the mode, never show at stale coordinates.
  m_focus_tip_pending = 0;
  if (m_ui) m_ui->Hide();
}

void RimeWithWeaselHandler::FocusOut(DWORD param, WeaselSessionId ipc_id) {
  DLOG(INFO) << "Focus out: ipc_id = " << ipc_id;
  if (param & 0x80000000u) m_profile_active = false;
  if (m_active_session != ipc_id) return;
  m_focus_tip_pending = 0;
  // A live mode tip can travel to the next field; its deadline still bounds
  // the lifetime when focus instead leaves editable content entirely.
  if (m_ui) m_ui->Hide();
  m_active_session = 0;
}

void RimeWithWeaselHandler::UpdateInputPosition(RECT const& rc,
                                                WeaselSessionId ipc_id) {
  DLOG(INFO) << "Update input position: (" << rc.left << ", " << rc.top
             << "), ipc_id = " << ipc_id
             << ", m_active_session = " << m_active_session;
  if (m_active_session != 0 && m_active_session != ipc_id)
    return;  // Ignore late caret updates from a field that already lost focus.
  if (m_ui && !m_disabled && m_focus_tip_pending == ipc_id &&
      m_active_session == ipc_id && rc.bottom > rc.top) {
    m_focus_tip_pending = 0;
    auto it = m_session_status_map.find(ipc_id);
    if (it != m_session_status_map.end() &&
        it->second.style.ssf_enabled && !it->second.style.ssf_skin.empty()) {
      Context ctx;
      Status status;
      _GetStatus(status, ipc_id, ctx);
      if (!status.disabled && !status.composing) {
        ctx = Context();
        status.show_mode_tip = true;
        m_ui->style() = it->second.style;
        m_ui->Update(ctx, status);
        m_ui->UpdateInputPosition(rc);
        if (m_show_notifications_time > 0)
          m_ui->ShowWithTimeout(m_show_notifications_time);
        return;
      }
    }
  }
  if (m_ui)
    m_ui->UpdateInputPosition(rc);
  if (m_disabled)
    return;
  if (m_active_session != ipc_id) {
    _UpdateUI(ipc_id);
    m_active_session = ipc_id;
  }
}

std::string RimeWithWeaselHandler::m_message_type;
std::string RimeWithWeaselHandler::m_message_value;
std::string RimeWithWeaselHandler::m_message_label;
std::string RimeWithWeaselHandler::m_option_name;
RimeSessionId RimeWithWeaselHandler::m_message_session_id = 0;
std::mutex RimeWithWeaselHandler::m_notifier_mutex;

void RimeWithWeaselHandler::OnNotify(void* context_object,
                                     uintptr_t session_id,
                                     const char* message_type,
                                     const char* message_value) {
  // may be running in a thread when deploying rime
  RimeWithWeaselHandler* self =
      reinterpret_cast<RimeWithWeaselHandler*>(context_object);
  if (!self || !message_type || !message_value)
    return;
  std::lock_guard<std::mutex> lock(m_notifier_mutex);
  m_message_session_id = static_cast<RimeSessionId>(session_id);
  m_message_type = message_type;
  m_message_value = message_value;
  if (RIME_API_AVAILABLE(rime_api, get_state_label) &&
      !strcmp(message_type, "option")) {
    Bool state = message_value[0] != '!';
    const char* option_name = message_value + !state;
    m_option_name = option_name;
    const char* state_label =
        rime_api->get_state_label(session_id, option_name, state);
    if (state_label) {
      m_message_label = std::string(state_label);
    }
  }
}

void RimeWithWeaselHandler::_ReadClientInfo(WeaselSessionId ipc_id,
                                            LPWSTR buffer) {
  std::string app_name;
  // parse request text
  wbufferstream bs(buffer, WEASEL_IPC_BUFFER_LENGTH);
  std::wstring line;
  while (bs.good()) {
    std::getline(bs, line);
    if (!bs.good())
      break;
    // file ends
    if (line == L".")
      break;
    const std::wstring kClientAppKey = L"session.client_app=";
    if (starts_with(line, kClientAppKey)) {
      std::wstring lwr = line;
      to_lower(lwr);
      app_name = wtou8(lwr.substr(kClientAppKey.length()));
    }
  }
  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  // set app specific options
  if (!app_name.empty()) {
    rime_api->set_property(session_id, "client_app", app_name.c_str());

    auto it = m_app_options.find(app_name);
    if (it != m_app_options.end()) {
      AppOptions& options(m_app_options[it->first]);
      for (const auto& pair : options) {
        DLOG(INFO) << "set app option: " << pair.first << " = " << pair.second;
        rime_api->set_option(session_id, pair.first.c_str(), Bool(pair.second));
      }
    }
  }
  // inline preedit
  bool inline_preedit = session_status.style.inline_preedit;
  rime_api->set_option(session_id, "inline_preedit", Bool(inline_preedit));
  // show soft cursor on weasel panel but not inline
  rime_api->set_option(session_id, "soft_cursor", Bool(!inline_preedit));
}

void RimeWithWeaselHandler::_GetCandidateInfo(CandidateInfo& cinfo,
                                              RimeContext& ctx) {
  cinfo.candies.resize(ctx.menu.num_candidates);
  cinfo.comments.resize(ctx.menu.num_candidates);
  cinfo.labels.resize(ctx.menu.num_candidates);
  for (int i = 0; i < ctx.menu.num_candidates; ++i) {
    cinfo.candies[i].str = escape_string(u8tow(ctx.menu.candidates[i].text));
    if (ctx.menu.candidates[i].comment) {
      cinfo.comments[i].str =
          escape_string(u8tow(ctx.menu.candidates[i].comment));
    }
    if (RIME_STRUCT_HAS_MEMBER(ctx, ctx.select_labels) && ctx.select_labels) {
      cinfo.labels[i].str = escape_string(u8tow(ctx.select_labels[i]));
    } else if (ctx.menu.select_keys) {
      cinfo.labels[i].str =
          escape_string(std::wstring(1, ctx.menu.select_keys[i]));
    } else {
      cinfo.labels[i].str = std::to_wstring((i + 1) % 10);
    }
  }
  cinfo.highlighted = ctx.menu.highlighted_candidate_index;
  cinfo.currentPage = ctx.menu.page_no;
  cinfo.is_last_page = ctx.menu.is_last_page;
}

void RimeWithWeaselHandler::StartMaintenance() {
  m_session_status_map.clear();
  Finalize();
  _UpdateUI(0);
}

void RimeWithWeaselHandler::EndMaintenance() {
  if (m_disabled) {
    Initialize();
    _UpdateUI(0);
  }
  m_session_status_map.clear();
}

void RimeWithWeaselHandler::SetOption(WeaselSessionId ipc_id,
                                      const std::string& opt,
                                      bool val) {
  // ascii_mode used to be ignored here when the custom SSF profile was active,
  // on the theory that language changes should only come from a bare Shift.
  // That left the TSF language-bar / tray CN-EN button a visible system control
  // that did nothing: the button flipped its own icon, the request was dropped,
  // and the next refresh snapped the icon back.  Accepting it makes the button
  // real and lets the icon follow the confirmed state.
  ModeDbg(L"[MODEDBG] SetOption: opt=" + u8tow(opt) + L" val=" + std::to_wstring(val ? 1 : 0) + L" ipc_id=" + std::to_wstring(ipc_id) + L" shared_before=" + std::to_wstring(m_shared_ascii_mode ? 1 : 0) + L" global=" + std::to_wstring(m_global_ascii_mode ? 1 : 0));
  const auto mark_mode_tip_request = [&](WeaselSessionId id) {
    if (opt == "ascii_mode" && id)
      get_session_status(id).mode_tip_requested = true;
  };
  // from no-session client, not actual typing session
  if (!ipc_id) {
    if (m_global_ascii_mode && opt == "ascii_mode") {
      // Keep the shared value authoritative.  ProcessKeyEvent and FocusIn both
      // push m_shared_ascii_mode back into the sessions, so leaving it stale
      // here would silently undo a toggle made from the language bar or tray.
      m_shared_ascii_mode = val;
      for (auto& pair : m_session_status_map) {
        rime_api->set_option(to_session_id(pair.first), "ascii_mode", val);
        // Change every Rime session, but surface the image only in the
        // foreground session where the user actually toggled the mode.  A
        // background application's later refresh must never look like a new
        // keyboard action (nor like a keypad-number notification).
        if (pair.first == m_active_session)
          pair.second.mode_tip_requested = true;
      }
    } else {
      rime_api->set_option(to_session_id(m_active_session), opt.c_str(), val);
      mark_mode_tip_request(m_active_session);
    }
  } else {
    // A request that carries a session id -- which is what the TSF language bar
    // and tray do -- used to update only that session's option.  With
    // m_global_ascii_mode the mode is shared state, and ProcessKeyEvent pushes
    // m_shared_ascii_mode back into the session on the next key, so a stale
    // shared value silently reverted the user's switch: the icon changed but
    // typing stayed in the old language.  Keep the shared value in step
    // whichever entry point performed the change.
    if (opt == "ascii_mode" && m_global_ascii_mode) {
      ModeDbg(L"[MODEDBG] SetOption: session-scoped ascii_mode, syncing shared " +
              std::to_wstring(m_shared_ascii_mode ? 1 : 0) + L" -> " +
              std::to_wstring(val ? 1 : 0));
      m_shared_ascii_mode = val;
      // Apply to every session so the whole input method agrees, matching the
      // no-session branch above.
      for (auto& pair : m_session_status_map) {
        rime_api->set_option(to_session_id(pair.first), "ascii_mode", val);
        if (pair.first == m_active_session)
          pair.second.mode_tip_requested = true;
      }
    } else {
      rime_api->set_option(to_session_id(ipc_id), opt.c_str(), val);
      mark_mode_tip_request(ipc_id);
    }
  }
  // refresh UI (and tray icon) so the option change takes effect immediately,
  // e.g. when toggling ascii_mode from the TSF language bar
  _UpdateUI(ipc_id ? ipc_id : m_active_session);
}

void RimeWithWeaselHandler::OnUpdateUI(std::function<void()> const& cb) {
  _UpdateUICallback = cb;
}

// {A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A} -- must match WeaselTSF's
// c_clsidTextService in Globals.cpp.  WeaselServer and WeaselTSF are separate
// binaries with separate Globals, so the value lives in a shared header.
//
// Getting this right matters more than it looks.  Measured on this machine,
// GetActiveProfile keeps reporting WEASEL even after the user selects the US
// keyboard layout, so "is the active profile Weasel" is NOT enough to tell the
// two apart -- an earlier attempt used exactly that and Shift kept toggling
// while the US layout was selected.
//
// What does change is the layout of the foreground thread: 0x08040804
// (langid 0x0804) with Weasel, 0x04090409 (langid 0x0409) with the US layout.
// So the test is whether the layout the user's foreground thread actually has
// selected belongs to the same language as the active profile.
bool RimeWithWeaselHandler::WeaselHasForegroundKeyboard() {
  // The keyboard hook calls this on every Shift press, so throttle the COM
  // query.  A stale answer for a few hundred milliseconds is harmless: it is
  // only consulted for a deliberate Shift tap.
  static ULONGLONG checked_at = 0;
  static bool is_weasel = false;
  const ULONGLONG now = GetTickCount64();
  if (checked_at != 0 && now - checked_at < 200) return is_weasel;
  checked_at = now;

  // The active profile is per input thread, and the user is typing in the
  // foreground window's thread.  GetKeyboardLayout(0) would report this
  // message-loop thread instead.
  HWND foreground = ::GetForegroundWindow();
  if (!foreground) return (is_weasel = false);
  const DWORD thread = ::GetWindowThreadProcessId(foreground, nullptr);
  if (!thread) return (is_weasel = false);
  const HKL foreground_hkl = ::GetKeyboardLayout(thread);
  if (!foreground_hkl) return (is_weasel = false);

  CComPtr<ITfInputProcessorProfileMgr> profile_mgr;
  if (FAILED(profile_mgr.CoCreateInstance(CLSID_TF_InputProcessorProfiles,
                                          nullptr, CLSCTX_ALL)))
    return (is_weasel = false);

  TF_INPUTPROCESSORPROFILE profile = {};
  if (FAILED(profile_mgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile)))
    return (is_weasel = false);

  if (!weasel_tsf::ProfileIsWeasel(profile.clsid))
    return (is_weasel = false);

  // The decisive comparison: the layout the foreground thread has selected must
  // be in the same language as the Weasel profile.  Selecting the US layout
  // moves the thread to langid 0x0409 and the toggle is refused.
  const LANGID thread_langid =
      LOWORD(reinterpret_cast<ULONG_PTR>(foreground_hkl));
  is_weasel = weasel_tsf::LayoutLanguageMatchesProfile(thread_langid,
                                                       profile.langid);
  return is_weasel;
}

bool RimeWithWeaselHandler::_IsDeployerRunning() {
  HANDLE hMutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  bool deployer_detected = hMutex && GetLastError() == ERROR_ALREADY_EXISTS;
  if (hMutex) {
    CloseHandle(hMutex);
  }
  return deployer_detected;
}

void RimeWithWeaselHandler::_UpdateUI(WeaselSessionId ipc_id) {
  // if m_ui nullptr, _UpdateUI meaningless
  if (!m_ui)
    return;

  // Work on a snapshot. Mutating UI::status() before UI::Update makes its
  // equality guard compare the new state to itself and skip the icon redraw.
  Status weasel_status = m_ui->status();
  SessionStatus& session_status = get_session_status(ipc_id);
  const bool previous_ascii_mode = session_status.mode_tip_last_ascii_mode;
  const bool ascii_mode_was_initialized =
      session_status.mode_tip_ascii_initialized;
  const bool mode_tip_was_requested = session_status.mode_tip_requested;
  const bool suppress_mode_tip = session_status.suppress_mode_tip_once;
  if (suppress_mode_tip)
    m_ui->Hide();  // Cancel any previous mode-tip countdown on numeric input.
  // This is an event flag, not a persistent input-mode property. Clear it on
  // every ordinary refresh; _ShowMessage enables it only for ascii_mode.
  weasel_status.show_mode_tip = false;
  weasel_status.ascii_mode_changed = false;
  Context weasel_context;

  RimeSessionId session_id = to_session_id(ipc_id);

  if (ipc_id == 0)
    weasel_status.disabled = m_disabled;

  _GetStatus(weasel_status, ipc_id, weasel_context);
  const bool ascii_mode_changed =
      ascii_mode_was_initialized &&
      previous_ascii_mode != weasel_status.ascii_mode;
  session_status.mode_tip_last_ascii_mode = weasel_status.ascii_mode;
  session_status.mode_tip_ascii_initialized = true;
  session_status.mode_tip_requested = false;
  session_status.suppress_mode_tip_once = false;
  weasel_status.ascii_mode_changed = suppress_mode_tip ? false
                                                     : ascii_mode_changed;
  weasel_status.ascii_mode_initialized = true;
  if (rime_api->get_option(session_id, "inline_preedit"))
    session_status.style.client_caps |= INLINE_PREEDIT_CAPABLE;
  else
    session_status.style.client_caps &= ~INLINE_PREEDIT_CAPABLE;

  if (!_ShowMessage(ipc_id, weasel_context, weasel_status,
                    !suppress_mode_tip && mode_tip_was_requested &&
                        ascii_mode_changed)) {
    m_ui->Hide();
    m_ui->Update(weasel_context, weasel_status);
  }

  _RefreshTrayIcon(session_id, _UpdateUICallback);

  {
    std::lock_guard<std::mutex> lock(m_notifier_mutex);
    m_message_type.clear();
    m_message_value.clear();
    m_message_label.clear();
    m_option_name.clear();
    m_message_session_id = 0;
  }
}

void RimeWithWeaselHandler::_LoadSchemaSpecificSettings(
    WeaselSessionId ipc_id,
    const std::string& schema_id) {
  if (!m_ui)
    return;
  RimeConfig config;
  if (!rime_api->schema_open(schema_id.c_str(), &config))
    return;
  _UpdateShowNotifications(&config);
  m_ui->style() = m_base_style;
  DebugStream() << L"[SSF] WRITE-C (LoadSchemaSpecific) ui->style = m_base_style "
                   L"-> skin='"
                << m_ui->style().ssf_skin << L"'\n";
  _UpdateUIStyle(&config, m_ui, false);
  // Re-apply the global SSF skin settings.
  //
  // The two lines above replace the whole UIStyle with m_base_style and then
  // re-read it from the *schema* config. A schema config has no style/ssf_*
  // keys, so without this the skin selection is lost as soon as a schema is
  // loaded -- the config parses correctly, _UpdateUIStyle sets it, and the panel
  // still sees an empty ssf_skin. That is exactly the bug that made the skin
  // silently fail to appear.
  {
    RimeConfig global = {NULL};
    if (rime_api->config_open("weasel", &global)) {
      _LoadSsfSkinSettings(&global, m_ui->style());
      rime_api->config_close(&global);
    }
  }
  SessionStatus& session_status = get_session_status(ipc_id);
  session_status.style = m_ui->style();
  UIStyle& style = session_status.style;
  // load schema color style config
  const int BUF_SIZE = 255;
  char buffer[BUF_SIZE + 1] = {0};
  const auto update_color_scheme = [&]() {
    std::string color_name(buffer);
    RimeConfigIterator preset = {0};
    if (rime_api->config_begin_map(
            &preset, &config, ("preset_color_schemes/" + color_name).c_str())) {
      _UpdateUIStyleColor(&config, style, color_name);
      rime_api->config_end(&preset);
    } else {
      RimeConfig weaselconfig;
      if (rime_api->config_open("weasel", &weaselconfig)) {
        _UpdateUIStyleColor(&weaselconfig, style, color_name);
        rime_api->config_close(&weaselconfig);
      }
    }
  };
  const char* key =
      m_current_dark_mode ? "style/color_scheme_dark" : "style/color_scheme";
  if (rime_api->config_get_string(&config, key, buffer, BUF_SIZE))
    update_color_scheme();
  // load schema icon start
  {
    const auto load_icon = [](RimeConfig& config, const char* key1,
                              const char* key2) {
      const auto user_dir = WeaselUserDataPath();
      const auto shared_dir = WeaselSharedDataPath();
      const int BUF_SIZE = 255;
      char buffer[BUF_SIZE + 1] = {0};
      if (rime_api->config_get_string(&config, key1, buffer, BUF_SIZE) ||
          (key2 != NULL &&
           rime_api->config_get_string(&config, key2, buffer, BUF_SIZE))) {
        auto resource = u8tow(buffer);
        if (fs::is_regular_file(user_dir / resource))
          return (user_dir / resource).wstring();
        else if (fs::is_regular_file(shared_dir / resource))
          return (shared_dir / resource).wstring();
      }
      return std::wstring();
    };
    style.current_zhung_icon =
        load_icon(config, "schema/icon", "schema/zhung_icon");
    style.current_ascii_icon = load_icon(config, "schema/ascii_icon", NULL);
    style.current_full_icon = load_icon(config, "schema/full_icon", NULL);
    style.current_half_icon = load_icon(config, "schema/half_icon", NULL);
  }
  // load schema icon end
  rime_api->config_close(&config);
}

void RimeWithWeaselHandler::_LoadAppInlinePreeditSet(WeaselSessionId ipc_id,
                                                     bool ignore_app_name) {
  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  static char _app_name[50];
  rime_api->get_property(session_id, "client_app", _app_name,
                         sizeof(_app_name) - 1);
  std::string app_name(_app_name);
  if (!ignore_app_name && m_last_app_name == app_name)
    return;
  m_last_app_name = app_name;
  bool inline_preedit = session_status.style.inline_preedit;
  bool found = false;
  if (!app_name.empty()) {
    auto it = m_app_options.find(app_name);
    if (it != m_app_options.end()) {
      AppOptions& options(m_app_options[it->first]);
      for (const auto& pair : options) {
        if (pair.first == "inline_preedit") {
          rime_api->set_option(session_id, pair.first.c_str(),
                               Bool(pair.second));
          session_status.style.inline_preedit = Bool(pair.second);
          found = true;
          break;
        }
      }
    }
  }
  if (!found) {
    session_status.style.inline_preedit = m_base_style.inline_preedit;
    // load from schema.
    RIME_STRUCT(RimeStatus, status);
    if (rime_api->get_status(session_id, &status)) {
      std::string schema_id = status.schema_id;
      RimeConfig config;
      if (rime_api->schema_open(schema_id.c_str(), &config)) {
        Bool value = False;
        if (rime_api->config_get_bool(&config, "style/inline_preedit",
                                      &value)) {
          session_status.style.inline_preedit = value;
        }
        rime_api->config_close(&config);
      }
      rime_api->free_status(&status);
    }
  }
  if (session_status.style.inline_preedit != inline_preedit)
    _UpdateInlinePreeditStatus(ipc_id);
}

bool RimeWithWeaselHandler::_ShowMessage(WeaselSessionId ipc_id,
                                         Context& ctx,
                                         Status& status,
                                         bool force_mode_tip) {
  std::lock_guard<std::mutex> lock(m_notifier_mutex);
  if (m_message_type.empty() || m_message_value.empty()) {
    if (!force_mode_tip)
      // A mode-tip countdown may be preserved only while the new context is
      // still empty. Once pinyin/preedit content arrives, the old icon must
      // yield to the candidate panel immediately; otherwise it continues to
      // receive caret updates and appears to jump around beside the candidates.
      return m_ui->IsCountingDown() && ctx.empty();
    if (m_active_session != ipc_id || m_focus_tip_pending == ipc_id)
      return false;  // Wait for the new field's valid caret.
    status.show_mode_tip = true;
    m_ui->Hide();  // Replace the previous mode image and countdown.
    m_ui->Update(ctx, status);
    if (m_show_notifications_time)
      m_ui->ShowWithTimeout(m_show_notifications_time);
    return true;
  }
  const bool notification_targets_current_session =
      m_message_session_id == to_session_id(ipc_id);
  // show as auxiliary string
  std::wstring& tips(ctx.aux.str);
  bool show_icon = force_mode_tip;
  bool is_ascii_mode_notification = false;
  if (m_message_type == "deploy") {
    if (m_message_value == "start")
      if (GetThreadUILanguage() == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US))
        tips = L"Deploying RIME";
      else
        tips = L"正在部署 RIME";
    else if (m_message_value == "success")
      if (GetThreadUILanguage() == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US))
        tips = L"Deployed";
      else
        tips = L"部署完成";
    else if (m_message_value == "failure") {
      if (GetThreadUILanguage() ==
          MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL))
        tips = L"有錯誤，請查看日誌 %TEMP%\\rime.weasel\\rime.weasel.*.INFO";
      else if (GetThreadUILanguage() ==
               MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
        tips = L"有错误，请查看日志 %TEMP%\\rime.weasel\\rime.weasel.*.INFO";
      else
        tips =
            L"There is an error, please check the logs "
            L"%TEMP%\\rime.weasel\\rime.weasel.*.INFO";
    }
  } else if (m_message_type == "schema") {
    tips = /*L"【" + */ status.schema_name /* + L"】"*/;
  } else if (m_message_type == "option") {
    status.type = SCHEMA;
    if (m_message_value == "!ascii_mode") {
      is_ascii_mode_notification = true;
      show_icon = show_icon ||
                  (notification_targets_current_session &&
                   status.ascii_mode_changed);
    } else if (m_message_value == "ascii_mode") {
      is_ascii_mode_notification = true;
      show_icon = show_icon ||
                  (notification_targets_current_session &&
                   status.ascii_mode_changed);
    } else
      tips = u8tow(m_message_label);

    if (m_message_value == "full_shape" || m_message_value == "!full_shape")
      status.type = FULL_SHAPE;
  }
  // Keep the cursor-near indicator exclusive to an explicit CN/EN toggle.
  // In particular, a literal numpad digit commit must not reopen an empty tip.
  status.show_mode_tip = show_icon;
  auto counter = m_ui->IsCountingDown();
  // Rime can repeat an ascii_mode option notification while it merely refreshes
  // composition (for example when a virtual keypad digit is edited). Without
  // a real state change there is nothing to show and no empty tip is created.
  if (is_ascii_mode_notification && !show_icon)
    return counter;
  // Do not let an older CN/EN tip suppress a newly non-empty composition.
  // The candidate window owns that state and must replace the tip at once.
  if (!show_icon && counter)
    return counter && ctx.empty();
  // The Color-P CN/EN hint is a skin-owned, cursor-near overlay rather than a
  // normal textual notification.  Do not make its only display path depend on
  // show_notifications: most schemas leave ascii_mode out of that map, which
  // previously meant a real toggle updated the status but never showed cn2.png
  // or a2.png.  show_icon is true only for an explicit ascii_mode notification
  // with an observed state transition, so keypad input and ordinary refreshes
  // cannot reach this branch.
  if (show_icon) {
    if (m_active_session != ipc_id || m_focus_tip_pending == ipc_id)
      return false;
    m_ui->Hide();  // Only the newest CN/EN indicator remains visible.
    m_ui->Update(ctx, status);
    if (m_show_notifications_time)
      m_ui->ShowWithTimeout(m_show_notifications_time);
    return true;
  }
  auto foption = m_show_notifications.find(m_option_name);
  auto falways = m_show_notifications.find("always");
  if (!ctx.empty() && ((!add_session && (foption != m_show_notifications.end() ||
                        falways != m_show_notifications.end())) ||
      m_message_type == "deploy")) {
    m_ui->Update(ctx, status);
    if (m_show_notifications_time)
      m_ui->ShowWithTimeout(m_show_notifications_time);
    return true;
  } else {
    return m_ui->IsCountingDown();
  }
}
inline std::string _GetLabelText(const std::vector<Text>& labels,
                                 int id,
                                 const wchar_t* format) {
  wchar_t buffer[128];
  swprintf_s<128>(buffer, format, labels.at(id).str.c_str());
  return wtou8(std::wstring(buffer));
}

bool RimeWithWeaselHandler::_Respond(WeaselSessionId ipc_id, EatLine eat) {
  std::wstring body;
  body.reserve(4096);
  std::vector<const char*> actions;
  actions.reserve(8);

  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  RIME_STRUCT(RimeCommit, commit);
  if (rime_api->get_commit(session_id, &commit)) {
    actions.push_back("commit");
    std::wstring commit_text_w = escape_string(u8tow(commit.text));
    body.append(L"commit=").append(commit_text_w).append(L"\n");
    rime_api->free_commit(&commit);
  }

  bool is_composing = false;
  RIME_STRUCT(RimeStatus, status);
  static const std::wstring Bool_wstring[] = {L"0", L"1"};
  if (rime_api->get_status(session_id, &status)) {
    is_composing = !!status.is_composing;
    actions.push_back("status");
    body.append(L"status.ascii_mode=")
        .append(Bool_wstring[!!status.is_ascii_mode])
        .append(L"\n")
        .append(L"status.composing=")
        .append(Bool_wstring[!!status.is_composing])
        .append(L"\n")
        .append(L"status.disabled=")
        .append(Bool_wstring[!!status.is_disabled])
        .append(L"\n")
        .append(L"status.full_shape=")
        .append(Bool_wstring[!!status.is_full_shape])
        .append(L"\n")
        .append(L"status.schema_id=")
        .append(status.schema_id ? u8tow(status.schema_id) : std::wstring())
        .append(L"\n");
    if (m_global_ascii_mode && !m_base_style.ssf_enabled &&
        (session_status.status.is_ascii_mode != status.is_ascii_mode)) {
      for (auto& pair : m_session_status_map) {
        if (pair.first != ipc_id)
          rime_api->set_option(to_session_id(pair.first), "ascii_mode",
                               !!status.is_ascii_mode);
      }
    }
    session_status.status = status;
    rime_api->free_status(&status);
  }

  RIME_STRUCT(RimeContext, ctx);
  if (rime_api->get_context(session_id, &ctx)) {
    bool has_candidates = ctx.menu.num_candidates > 0 &&
        get_keypad_property(session_id, "weasel_keypad_literal_only") != "1";
    CandidateInfo cinfo;
    if (has_candidates) {
      _GetCandidateInfo(cinfo, ctx);
    }
    if (is_composing) {
      const auto& preedit = ctx.composition.preedit;
      const std::string keypad_prefix = get_keypad_preedit_prefix(session_id);
      const std::string keypad_suffix = get_keypad_preedit_suffix(session_id);
      const std::string raw_preedit = preedit ? preedit : "";
      const int keypad_cursor =
          get_keypad_preedit_cursor(session_id, keypad_suffix);
      const std::string display_preedit =
          keypad_prefix + raw_preedit + keypad_suffix;
      const auto& start = ctx.composition.sel_start;
      const auto& end = ctx.composition.sel_end;
      const auto chars_before = [](const std::string& text, size_t bytes) {
        const size_t clamped = (std::min)(bytes, text.size());
        return utf8towcslen(text.c_str(), static_cast<int>(clamped));
      };
      const int prefix_chars = chars_before(keypad_prefix, keypad_prefix.size());
      const int raw_start_chars =
          chars_before(raw_preedit, static_cast<size_t>((std::max)(0, start)));
      const int raw_end_chars =
          chars_before(raw_preedit, static_cast<size_t>((std::max)(0, end)));
      const int raw_cursor_chars = chars_before(
          raw_preedit, static_cast<size_t>((std::max)(
                           0, (std::min)(ctx.composition.cursor_pos,
                                         ctx.composition.length))));
      // Rime and the Lua tail report UTF-8 byte offsets.  IPC expects the
      // rendered UTF-16 character index, including the virtual selected-word
      // prefix that remains in the input bar.
      int cursor_chars = prefix_chars +
          (keypad_suffix.empty() || keypad_cursor < 0
               ? raw_cursor_chars
               : chars_before(raw_preedit, raw_preedit.size()) +
                     chars_before(keypad_suffix, keypad_cursor));
      const auto prefix_cursor = get_keypad_property(session_id, "weasel_keypad_prefix_cursor");
      if (!prefix_cursor.empty()) cursor_chars = chars_before(keypad_prefix,
          static_cast<size_t>(std::strtoul(prefix_cursor.c_str(), nullptr, 10)));
      static const auto u8towstring = [](const char* u8str, int len = 0) {
        return std::to_wstring(utf8towcslen(u8str, len));
      };
      actions.push_back("ctx");
      switch (session_status.style.preedit_type) {
        case UIStyle::PREVIEW: {
          if (ctx.commit_text_preview && keypad_prefix.empty() &&
              keypad_suffix.empty()) {
            const char* first_utf8 = ctx.commit_text_preview;
            const size_t first_len = std::strlen(first_utf8);
            const std::wstring first_w = escape_string(u8tow(first_utf8));
            const std::wstring tmp = u8towstring(first_utf8, (int)first_len);
            body.append(L"ctx.preedit=")
                .append(first_w)
                .append(L"\n")
                .append(L"ctx.preedit.cursor=")
                .append(u8towstring(first_utf8, 0))
                .append(L",")
                .append(tmp)
                .append(L",")
                .append(tmp)
                .append(L"\n");
            break;
          }
          // no preview, fall back to composition
        }
        case UIStyle::COMPOSITION: {
          body.append(L"ctx.preedit=")
              .append(escape_string(u8tow(display_preedit)))
              .append(L"\n");
          if (start <= end) {
            body.append(L"ctx.preedit.cursor=")
                .append(std::to_wstring(prefix_chars + raw_start_chars))
                .append(L",")
                .append(std::to_wstring(prefix_chars + raw_end_chars))
                .append(L",")
                .append(std::to_wstring(cursor_chars))
                .append(L"\n");
          }
          break;
        }
        case UIStyle::PREVIEW_ALL: {
          body.append(L"ctx.preedit=")
              .append(escape_string(u8tow(display_preedit)))
              .append(L"  [");
          auto label_valid = session_status.style.label_font_point > 0;
          auto comment_valid = session_status.style.comment_font_point > 0;
          const std::wstring mark_text_w =
              session_status.style.mark_text.empty()
                  ? std::wstring(L"*")
                  : session_status.style.mark_text;
          for (auto i = 0; i < ctx.menu.num_candidates; i++) {
            std::wstring label_w;
            if (label_valid) {
              wchar_t buf_lbl[128];
              swprintf_s<128>(buf_lbl,
                              session_status.style.label_text_format.c_str(),
                              cinfo.labels.at(i).str.c_str());
              label_w = std::wstring(buf_lbl);
            }
            std::wstring comment_w =
                comment_valid ? cinfo.comments.at(i).str : std::wstring();
            std::wstring prefix_w = (i != ctx.menu.highlighted_candidate_index)
                                        ? std::wstring()
                                        : mark_text_w;
            body.append(L" ")
                .append(prefix_w)
                .append(escape_string(label_w))
                .append(escape_string(u8tow(ctx.menu.candidates[i].text)))
                .append(L" ")
                .append(escape_string(comment_w));
          }
          body.append(L" ]\n");
          if (start <= end) {
            body.append(L"ctx.preedit.cursor=")
                .append(std::to_wstring(prefix_chars + raw_start_chars))
                .append(L",")
                .append(std::to_wstring(prefix_chars + raw_end_chars))
                .append(L",")
                .append(std::to_wstring(cursor_chars))
                .append(L"\n");
          }
          break;
        }
      }
    }
    if (has_candidates) {
      std::wstringstream ss;
      boost::archive::text_woarchive oa(ss);

      oa << cinfo;

      auto s = ss.str();
      body.append(L"ctx.cand=").append(std::move(s)).append(L"\n");
    }
    rime_api->free_context(&ctx);
  }

  // configuration information
  actions.push_back("config");
  body.append(L"config.inline_preedit=")
      .append(std::to_wstring((int)session_status.style.inline_preedit))
      .append(L"\n");

  // style
  if (!session_status.__synced) {
    std::wstringstream ss;
    boost::archive::text_woarchive oa(ss);
    oa << session_status.style;

    actions.push_back("style");
    body.append(L"style=").append(ss.str()).append(L"\n");
    session_status.__synced = true;
  }

  // summarize: send header first to avoid vector head-insert cost
  std::wstring header;
  if (actions.empty()) {
    header = L"action=noop\n";
  } else {
    std::string actionList;
    actionList.reserve(64);
    for (size_t i = 0; i < actions.size(); ++i) {
      if (i > 0)
        actionList += ',';
      actionList += actions[i];
    }
    header = std::wstring(L"action=") + u8tow(actionList) + L"\n";
  }
  if (!eat(header))
    return false;

  body.append(L".\n");
  if (!eat(body))
    return false;

  return true;
}

// Blend foreground and background ARGB colors taking alpha into account.
// Returns an ABGR COLORREF with premultiplied alpha blended result.
static inline COLORREF blend_colors(COLORREF fcolor, COLORREF bcolor) {
  // Extract ARGB channels from both colors.
  BYTE fA = (fcolor >> 24) & 0xFF;
  BYTE fB = (fcolor >> 16) & 0xFF;
  BYTE fG = (fcolor >> 8) & 0xFF;
  BYTE fR = fcolor & 0xFF;
  BYTE bA = (bcolor >> 24) & 0xFF;
  BYTE bB = (bcolor >> 16) & 0xFF;
  BYTE bG = (bcolor >> 8) & 0xFF;
  BYTE bR = bcolor & 0xFF;
  // Convert alpha to [0,1]
  float fAlpha = fA / 255.0f;
  float bAlpha = bA / 255.0f;
  // Result alpha
  float retAlpha = fAlpha + (1 - fAlpha) * bAlpha;
  if (retAlpha <= 1e-6f) {
    // Fully transparent result — return background unchanged as fallback.
    return bcolor;
  }
  auto mix = [&](float fc, float bc) -> BYTE {
    return static_cast<BYTE>((fc * fAlpha + bc * bAlpha * (1 - fAlpha)) /
                             retAlpha);
  };
  BYTE retR = mix(fR, bR);
  BYTE retG = mix(fG, bG);
  BYTE retB = mix(fB, bB);
  BYTE outA = static_cast<BYTE>(retAlpha * 255.0f);
  return (static_cast<COLORREF>(outA) << 24) | (retB << 16) | (retG << 8) |
         retR;
}
// parse color value, with fallback value
static Bool _RimeGetColor(RimeConfig* config,
                          const std::string& key,
                          int& value,
                          const ColorFormat& fmt,
                          const unsigned int& fallback) {
  char color[256] = {0};
  if (!rime_api->config_get_string(config, key.c_str(), color, 256)) {
    value = fallback;
    return False;
  }
  const auto color_str = std::string(color);
  // adjudge if str is 0x 0X # hex color format, return trimmed hex part
  // out part is 6 or 8 length hex string without white space
  const auto parse_color_code = [](const std::string& str, std::string& out) {
    if (str.empty())
      return false;
    size_t start = 0;
    if (str[0] == '#') {
      start = 1;
    } else if (str.size() >= 2 &&
               (str.compare(0, 2, "0x") == 0 || str.compare(0, 2, "0X") == 0)) {
      start = 2;
    } else {
      return false;
    }
    const std::string hex_part = str.substr(start);
    if (hex_part.empty())
      return false;
    if ((start == 1 || start == 2) && hex_part.length() != 3 &&
        hex_part.length() != 4 && hex_part.length() != 6 &&
        hex_part.length() != 8) {
      return false;
    }
    for (char c : hex_part) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F')))
        return false;
    }
    out = str.substr(start).substr(0, 8);
#define _2C(c) std::string(2, c)
    if (out.size() == 3)
      out = _2C(out[0]) + _2C(out[1]) + _2C(out[2]);
    else if (out.size() == 4)
      out = _2C(out[0]) + _2C(out[1]) + _2C(out[2]) + _2C(out[3]);
#undef _2C
    return true;
  };
  auto hex_color = std::string();
  if (parse_color_code(color_str, hex_color)) {
    value = std::stoul(hex_color, 0, 16);
    if (hex_color.length() == 6)
      value = (fmt != COLOR_RGBA) ? (value | 0xff000000)
                                  : (((unsigned int)value << 8) | 0x000000ff);
  } else {
    if (!rime_api->config_get_int(config, key.c_str(), &value)) {
      value = fallback;
      return False;
    }
    if (value <= 0xffffff)
      value = (fmt != COLOR_RGBA) ? (value | 0xff000000)
                                  : (((unsigned int)value << 8) | 0x000000ff);
    else if (value > 0xffffffff)
      value &= 0xffffffff;
  }
  if (fmt == COLOR_ARGB)
    value = ARGB2ABGR(value);
  else if (fmt == COLOR_RGBA)
    value = RGBA2ABGR(value);
  value &= 0xffffffff;
  return True;
}

template <typename T, size_t N>
using Array = std::array<std::pair<const char*, T>, N>;

// parset bool type configuration to T type value trueValue / falseValue
template <typename T>
void _RimeGetBool(RimeConfig* config,
                  const char* key,
                  bool cond,
                  T& value,
                  const T& trueValue = true,
                  const T& falseValue = false) {
  Bool tempb = False;
  if (rime_api->config_get_bool(config, key, &tempb) || cond)
    value = (!!tempb) ? trueValue : falseValue;
}
// parse string option to T type value, with fallback
template <typename T, size_t N>
void _RimeParseStringOptWithFallback(RimeConfig* config,
                                     const char* key,
                                     T& value,
                                     const Array<T, N>& arr,
                                     const T& fallback) {
  char str_buff[256] = {0};
  if (rime_api->config_get_string(config, key, str_buff, 255)) {
    for (size_t i = 0; i < N; ++i) {
      if (strcmp(arr[i].first, str_buff) == 0) {
        value = arr[i].second;
        return;
      }
    }
  }
  value = fallback;
}

template <typename T>
void _RimeGetIntStr(RimeConfig* config,
                    const char* key,
                    T& value,
                    const char* fb_key = nullptr,
                    const void* fb_value = nullptr,
                    const std::function<void(T&)>& func = nullptr) {
  if constexpr (std::is_same<T, int>::value) {
    if (!rime_api->config_get_int(config, key, &value) && fb_key != 0)
      rime_api->config_get_int(config, fb_key, &value);
  } else if constexpr (std::is_same<T, std::wstring>::value) {
    const int BUF_SIZE = 2047;
    char buffer[BUF_SIZE + 1] = {0};
    if (rime_api->config_get_string(config, key, buffer, BUF_SIZE) ||
        rime_api->config_get_string(config, fb_key, buffer, BUF_SIZE)) {
      value = u8tow(buffer);
    } else if (fb_value) {
      value = *(T*)fb_value;
    }
  }
  if (func)
    func(value);
}

// Helper to iterate a Rime map and invoke callback with key/path
static void ForEachRimeMap(
    RimeConfig* config,
    const std::string& path,
    const std::function<void(const char* key, const char* child_path)>& cb) {
  RimeConfigIterator iter;
  if (!rime_api->config_begin_map(&iter, config, path.c_str()))
    return;
  while (rime_api->config_next(&iter)) {
    cb(iter.key, iter.path);
  }
  rime_api->config_end(&iter);
}

// Helper to iterate a Rime list and invoke callback with item path
static void ForEachRimeList(
    RimeConfig* config,
    const std::string& path,
    const std::function<void(const char* item_path)>& cb) {
  RimeConfigIterator iter;
  if (!rime_api->config_begin_list(&iter, config, path.c_str()))
    return;
  while (rime_api->config_next(&iter)) {
    cb(iter.path);
  }
  rime_api->config_end(&iter);
}

void RimeWithWeaselHandler::_UpdateShowNotifications(RimeConfig* config,
                                                     bool initialize) {
  Bool show_notifications = true;
  if (initialize)
    m_show_notifications_base.clear();
  m_show_notifications.clear();

  if (rime_api->config_get_bool(config, "show_notifications",
                                &show_notifications)) {
    // config read as bool, for global all on or off
    if (show_notifications)
      m_show_notifications["always"] = true;
    if (initialize)
      m_show_notifications_base = m_show_notifications;
  } else {
    // read as list using helper
    ForEachRimeList(config, "show_notifications", [&](const char* item_path) {
      char buffer[256] = {0};
      if (rime_api->config_get_string(config, item_path, buffer, 256))
        m_show_notifications[std::string(buffer)] = true;
    });
    if (initialize)
      m_show_notifications_base = m_show_notifications;
    if (m_show_notifications.empty()) {
      // not configured, or incorrect type
      if (initialize)
        m_show_notifications_base["always"] = true;
      m_show_notifications = m_show_notifications_base;
    }
  }
}

// Read the global Sogou SSF skin settings (style/ssf_*).
//
// Split out from _UpdateUIStyle because those keys are GLOBAL, while
// _LoadSchemaSpecificSettings() replaces the whole UIStyle with m_base_style and
// then re-reads it from the *schema* config -- which knows nothing about
// style/ssf_*. Without re-applying them from the global config there, the skin
// selection is silently lost the first time a schema is loaded, and the panel
// then sees an empty ssf_skin. That was a real bug: the config parsed correctly
// and _UpdateUIStyle did set it, but the panel observed '' because of this
// reset.
void _LoadSsfSkinSettings(RimeConfig* config, UIStyle& style) {
  if (!config) return;
  _RimeGetIntStr(config, "style/ssf_skin", style.ssf_skin);
  _RimeGetBool(config, "style/ssf_enabled", false, style.ssf_enabled, true,
               false);
  _RimeGetBool(config, "style/ssf_status_bar", false, style.ssf_status_bar,
               true, false);
  // An absent skin must not leave the backend switched on: the panel would then
  // try to load '' as a directory.
  if (style.ssf_skin.empty()) style.ssf_enabled = false;
  DebugStream() << L"[SSF] global settings: skin='" << style.ssf_skin
                << L"' enabled=" << (style.ssf_enabled ? 1 : 0)
                << L" status_bar=" << (style.ssf_status_bar ? 1 : 0)
                << L" style_addr=" << &style << L"\n";
}

// update ui's style parameters, ui has been check before referenced
static void _UpdateUIStyle(RimeConfig* config, UI* ui, bool initialize) {
  UIStyle& style(ui->style());
  DebugStream() << L"[SSF] _UpdateUIStyle enter init=" << (initialize ? 1 : 0)
                << L" style.ssf_skin(before)='" << style.ssf_skin << L"'\n";
  const std::function<void(std::wstring&)> rmspace = [](std::wstring& str) {
    str = std::regex_replace(str, std::wregex(L"\\s*(,|:|^|$)\\s*"), L"$1");
  };
  const std::function<void(int&)> _abs = [](int& value) { value = abs(value); };
  // get font faces
  _RimeGetIntStr(config, "style/font_face", style.font_face, 0, 0, rmspace);
  std::wstring* const pFallbackFontFace = initialize ? &style.font_face : NULL;
  _RimeGetIntStr(config, "style/label_font_face", style.label_font_face, 0,
                 pFallbackFontFace, rmspace);
  _RimeGetIntStr(config, "style/comment_font_face", style.comment_font_face, 0,
                 pFallbackFontFace, rmspace);
  // able to set label font/comment font empty, force fallback to font face.
  if (style.label_font_face.empty())
    style.label_font_face = style.font_face;
  if (style.comment_font_face.empty())
    style.comment_font_face = style.font_face;
  // get font points
  _RimeGetIntStr(config, "style/font_point", style.font_point);
  if (style.font_point <= 0)
    style.font_point = 12;
  _RimeGetIntStr(config, "style/label_font_point", style.label_font_point,
                 "style/font_point", 0, _abs);
  _RimeGetIntStr(config, "style/comment_font_point", style.comment_font_point,
                 "style/font_point", 0, _abs);
  _RimeGetIntStr(config, "style/candidate_abbreviate_length",
                 style.candidate_abbreviate_length, 0, 0, _abs);
  _RimeGetBool(config, "style/inline_preedit", initialize,
               style.inline_preedit);
  _RimeGetBool(config, "style/vertical_auto_reverse", initialize,
               style.vertical_auto_reverse);
  static constexpr Array<UIStyle::PreeditType, 3> _preeditArr = {
      {{"composition", UIStyle::COMPOSITION},
       {"preview", UIStyle::PREVIEW},
       {"preview_all", UIStyle::PREVIEW_ALL}}};
  _RimeParseStringOptWithFallback(config, "style/preedit_type",
                                  style.preedit_type, _preeditArr,
                                  style.preedit_type);
  static constexpr Array<UIStyle::AntiAliasMode, 5> _aliasModeArr = {
      {{"force_dword", UIStyle::FORCE_DWORD},
       {"cleartype", UIStyle::CLEARTYPE},
       {"grayscale", UIStyle::GRAYSCALE},
       {"aliased", UIStyle::ALIASED},
       {"default", UIStyle::DEFAULT}}};
  _RimeParseStringOptWithFallback(config, "style/antialias_mode",
                                  style.antialias_mode, _aliasModeArr,
                                  style.antialias_mode);
  static constexpr Array<UIStyle::HoverType, 3> _hoverTypeArr = {
      {{"none", UIStyle::HoverType::NONE},
       {"semi_hilite", UIStyle::HoverType::SEMI_HILITE},
       {"hilite", UIStyle::HoverType::HILITE}}};
  _RimeParseStringOptWithFallback(config, "style/hover_type", style.hover_type,
                                  _hoverTypeArr, style.hover_type);
  static constexpr Array<UIStyle::LayoutAlignType, 3> _alignType = {
      {{"top", UIStyle::ALIGN_TOP},
       {"center", UIStyle::ALIGN_CENTER},
       {"bottom", UIStyle::ALIGN_BOTTOM}}};
  _RimeParseStringOptWithFallback(config, "style/layout/align_type",
                                  style.align_type, _alignType,
                                  style.align_type);
  _RimeGetBool(config, "style/display_tray_icon", initialize,
               style.display_tray_icon);
  _RimeGetBool(config, "style/ascii_tip_follow_cursor", initialize,
               style.ascii_tip_follow_cursor);
  _RimeGetBool(config, "style/horizontal", initialize, style.layout_type,
               UIStyle::LAYOUT_HORIZONTAL, UIStyle::LAYOUT_VERTICAL);
  _RimeGetBool(config, "style/paging_on_scroll", initialize,
               style.paging_on_scroll);
  _RimeGetBool(config, "style/click_to_capture", initialize,
               style.click_to_capture, true, false);
  _RimeGetBool(config, "style/fullscreen", false, style.layout_type,
               ((style.layout_type == UIStyle::LAYOUT_HORIZONTAL)
                    ? UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN
                    : UIStyle::LAYOUT_VERTICAL_FULLSCREEN),
               style.layout_type);
  _RimeGetBool(config, "style/vertical_text", false, style.layout_type,
               UIStyle::LAYOUT_VERTICAL_TEXT, style.layout_type);
  _RimeGetBool(config, "style/vertical_text_left_to_right", false,
               style.vertical_text_left_to_right);
  _RimeGetBool(config, "style/vertical_text_with_wrap", false,
               style.vertical_text_with_wrap);
  static constexpr Array<bool, 2> _text_orientation = {
      {{"horizontal", false}, {"vertical", true}}};
  bool _text_orientation_bool = false;
  _RimeParseStringOptWithFallback(config, "style/text_orientation",
                                  _text_orientation_bool, _text_orientation,
                                  _text_orientation_bool);
  if (_text_orientation_bool)
    style.layout_type = UIStyle::LAYOUT_VERTICAL_TEXT;
  _RimeGetIntStr(config, "style/label_format", style.label_text_format);
  _RimeGetIntStr(config, "style/mark_text", style.mark_text);
  _RimeGetIntStr(config, "style/layout/baseline", style.baseline, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/linespacing", style.linespacing, 0, 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/min_width", style.min_width, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/max_width", style.max_width, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/min_height", style.min_height, 0, 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/max_height", style.max_height, 0, 0,
                 _abs);
  // layout (alternative to style/horizontal)
  static constexpr Array<UIStyle::LayoutType, 5> _layoutArr = {
      {{"vertical", UIStyle::LAYOUT_VERTICAL},
       {"horizontal", UIStyle::LAYOUT_HORIZONTAL},
       {"vertical_text", UIStyle::LAYOUT_VERTICAL_TEXT},
       {"vertical+fullscreen", UIStyle::LAYOUT_VERTICAL_FULLSCREEN},
       {"horizontal+fullscreen", UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN}}};
  _RimeParseStringOptWithFallback(config, "style/layout/type",
                                  style.layout_type, _layoutArr,
                                  style.layout_type);
  // disable max_width when full screen
  if (style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN ||
      style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN) {
    style.max_width = 0;
    style.inline_preedit = false;
  }
  _RimeGetIntStr(config, "style/layout/border", style.border,
                 "style/layout/border_width", 0, _abs);
  _RimeGetIntStr(config, "style/layout/margin_x", style.margin_x);
  _RimeGetIntStr(config, "style/layout/margin_y", style.margin_y);
  _RimeGetIntStr(config, "style/layout/spacing", style.spacing, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/candidate_spacing",
                 style.candidate_spacing, 0, 0, _abs);
  _RimeGetIntStr(config, "style/layout/hilite_spacing", style.hilite_spacing, 0,
                 0, _abs);
  _RimeGetIntStr(config, "style/layout/hilite_padding_x",
                 style.hilite_padding_x, "style/layout/hilite_padding", 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/hilite_padding_y",
                 style.hilite_padding_y, "style/layout/hilite_padding", 0,
                 _abs);
  _RimeGetIntStr(config, "style/layout/shadow_radius", style.shadow_radius, 0,
                 0, _abs);
  // disable shadow for fullscreen layout
  style.shadow_radius *=
      (!(style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN ||
         style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN));
  _RimeGetIntStr(config, "style/layout/shadow_offset_x", style.shadow_offset_x);
  _RimeGetIntStr(config, "style/layout/shadow_offset_y", style.shadow_offset_y);
  // round_corner as alias of hilited_corner_radius
  _RimeGetIntStr(config, "style/layout/hilited_corner_radius",
                 style.round_corner, "style/layout/round_corner", 0, _abs);
  // corner_radius not set, fallback to round_corner
  _RimeGetIntStr(config, "style/layout/corner_radius", style.round_corner_ex,
                 "style/layout/round_corner", 0, _abs);
  // fix padding and spacing settings
  if (style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT) {
    // hilite_padding vs spacing
    // if hilite_padding over spacing, increase spacing
    style.spacing = max(style.spacing, style.hilite_padding_y * 2);
    // hilite_padding vs candidate_spacing
    if (style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN ||
        style.layout_type == UIStyle::LAYOUT_VERTICAL) {
      // vertical, if hilite_padding_y over candidate spacing,
      // increase candidate spacing
      style.candidate_spacing =
          max(style.candidate_spacing, style.hilite_padding_y * 2);
    } else {
      // horizontal, if hilite_padding_x over candidate
      // spacing, increase candidate spacing
      style.candidate_spacing =
          max(style.candidate_spacing, style.hilite_padding_x * 2);
    }
    // hilite_padding_x vs hilite_spacing
    if (!style.inline_preedit)
      style.hilite_spacing = max(style.hilite_spacing, style.hilite_padding_x);
  } else  // LAYOUT_VERTICAL_TEXT
  {
    // hilite_padding_x vs spacing
    // if hilite_padding over spacing, increase spacing
    style.spacing = max(style.spacing, style.hilite_padding_x * 2);
    // hilite_padding vs candidate_spacing
    // if hilite_padding_x over candidate
    // spacing, increase candidate spacing
    style.candidate_spacing =
        max(style.candidate_spacing, style.hilite_padding_x * 2);
    // vertical_text_with_wrap and hilite_padding_y over candidate_spacing
    if (style.vertical_text_with_wrap)
      style.candidate_spacing =
          max(style.candidate_spacing, style.hilite_padding_y * 2);
    // hilite_padding_y vs hilite_spacing
    if (!style.inline_preedit)
      style.hilite_spacing = max(style.hilite_spacing, style.hilite_padding_y);
  }
  // fix padding and margin settings
  int scale = style.margin_x < 0 ? -1 : 1;
  style.margin_x = scale * max(style.hilite_padding_x, abs(style.margin_x));
  scale = style.margin_y < 0 ? -1 : 1;
  style.margin_y = scale * max(style.hilite_padding_y, abs(style.margin_y));
  // get enhanced_position
  _RimeGetBool(config, "style/enhanced_position", initialize,
               style.enhanced_position, true, false);
  // Sogou SSF skin. A relative ssf_skin is resolved by WeaselUI against the Rime
  // user data directory, which is where an extracted .ssf directory belongs.
  // Empty means "use the stock renderer", so this is opt-in and leaves existing
  // configurations untouched.
  // Sogou SSF skin settings are global, so only the config that actually
  // describes the whole UI (initialize == true, i.e. weasel.yaml) is consulted
  // here. _LoadSchemaSpecificSettings() re-applies them from that same global
  // config after it resets the style, because a schema config has no style/ssf_*
  // keys and would otherwise clear the selection.
  if (initialize) _LoadSsfSkinSettings(config, style);
  DebugStream() << L"[SSF] _UpdateUIStyle exit init=" << (initialize ? 1 : 0)
                << L" style.ssf_skin='" << style.ssf_skin
                << L"' enabled=" << (style.ssf_enabled ? 1 : 0) << L"\n";
  // get color scheme
  const int BUF_SIZE = 255;
  char buffer[BUF_SIZE + 1] = {0};
  if (initialize && rime_api->config_get_string(config, "style/color_scheme",
                                                buffer, BUF_SIZE))
    _UpdateUIStyleColor(config, style);
}
// load color configs to style, by "style/color_scheme" or specific scheme name
// "color" which is default empty
static bool _UpdateUIStyleColor(RimeConfig* config,
                                UIStyle& style,
                                const std::string& color) {
  const int BUF_SIZE = 255;
  char buffer[BUF_SIZE + 1] = {0};
  std::string color_mark = "style/color_scheme";
  // color scheme
  if (rime_api->config_get_string(config, color_mark.c_str(), buffer,
                                  BUF_SIZE) ||
      !color.empty()) {
    std::string prefix("preset_color_schemes/");
    prefix += (color.empty()) ? buffer : color;
    // define color format, default abgr if not set
    ColorFormat fmt = COLOR_ABGR;
    static constexpr Array<ColorFormat, 3> _colorFmt = {
        {{"argb", COLOR_ARGB}, {"rgba", COLOR_RGBA}, {"abgr", COLOR_ABGR}}};
    _RimeParseStringOptWithFallback(config, (prefix + "/color_format").c_str(),
                                    fmt, _colorFmt, COLOR_ABGR);
#define COLOR(key, value, fallback) \
  _RimeGetColor(config, (prefix + "/" + key), value, fmt, fallback)
    COLOR("back_color", style.back_color, 0xffffffff);
    COLOR("shadow_color", style.shadow_color, 0);
    COLOR("prevpage_color", style.prevpage_color, 0);
    COLOR("nextpage_color", style.nextpage_color, 0);
    COLOR("text_color", style.text_color, 0xff000000);
    COLOR("candidate_text_color", style.candidate_text_color, style.text_color);
    COLOR("candidate_back_color", style.candidate_back_color, 0);
    COLOR("border_color", style.border_color, style.text_color);
    COLOR("hilited_text_color", style.hilited_text_color, style.text_color);
    COLOR("hilited_back_color", style.hilited_back_color, style.back_color);
    COLOR("hilited_candidate_text_color", style.hilited_candidate_text_color,
          style.hilited_text_color);
    COLOR("hilited_candidate_back_color", style.hilited_candidate_back_color,
          style.hilited_back_color);
    COLOR("hilited_candidate_shadow_color",
          style.hilited_candidate_shadow_color, 0);
    COLOR("hilited_shadow_color", style.hilited_shadow_color, 0);
    COLOR("candidate_shadow_color", style.candidate_shadow_color, 0);
    COLOR("candidate_border_color", style.candidate_border_color, 0);
    COLOR("hilited_candidate_border_color",
          style.hilited_candidate_border_color, 0);
    COLOR("label_color", style.label_text_color,
          blend_colors(style.candidate_text_color, style.candidate_back_color));
    COLOR("hilited_label_color", style.hilited_label_text_color,
          blend_colors(style.hilited_candidate_text_color,
                       style.hilited_candidate_back_color));
    COLOR("comment_text_color", style.comment_text_color,
          style.label_text_color);
    COLOR("hilited_comment_text_color", style.hilited_comment_text_color,
          style.hilited_label_text_color);
    COLOR("hilited_mark_color", style.hilited_mark_color, 0);
#undef COLOR
    return true;
  }
  return false;
}
static void _LoadAppOptions(RimeConfig* config,
                            AppOptionsByAppName& app_options) {
  app_options.clear();
  ForEachRimeMap(
      config, "app_options", [&](const char* app_key, const char* app_path) {
        AppOptions& options(app_options[app_key]);
        ForEachRimeMap(
            config, app_path, [&](const char* opt_key, const char* opt_path) {
              Bool value = False;
              if (rime_api->config_get_bool(config, opt_path, &value)) {
                options[opt_key] = !!value;
              }
            });
      });
}

void RimeWithWeaselHandler::_GetStatus(Status& stat,
                                       WeaselSessionId ipc_id,
                                       Context& ctx) {
  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  RIME_STRUCT(RimeStatus, status);
  if (rime_api->get_status(session_id, &status)) {
    std::string schema_id = "";
    if (status.schema_id)
      schema_id = status.schema_id;
    stat.schema_name = u8tow(status.schema_name);
    stat.schema_id = u8tow(status.schema_id);
    stat.ascii_mode = !!status.is_ascii_mode;
    stat.composing = !!status.is_composing;
    stat.disabled = !!status.is_disabled;
    stat.full_shape = !!status.is_full_shape;
    if (schema_id != m_last_schema_id) {
      session_status.__synced = false;
      m_last_schema_id = schema_id;
      if (schema_id != ".default") {  // don't load for schema select menu
        bool inline_preedit = session_status.style.inline_preedit;
        _LoadSchemaSpecificSettings(ipc_id, schema_id);
        _LoadAppInlinePreeditSet(ipc_id, true);
        if (session_status.style.inline_preedit != inline_preedit)
          // in case of inline_preedit set in schema
          _UpdateInlinePreeditStatus(ipc_id);
        // refresh icon after schema changed
        _RefreshTrayIcon(session_id, _UpdateUICallback);
        m_ui->style() = session_status.style;
        DebugStream() << L"[SSF] WRITE-D (notify): ui->style = session_status "
                         L"-> skin='"
                      << m_ui->style().ssf_skin << L"'\n";
        if (m_show_notifications.find("schema") != m_show_notifications.end() &&
            m_show_notifications_time > 0) {
          ctx.aux.str = stat.schema_name;
          m_ui->Update(ctx, stat);
          m_ui->ShowWithTimeout(m_show_notifications_time);
        }
      }
    }
    rime_api->free_status(&status);
  }
}

void RimeWithWeaselHandler::_GetContext(Context& weasel_context,
                                        RimeSessionId session_id) {
  RIME_STRUCT(RimeContext, ctx);
  if (rime_api->get_context(session_id, &ctx)) {
    if (ctx.composition.length > 0) {
      const std::string keypad_prefix = get_keypad_preedit_prefix(session_id);
      const std::string keypad_suffix = get_keypad_preedit_suffix(session_id);
      const int keypad_cursor =
          get_keypad_preedit_cursor(session_id, keypad_suffix);
      const std::string raw_preedit =
          ctx.composition.preedit ? ctx.composition.preedit : "";
      const std::string display_preedit =
          keypad_prefix + raw_preedit + keypad_suffix;
      weasel_context.preedit.str = u8tow(display_preedit);
      // Carry the insertion point even without a highlighted range. The SSF
      // renderer uses it to draw Sogou-style vertical soft cursor in the
      // pinyin strip.
      TextAttribute attr;
      attr.type = ctx.composition.sel_start < ctx.composition.sel_end
                      ? HIGHLIGHTED
                      : NONE;
      const int prefix_chars =
          utf8towcslen(keypad_prefix.c_str(), (int)keypad_prefix.size());
      attr.range.start = prefix_chars +
          utf8towcslen(raw_preedit.c_str(), ctx.composition.sel_start);
      attr.range.end = prefix_chars +
          utf8towcslen(raw_preedit.c_str(), ctx.composition.sel_end);
      const int composition_cursor =
          (std::max)(0, (std::min)(ctx.composition.cursor_pos,
                                   ctx.composition.length));
      attr.range.cursor = prefix_chars +
          (keypad_suffix.empty() || keypad_cursor < 0
               ? utf8towcslen(raw_preedit.c_str(), composition_cursor)
               : utf8towcslen(raw_preedit.c_str(), ctx.composition.length) +
                     utf8towcslen(keypad_suffix.c_str(),
                                  static_cast<int>(keypad_cursor)));
      const auto prefix_cursor = get_keypad_property(session_id, "weasel_keypad_prefix_cursor");
      if (!prefix_cursor.empty()) attr.range.cursor = utf8towcslen(keypad_prefix.c_str(),
          static_cast<int>((std::min)(keypad_prefix.size(),
              static_cast<size_t>(std::strtoul(prefix_cursor.c_str(), nullptr, 10)))));
      weasel_context.preedit.attributes.push_back(attr);
    }
    if (ctx.menu.num_candidates &&
        get_keypad_property(session_id, "weasel_keypad_literal_only") != "1") {
      CandidateInfo& cinfo(weasel_context.cinfo);
      _GetCandidateInfo(cinfo, ctx);
    }
    rime_api->free_context(&ctx);
  }
}

void RimeWithWeaselHandler::_UpdateInlinePreeditStatus(WeaselSessionId ipc_id) {
  if (!m_ui)
    return;
  SessionStatus& session_status = get_session_status(ipc_id);
  RimeSessionId session_id = session_status.session_id;
  // set inline_preedit option
  bool inline_preedit = session_status.style.inline_preedit;
  rime_api->set_option(session_id, "inline_preedit", Bool(inline_preedit));
  // show soft cursor on weasel panel but not inline
  rime_api->set_option(session_id, "soft_cursor", Bool(!inline_preedit));
}

