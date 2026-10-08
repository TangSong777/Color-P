// Regression for "the first candidate frame after an input-method switch is
// slow" (reported for Win+Space to the US keyboard, then back).
//
// The bug: leaving Weasel ran WeaselTSF's _DisposeUIWindow() -> UI::Destroy(true),
// which deleted the whole WeaselPanel.  The next composition therefore had to
// re-create GDI+, re-decode the Color-P PNGs and rebuild the DirectWrite
// resources unattended, synchronously, on the user's first keystroke.
//
// The fix, in two cooperating places:
//   * WeaselTSF/CandidateList.cpp _DisposeUIWindow() now calls UI::Destroy(false)
//     -- the no-arg form -- so only the native popup window goes away and
//     everything decoded survives inside the UIImpl.
//   * CCandidateList::Show(TRUE) re-creates just the window when the popup is
//     gone, because Show() used to assume the window still existed.
//
// This program mirrors exactly the UI-level calls the TSF side makes around a
// language switch, and asserts that the surface comes back.  It is the TSF
// helpers themselves that are trusted; the window lifecycle is what is checked.
//
// Run with no arguments for pass/fail, or with -bench to also print the cost of
// the old and new paths.

#include <windows.h>
#include <atlbase.h>
#include <atlwin.h>
#include <wtl/atlapp.h>
#include <WeaselUI.h>
#include <cstdio>
#include <cstring>
#include <string>

CAppModule _Module;

namespace {

const wchar_t* kSkinDir = L"C:\\ProgramData\\ColorPWeasel\\Color-P";

// The owner window CCandidateList would use: the active view window, which does
// not change across a language switch.
HWND g_host = nullptr;

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (condition) {
    std::printf("  ok   %s\n", what);
  } else {
    std::printf("  FAIL %s\n", what);
    ++g_failures;
  }
}

long long NowUs() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return static_cast<long long>(c.QuadPart) * 1000000LL / f.QuadPart;
}

void ConfigureStyle(weasel::UI& ui) {
  auto& s = ui.style();
  s.ssf_enabled = true;
  s.ssf_skin = kSkinDir;
  s.ssf_status_bar = false;
  s.font_face = s.comment_font_face = L"Arial, 宋体";
  s.label_font_face = L"宋体";
  s.font_point = s.label_font_point = s.comment_font_point = 12;
  s.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL;
}

// What CCandidateList::UpdateUI does for a composing response, plus the
// Show(TRUE) that follows it.
void ComposingFrame(weasel::UI& ui, const wchar_t* preedit) {
  weasel::Status status;
  status.composing = true;
  weasel::Context ctx;
  ctx.preedit.str = preedit;
  ctx.cinfo.candies.emplace_back(L"你好");
  ctx.cinfo.comments.emplace_back(L"");
  ctx.cinfo.labels.emplace_back(L"1");
  ui.style().inline_preedit = false;
  ui.Update(ctx, status);
  RECT pos = {400, 300, 401, 320};
  ui.UpdateInputPosition(pos);
  // CCandidateList::Show(TRUE): re-create the window first if the panel was
  // disposed but its decoded resources were kept.
  if (ui.panel_hwnd() == nullptr) ui.Create(g_host);
  ui.Show();
}

// A frame with no candidate content: CCandidateList::UpdateUI takes its
// "nothing to present" branch and the panel's Refresh() takes its
// SuppressEmptyWindow branch, which returns before _RepositionWindow() runs.
void EmptyFrame(weasel::UI& ui) {
  weasel::Status status;
  status.composing = false;
  ui.Update(weasel::Context(), status);
}

// The exact order CCandidateList::UpdateUI uses for a composing response:
// Update() first, then UpdateInputPosition(), then Show().  Splitting it lets a
// caller create the window in between, which is what a fresh window after a
// switch does.
void ComposingUpdate(weasel::UI& ui, const wchar_t* preedit) {
  weasel::Status status;
  status.composing = true;
  weasel::Context ctx;
  ctx.preedit.str = preedit;
  ctx.cinfo.candies.emplace_back(L"你好");
  ctx.cinfo.comments.emplace_back(L"");
  ctx.cinfo.labels.emplace_back(L"1");
  ui.style().inline_preedit = false;
  ui.Update(ctx, status);
}

void ComposingPosition(weasel::UI& ui) {
  RECT pos = {400, 300, 401, 320};
  ui.UpdateInputPosition(pos);
}

// Screen rectangle of the popup, or false when there is no window.
bool PopupRect(weasel::UI& ui, RECT* out) {
  HWND h = ui.panel_hwnd();
  if (!h) return false;
  return ::GetWindowRect(h, out) != FALSE;
}

}  // namespace

int main(int argc, char** argv) {
  bool bench = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-bench") == 0) bench = true;
  }

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  _Module.Init(nullptr, GetModuleHandle(nullptr));

  g_host = CreateWindowExW(0, L"STATIC", L"panel recreate regression",
                           WS_POPUP, 0, 0, 800, 400, nullptr, nullptr,
                           GetModuleHandle(nullptr), nullptr);
  if (!g_host) {
    std::printf("host window creation failed\n");
    return 2;
  }

  std::printf("candidate panel survives an input-method switch:\n");

  weasel::UI ui;
  ConfigureStyle(ui);
  Check(ui.Create(g_host), "UI::Create builds the candidate panel");
  Check(ui.panel_hwnd() != nullptr, "the native popup exists after Create");

  ComposingFrame(ui, L"n");
  Check(ui.IsShown(), "first composition shows the candidate window");

  // --- switch away (Win+Space): hide, then dispose only the window ---
  ui.Hide();
  ui.Destroy(false);
  Check(!ui.IsShown(), "after switching away the window is not shown");
  Check(ui.panel_hwnd() == nullptr,
        "the popup window itself is gone, so nothing is left on screen");

  // CCandidateList::Destroy() is reached twice for one composition in the
  // ordinary focus-change sequence.  The second call must be a no-op: disposing
  // an already disposed window used to re-arm the "needs re-creating" flag and
  // make the popup get rebuilt and torn down on every composition.
  ui.Destroy(false);
  Check(ui.panel_hwnd() == nullptr && !ui.IsShown(),
        "a repeated dispose is a no-op, not a rebuild");

  // --- switch back and type the first letter ---
  const long long t0 = NowUs();
  if (ui.panel_hwnd() == nullptr) ui.Create(g_host);
  const long long tCreate = NowUs() - t0;
  ComposingFrame(ui, L"ni");

  const HWND popup = ui.panel_hwnd();
  Check(popup != nullptr && IsWindow(popup),
        "switching back re-creates the native popup");
  Check(ui.IsShown(), "the candidate window shows again");
  Check(popup != nullptr && IsWindowVisible(popup),
        "the re-created popup is actually visible");
  Check(!ui.ctx().cinfo.candies.empty(),
        "candidate data is intact after the switch");
  Check(ui.style().ssf_enabled, "the SSF style survived the switch");
  std::printf("  [info] window re-create after a switch: %.1f ms\n",
              tCreate / 1000.0);

  // --- no flash at the top-left corner after a switch ---
  //
  // The reported symptom was a brief candidate window at the screen origin
  // before it settled at the caret.  The panel derives its position from
  // m_inputPos, which starts as an empty CRect(), so any Show() that happens
  // before a real caret anchor arrives puts the window at (0,0).
  {
    RECT r = {};
    const bool have = PopupRect(ui, &r);
    std::printf("  [info] popup rect now: (%ld,%ld)\n", r.left, r.top);
    Check(have && !(r.left == 0 && r.top == 0),
          "the visible popup is not parked at the screen origin");

    // Re-do the switch with a genuinely fresh UI object, which is what a
    // rebuilt window after a switch looks like: m_inputPos starts as an empty
    // CRect().  CCandidateList::_MakeUIWindow() now re-applies the remembered
    // caret anchor right after creating the window, so the window must never be
    // observed at the origin.
    ui.Hide();
    ui.Destroy(false);
    {
      weasel::UI fresh;
      ConfigureStyle(fresh);
      fresh.Create(g_host);
      // The realistic ordering, and the one the earlier version of this check
      // missed: WeaselPanel::OnCreate() -> Refresh() -> _RepositionWindow() runs
      // while m_inputPos is still the default CRect().  The panel therefore sits
      // at the screen origin, and _RepositionWindow() refuses to move it because
      // there is no anchor yet.  Sitting there is harmless as long as the window
      // is never made visible in that state, which is what used to produce the
      // top-left candidate flash after switching input methods.
      RECT before = {};
      const bool have_before = PopupRect(fresh, &before);
      const HWND fresh_popup = fresh.panel_hwnd();
      std::printf("  [info] fresh popup right after Create: (%ld,%ld) visible=%d\n",
                  before.left, before.top,
                  fresh_popup && IsWindowVisible(fresh_popup) ? 1 : 0);
      Check(!fresh_popup || !IsWindowVisible(fresh_popup),
            "an unanchored popup is never visible, whatever position it holds");

      // This is the state Show(TRUE) would run in before the caret arrives.
      fresh.Show();
      Check(!fresh_popup || !IsWindowVisible(fresh_popup),
            "showing before a caret arrives keeps the popup hidden");

      // Only now does the caret position arrive, as UpdateInputPosition does.
      ComposingUpdate(fresh, L"ni");
      ComposingPosition(fresh);
      RECT placed = {};
      const bool have_placed = PopupRect(fresh, &placed);
      std::printf("  [info] fresh popup after the caret arrives: (%ld,%ld)\n",
                  placed.left, placed.top);
      Check(!have_placed || !(placed.left == 0 && placed.top == 0),
            "once the caret arrives the popup moves off the origin");
      fresh.Show();
      RECT after = {};
      Check(PopupRect(fresh, &after) && !(after.left == 0 && after.top == 0),
            "the freshly created popup is anchored away from the origin once shown");
      fresh.Destroy(true);
    }
    ComposingFrame(ui, L"ni");
  }

  if (bench) {
    const int kCycles = 12;
    long long keepSum = 0, rebuildSum = 0;

    // New behaviour: keep everything decoded, re-create only the window.
    for (int c = 0; c < kCycles; ++c) {
      ui.Hide();
      ui.Destroy(false);
      const long long s = NowUs();
      if (ui.panel_hwnd() == nullptr) ui.Create(g_host);
      ComposingFrame(ui, L"n");
      keepSum += NowUs() - s;
    }

    // Old behaviour: drop everything decoded and rebuild it.
    for (int c = 0; c < kCycles; ++c) {
      ui.Destroy(true);
      const long long s = NowUs();
      ui.Create(g_host);
      ComposingFrame(ui, L"n");
      rebuildSum += NowUs() - s;
    }

    std::printf("\nfirst visible frame after a switch (avg of %d):\n", kCycles);
    std::printf("  old path (destroy(true) + rebuild) : %.1f ms\n",
                static_cast<double>(rebuildSum) / kCycles / 1000.0);
    std::printf("  new path (keep decoded)            : %.1f ms\n",
                static_cast<double>(keepSum) / kCycles / 1000.0);
  }

  ui.Destroy(true);
  DestroyWindow(g_host);
  _Module.Term();
  CoUninitialize();

  std::printf("\n%s\n", g_failures == 0 ? "PASS" : "FAIL");
  return g_failures == 0 ? 0 : 1;
}
