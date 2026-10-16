#include "stdafx.h"
#include "WeaselServerApp.h"
#include <filesystem>
#include <WeaselIdleShift.h>

namespace {
RimeWithWeaselHandler* idle_mode_handler = nullptr;
HHOOK idle_keyboard_hook = nullptr;
HHOOK idle_mouse_hook = nullptr;
// 裸 Shift 切换中英文的状态机。判定本身在 WeaselIdleShift.h 里（可单测），
// 这里只负责把事件喂进去并执行它给出的动作。
//
// 注意：键鼠钩子的安装已停用（原因见 Run() 里的说明）。这套回调与状态机保留着，
// 将来若要恢复该功能，重新接线即可。
weasel_idle_shift::State idle_shift;
UINT_PTR idle_shift_timer = 0;

// Drops a queued bare-Shift toggle.  Used both by the keyboard hook (anything
// that disqualifies the gesture) and by the mouse hook (any click means the
// user left the mode-switch gesture).  Without clearing the timer a click could
// still land the queued ToggleIdleMode, so switching away with the mouse and
// clicking somewhere else changed Weasel's CN/EN state afterwards.
void CancelIdleShiftToggle() {
  idle_shift.armed = false;
  idle_shift.timer_pending = false;
  if (idle_shift_timer) {
    KillTimer(nullptr, idle_shift_timer);
    idle_shift_timer = 0;
  }
}

void CALLBACK ApplyIdleShift(HWND, UINT, UINT_PTR timer, DWORD) {
  KillTimer(nullptr, timer);
  idle_shift_timer = 0;
  idle_shift.timer_pending = false;
  if (idle_mode_handler) idle_mode_handler->ToggleIdleMode();
}
LRESULT CALLBACK IdleKeyboard(int code, WPARAM message, LPARAM data) {
  if (code == HC_ACTION && idle_mode_handler) {
    const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    if (!(key.flags & LLKHF_INJECTED)) {
      const bool shift = key.vkCode == VK_LSHIFT || key.vkCode == VK_RSHIFT;
      const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
      using weasel_idle_shift::Event;
      const Event event = !shift      ? Event::kNonShiftKey
                          : down      ? Event::kShiftDown
                                      : Event::kShiftUp;

      // 前提检查只在“准备待命”时求值一次：它要查输入法状态、开销不小。
      bool arm_allowed = false;
      if (event == Event::kShiftDown && !idle_shift.armed) {
        // Only arm while Weasel is the text service the user is typing with.
        // The hook is global, so without this a bare Shift changed Weasel's
        // CN/EN state while another keyboard (for example the US layout) was
        // selected.
        arm_allowed = RimeWithWeaselHandler::WeaselHasForegroundKeyboard() &&
                      idle_mode_handler->CanToggleIdleMode() &&
                      !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                      !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
                      !(GetAsyncKeyState(VK_LWIN) & 0x8000) &&
                      !(GetAsyncKeyState(VK_RWIN) & 0x8000);
      }

      const auto decision =
          weasel_idle_shift::Classify(idle_shift, event, arm_allowed, key.vkCode);
      weasel_idle_shift::Apply(idle_shift, decision, arm_allowed, key.vkCode);

      if (decision.cancel) {
        CancelIdleShiftToggle();
      } else if (decision.schedule) {
        idle_shift_timer = SetTimer(nullptr, 0, 1, ApplyIdleShift);
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
      message != WM_NCMOUSEMOVE) {
    using weasel_idle_shift::Event;
    const auto decision =
        weasel_idle_shift::Classify(idle_shift, Event::kPointerActivity, false);
    weasel_idle_shift::Apply(idle_shift, decision, false);
    CancelIdleShiftToggle();
  }
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

  // 裸 Shift 切换中英文的键鼠钩子已停用。
  //
  // 停用原因（2026-10-13）：诊断日志（1766 行）显示该手势在常见场景下根本不会
  // 生效，而它与时长无关 —— 10 次尝试全部停在同一个判据上：
  //
  //   [ARM-DENY] vk=160 has_kbd=1 can_toggle=0 no_mods=1
  //   [KEY] vk=160 down=1 ... arm_allowed=0 -> arm=1 | armed=0
  //   [KEY] vk=160 down=0 ... sched=0
  //   [SCHEDULE]/[TIMER]/[TOGGLE] 均为 0 次
  //
  // 即 CanToggleIdleMode() 返回假（它要求 m_active_session == 0，也就是「TSF 没有
  // 可编辑会话」），于是从未待命、从未排队切换。用户实测“长按不切换、单击才切换”
  // 的现象与此吻合：能否切换取决于当时的焦点/会话状态，而不是按键时长。
  //
  // 既然该功能的可用性如此有限，就不再为它常驻两个全局低层钩子 —— 省掉每次按键
  // 的钩子开销，也避免一个全局钩子带来的一类风险。状态机（WeaselIdleShift.h）与
  // 其 15 项回归测试保留，将来若要用可以重新接线。
  //
  // 若将来恢复：取消下面两行注释即可，其余逻辑（含 StopMaintenance 路径）仍在。
  // idle_mode_handler = m_handler.get();
  // idle_keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, IdleKeyboard,
  //                                       GetModuleHandle(nullptr), 0);
  // idle_mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, IdleMouse,
  //                                    GetModuleHandle(nullptr), 0);
  (void)&IdleKeyboard;
  (void)&IdleMouse;
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
