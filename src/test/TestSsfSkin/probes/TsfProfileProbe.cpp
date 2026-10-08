// Read-only probe: what does TSF report as the active keyboard profile?
//
// This is the pivot of the bare-Shift fix: WeaselServer refuses to toggle the
// CN/EN mode unless the active profile is Weasel's.  If GetActiveProfile never
// returned Weasel's CLSID the toggle would simply stop working, so it is worth
// being able to look at the real value.
//
// Usage: tsf-profile-probe.exe [repeat] [interval_ms]
// Prints one line per sample.  It never changes the active input method.

#include <windows.h>
#include <atlbase.h>
#include <msctf.h>
#include <cstdio>
#include <cstdlib>
#include "WeaselTsfIdentity.h"

static const char* GuidName(const GUID& g) {
  if (weasel_tsf::ProfileIsWeasel(g)) return "WEASEL";
  if (IsEqualGUID(g, GUID_NULL)) return "<null>";
  return "other";
}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("probe start\n");
  std::fflush(stdout);

  const int repeat = argc > 1 ? std::atoi(argv[1]) : 5;
  const int interval = argc > 2 ? std::atoi(argv[2]) : 1000;

  const HRESULT co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  std::printf("CoInitializeEx -> 0x%08lX\n", static_cast<unsigned long>(co));

  std::printf("identity check: ProfileIsWeasel(self)=%d\n",
              weasel_tsf::ProfileIsWeasel(weasel_tsf::TextServiceClsid()) ? 1 : 0);

  CComPtr<ITfInputProcessorProfileMgr> mgr;
  std::printf("about to CoCreateInstance\n");
  const HRESULT hr = mgr.CoCreateInstance(CLSID_TF_InputProcessorProfiles,
                                          nullptr, CLSCTX_ALL);
  std::printf("CoCreateInstance(CLSID_TF_InputProcessorProfiles) -> 0x%08lX\n",
              static_cast<unsigned long>(hr));
  if (FAILED(hr)) {
    ::CoUninitialize();
    return 1;
  }

  for (int i = 0; i < repeat; ++i) {
    HWND foreground = ::GetForegroundWindow();
    const DWORD thread =
        foreground ? ::GetWindowThreadProcessId(foreground, nullptr) : 0;
    const HKL hkl = thread ? ::GetKeyboardLayout(thread) : nullptr;

    TF_INPUTPROCESSORPROFILE profile = {};
    const HRESULT g = mgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile);
    if (FAILED(g)) {
      std::printf("[%d] GetActiveProfile failed 0x%08lX\n", i,
                  static_cast<unsigned long>(g));
    } else {
      wchar_t clsid[64] = {};
      ::StringFromGUID2(profile.clsid, clsid, 64);
      std::printf("[%d] active clsid=%ls (%s) langid=0x%04X hkl=%p thread_hkl=%p weasel=%d\n",
                  i, clsid, GuidName(profile.clsid),
                  static_cast<unsigned>(profile.langid),
                  reinterpret_cast<void*>(profile.hkl),
                  reinterpret_cast<void*>(hkl),
                  (weasel_tsf::ProfileIsWeasel(profile.clsid) &&
                   weasel_tsf::ProfileHklMatches(profile.hkl, hkl))
                      ? 1
                      : 0);
    }
    std::fflush(stdout);
    if (i + 1 < repeat) ::Sleep(interval);
  }

  // Release the COM object before CoUninitialize, then leave without running
  // static destructors: the console is redirected from a short-lived process and
  // a teardown-time fault would mask the exit code.
  mgr.Release();
  ::CoUninitialize();
  std::printf("done\n");
  std::fflush(stdout);
  ::ExitProcess(0);
}
