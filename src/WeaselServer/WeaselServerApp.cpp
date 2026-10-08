#include "stdafx.h"
#include "WeaselServerApp.h"
#include <filesystem>

namespace {
RimeWithWeaselHandler* idle_mode_handler = nullptr;
HHOOK idle_keyboard_hook = nullptr;
HHOOK idle_mouse_hook = nullptr;
bool idle_shift_armed = false;
DWORD idle_shift_key = 0;
UINT_PTR idle_shift_timer = 0;

// Drops a queued bare-Shift toggle.  Used both by the keyboard hook (anything
// that disqualifies the gesture) and by the mouse hook (any click means the
// user left the mode-switch gesture).  Without clearing the timer a click could
// still land the queued ToggleIdleMode, so switching away with the mouse and
// clicking somewhere else changed Weasel's CN/EN state afterwards.
void CancelIdleShiftToggle() {
  idle_shift_armed = false;
  if (idle_shift_timer) {
    KillTimer(nullptr, idle_shift_timer);
    idle_shift_timer = 0;
  }
}

void CALLBACK ApplyIdleShift(HWND, UINT, UINT_PTR timer, DWORD) {
  KillTimer(nullptr, timer);
  idle_shift_timer = 0;
  if (idle_mode_handler) idle_mode_handler->ToggleIdleMode();
}
LRESULT CALLBACK IdleKeyboard(int code, WPARAM message, LPARAM data) {
  if (code == HC_ACTION && idle_mode_handler) {
    const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    if (!(key.flags & LLKHF_INJECTED)) {
      const bool shift = key.vkCode == VK_LSHIFT || key.vkCode == VK_RSHIFT;
      const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
      if (!shift) {
        CancelIdleShiftToggle();
      } else if (down) {
        if (!idle_shift_armed) {
          // Only arm while Weasel is the text service the user is typing with.
          // The hook is global, so without this a bare Shift changed Weasel's
          // CN/EN state while another keyboard (for example the US layout) was
          // selected.
          idle_shift_armed =
              RimeWithWeaselHandler::WeaselHasForegroundKeyboard() &&
              idle_mode_handler->CanToggleIdleMode() &&
              !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
              !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
              !(GetAsyncKeyState(VK_LWIN) & 0x8000) &&
              !(GetAsyncKeyState(VK_RWIN) & 0x8000);
          idle_shift_key = key.vkCode;
        }
      } else {
        if (idle_shift_armed && key.vkCode == idle_shift_key && !idle_shift_timer)
          idle_shift_timer = SetTimer(nullptr, 0, 1, ApplyIdleShift);
        idle_shift_armed = false;
      }
    }
  }
  return CallNextHookEx(idle_keyboard_hook, code, message, data);
}
LRESULT CALLBACK IdleMouse(int code, WPARAM message, LPARAM data) {
  // A click or scroll means the user is doing something else, so the bare-Shift
  // gesture is over: drop both the armed flag and any queued toggle.  Keyboard
  // shortcuts still arrive as key events and are filtered there.
  if (code == HC_ACTION && message != WM_MOUSEMOVE &&
      message != WM_NCMOUSEMOVE)
    CancelIdleShiftToggle();
  return CallNextHookEx(idle_mouse_hook, code, message, data);
}
}  // namespace

WeaselServerApp::WeaselServerApp()
    : m_handler(std::make_unique<RimeWithWeaselHandler>(&m_ui)),
      tray_icon(m_ui) {
  // m_handler.reset(new RimeWithWeaselHandler(&m_ui));
  m_server.SetRequestHandler(m_handler.get());
  SetupMenuHandlers();
}

WeaselServerApp::~WeaselServerApp() {}

int WeaselServerApp::Run() {
  // Initialise Rime before making the IPC endpoint visible.  Previously a
  // just-launched server accepted a TSF session while its engine and skin were
  // still loading, so the first input after sign-in could race an incomplete
  // service.  Starting the listener only after this work makes "connected"
  // mean that a candidate request can be handled immediately.
  m_handler->Initialize();
  if (!m_server.Start()) {
    m_handler->Finalize();
    return -1;
  }

  // win_sparkle_set_appcast_url("http://localhost:8000/weasel/update/appcast.xml");
  win_sparkle_set_registry_path("Software\\Rime\\Weasel\\Updates");
  if (GetThreadUILanguage() ==
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL))
    win_sparkle_set_lang("zh-TW");
  else if (GetThreadUILanguage() ==
           MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
    win_sparkle_set_lang("zh-CN");
  else
    win_sparkle_set_lang("en");
  win_sparkle_init();
  // The handler has already populated the UI style, so panel construction now
  // preloads the selected SSF artwork and DirectWrite formats instead of first
  // building a default panel and repairing it on the user's first key.
  DebugStream() << L"[SSF] Run: before m_ui.Create skin='"
                << m_ui.style().ssf_skin << L"'\n";
  m_ui.Create(m_server.GetHWnd());
  DebugStream() << L"[SSF] Run: after m_ui.Create skin='"
                << m_ui.style().ssf_skin << L"'\n";

  // Build an empty layout now, off the input path.  Skin images and font
  // formats were already warmed by the panel constructor; this finishes the
  // remaining layout allocation before the first composition arrives.
  m_ui.Refresh();
  DebugStream() << L"[SSF] Run: after m_ui.Refresh skin='"
                << m_ui.style().ssf_skin << L"'\n";

  m_handler->OnUpdateUI([this]() { tray_icon.RequestRefresh(); });

  tray_icon.Create(m_server.GetHWnd());
  m_server.SetTrayRefreshCallback([this]() { tray_icon.ApplyRefresh(); });
  tray_icon.RequestRefresh();

  // Only handle bare Shift when TSF has no editable session. Hooks never eat
  // events or retain input text; actual Rime work is deferred to our loop.
  idle_mode_handler = m_handler.get();
  idle_keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, IdleKeyboard,
                                        GetModuleHandle(nullptr), 0);
  idle_mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, IdleMouse,
                                     GetModuleHandle(nullptr), 0);
  int ret = m_server.Run();
  if (idle_keyboard_hook) UnhookWindowsHookEx(idle_keyboard_hook);
  if (idle_mouse_hook) UnhookWindowsHookEx(idle_mouse_hook);
  if (idle_shift_timer) KillTimer(nullptr, idle_shift_timer);
  idle_mode_handler = nullptr;

  tray_icon.DisableRefresh();
  m_handler->Finalize();
  m_ui.Destroy();
  tray_icon.RemoveIcon();
  win_sparkle_cleanup();

  return ret;
}

void WeaselServerApp::SetupMenuHandlers() {
  std::filesystem::path dir = install_dir();
  m_server.AddMenuHandler(ID_WEASELTRAY_QUIT,
                          [this] { return m_server.Stop() == 0; });
  m_server.AddMenuHandler(ID_WEASELTRAY_DEPLOY,
                          std::bind(execute, dir / L"WeaselDeployer.exe",
                                    std::wstring(L"/deploy")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SETTINGS,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring()));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_DICT_MANAGEMENT,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/dict")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SYNC,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/sync")));
  m_server.AddMenuHandler(ID_WEASELTRAY_WIKI,
                          std::bind(open, L"https://rime.im/docs/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_HOMEPAGE,
                          std::bind(open, L"https://rime.im/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_FORUM,
                          std::bind(open, L"https://rime.im/discuss/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_CHECKUPDATE, check_update);
  m_server.AddMenuHandler(ID_WEASELTRAY_INSTALLDIR, std::bind(explore, dir));
  m_server.AddMenuHandler(ID_WEASELTRAY_USERCONFIG,
                          std::bind(explore, WeaselUserDataPath()));
  m_server.AddMenuHandler(ID_WEASELTRAY_LOGDIR,
                          std::bind(explore, WeaselLogPath()));
}
