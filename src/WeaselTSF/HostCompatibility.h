#pragma once
#include <windows.h>
namespace weasel_host {
// Shell icon views accept type-to-select but are not text editors. An Edit
// child (F2 rename) must remain eligible even under the same desktop ancestry.
inline bool IsDesktopNonTextWindow(HWND focus) {
  bool desktop = false;
  for (HWND w = focus; w; w = GetParent(w)) {
    wchar_t name[128] = {};
    GetClassNameW(w, name, 128);
    if (_wcsicmp(name, L"Edit") == 0 ||
        _wcsnicmp(name, L"RichEdit", 8) == 0) return false;
    if (_wcsicmp(name, L"Progman") == 0 ||
        _wcsicmp(name, L"WorkerW") == 0) desktop = true;
  }
  return desktop;
}
inline bool DesktopHasNoTextFocus() {
  HWND foreground = GetForegroundWindow();
  GUITHREADINFO info = {sizeof(GUITHREADINFO)};
  DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
  if (!thread || !GetGUIThreadInfo(thread, &info)) return false;
  return IsDesktopNonTextWindow(info.hwndFocus ? info.hwndFocus : foreground);
}
// TSF callbacks can arrive through different window messages. Compare physical
// identity, not the dispatch timestamp or repeat-count metadata.
inline bool SamePendingKey(WPARAM tested, LPARAM flags, const void* context,
                           WPARAM key, LPARAM incoming, const void* current) {
  constexpr LPARAM identity = 0x01ff0000; // scan code and extended-key flag
  return tested == key && context == current &&
         (flags & identity) == (incoming & identity);
}
inline bool PlaceholderExtent(const RECT& caret, const RECT& view) {
  return view.right - view.left > 200 && view.bottom - view.top > 100 &&
         caret.left >= view.left && caret.left <= view.left + 1 &&
         caret.top >= view.top && caret.top <= view.top + 1 &&
         caret.right - caret.left <= 2;
}
inline RECT FallbackAnchor(const RECT& view) {
  const LONG x = view.left + 16;
  const LONG y = view.bottom - 80;
  return {x, y, x + 1, y + 20};
}
}
