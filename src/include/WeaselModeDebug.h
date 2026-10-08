#pragma once

// Temporary mode-plumbing diagnostics, shared by WeaselTSF and RimeWithWeasel so
// both sides land in one file.
//
// The environment-variable route (WEASEL_DEBUG_LOG) is unreliable here: the
// server is auto-started by whichever host happens to run first, so a variable
// set later in the session never reaches it, and the TSF side runs inside
// explorer.exe with its own launch-time environment.  A fixed path removes that
// uncertainty while the CN/EN and language-bar behaviour is being pinned down.
//
// Remove this header, ModeDbg() and its call sites once those paths are
// confirmed and covered by tests.

#include <windows.h>
#include <string>

inline void ModeDbg(const std::wstring& text) {
  HANDLE h = ::CreateFileW(L"C:\\ProgramData\\ColorPWeasel\\modedbg.log",
                           FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  wchar_t line[1024];
  const int len = swprintf_s<1024>(line, L"[%02d:%02d:%02d.%03d] %ls\r\n",
                                   st.wHour, st.wMinute, st.wSecond,
                                   st.wMilliseconds, text.c_str());
  if (len > 0) {
    DWORD written = 0;
    ::WriteFile(h, line,
                static_cast<DWORD>(static_cast<size_t>(len) * sizeof(wchar_t)),
                &written, nullptr);
  }
  ::CloseHandle(h);
}

inline void ModeDbg(const wchar_t* text) {
  if (text) ModeDbg(std::wstring(text));
}
