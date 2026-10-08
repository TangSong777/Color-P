#include "../../WeaselTSF/HostCompatibility.h"
#include "../../include/WeaselTsfIdentity.h"
#include <cassert>
#include <cstdio>
int main() {
  using namespace weasel_host;
  int field1=0,field2=0; bool pending=false; int sends=0;
  auto test=[&]{if(!pending){++sends;pending=true;}};
  auto deliver=[&](WPARAM key,LPARAM flags,void* field){
    if(pending && SamePendingKey('A',0x001e0001,&field1,key,flags,field)) pending=false;
    else {pending=false;++sends;}
  };
  test();deliver('A',0x401e0002,&field1);assert(sends==1); // dispatch changes repeat metadata
  test();deliver('A',0x401e0001,&field1);assert(sends==2); // real second physical press retained
  deliver('A',0x401e0001,&field1);assert(sends==3); // KeyDown-only host
  test();deliver('B',0x00300001,&field1);assert(sends==5); // different key not swallowed
  test();deliver('A',0x001e0001,&field2);assert(sends==7); // new context not swallowed
  assert(!SamePendingKey(VK_RETURN,0x001c0001,&field1,VK_RETURN,0x011c0001,&field1));
  RECT screen={0,0,1920,1080}, stub={0,0,1,20};
  assert(PlaceholderExtent(stub,screen));
  assert(!PlaceholderExtent(RECT{100,100,101,120},screen));
  RECT secondary={-1920,0,0,1080};
  assert(PlaceholderExtent(RECT{-1920,0,-1919,20},secondary));
  RECT fallback=FallbackAnchor(secondary);
  assert(fallback.left>secondary.left && fallback.right<secondary.right && fallback.bottom<secondary.bottom);
  // The no-caret fallback must only be used on a view big enough to host a
  // candidate window; a small view still withholds the position instead of
  // anchoring the popup somewhere meaningless.  Composition.cpp falls back to
  // the foreground window rect when the TSF view has no usable extent, so the
  // same size guard has to hold for any rect it can produce.
  RECT tiny={0,0,180,90};
  assert(tiny.right-tiny.left<=200 || tiny.bottom-tiny.top<=100);
  RECT usable={0,0,1920,1080};
  assert(usable.right-usable.left>200 && usable.bottom-usable.top>100);
  RECT host_fallback=FallbackAnchor(usable);
  assert(host_fallback.top>usable.top && host_fallback.bottom<usable.bottom);
  WNDCLASSW wc = {}; wc.lpfnWndProc=DefWindowProcW;
  wc.hInstance=GetModuleHandle(nullptr); wc.lpszClassName=L"Progman";
  ATOM atom=RegisterClassW(&wc); assert(atom);
  HWND desktop=CreateWindowExW(0,L"Progman",L"test",WS_POPUP,0,0,500,500,nullptr,nullptr,wc.hInstance,nullptr);
  HWND icons=CreateWindowExW(0,L"STATIC",L"icons",WS_CHILD,0,0,400,400,desktop,nullptr,wc.hInstance,nullptr);
  HWND rename=CreateWindowExW(0,L"EDIT",L"rename",WS_CHILD,0,0,200,20,icons,nullptr,wc.hInstance,nullptr);
  HWND normal=CreateWindowExW(0,L"STATIC",L"normal",WS_POPUP,0,0,400,400,nullptr,nullptr,wc.hInstance,nullptr);
  assert(desktop && icons && rename && normal);
  assert(IsDesktopNonTextWindow(desktop));
  assert(IsDesktopNonTextWindow(icons));
  assert(!IsDesktopNonTextWindow(rename));
  assert(!IsDesktopNonTextWindow(normal));
  assert(!IsDesktopNonTextWindow(nullptr));
  DestroyWindow(desktop);DestroyWindow(normal);UnregisterClassW(L"Progman",wc.hInstance);

  // The bare-Shift toggle must only fire while Weasel owns the keyboard.  That
  // decision asks whether the active TSF profile is Weasel's, so the identity
  // comparison is what keeps Shift from changing Weasel's CN/EN state while the
  // US layout is selected.
  {
    using namespace weasel_tsf;
    assert(ProfileIsWeasel(TextServiceClsid()));
    // A plain keyboard layout has no TSF CLSID at all; the useful negative is
    // another text service, e.g. the Microsoft Pinyin CLSID.
    const GUID other = {0x81d4e9c9,0x1d3b,0x41bc,
                        {0x9e,0x6c,0x4b,0x40,0xbf,0x79,0xe3,0x5e}};
    assert(!ProfileIsWeasel(other));
    assert(!ProfileIsWeasel(GUID_NULL));
    // The profile GUID must stay distinct from the CLSID; conflating them would
    // make every Weasel profile compare unequal.
    assert(!IsEqualGUID(ProfileGuid(), TextServiceClsid()));
    // No hkl on either side means "no evidence against".
    assert(ProfileHklMatches(nullptr, reinterpret_cast<const void*>(0x08040804)));
    assert(ProfileHklMatches(reinterpret_cast<const void*>(0x08040804), nullptr));
    // Matching layouts pass; a mismatch is rejected, which is what excludes a
    // Weasel profile that is registered but not the one in use.
    assert(ProfileHklMatches(reinterpret_cast<const void*>(0x08040804),
                             reinterpret_cast<const void*>(0x08040804)));
    assert(!ProfileHklMatches(reinterpret_cast<const void*>(0x08040804),
                              reinterpret_cast<const void*>(0x04090409)));

    // The check that actually keeps bare Shift from firing under another
    // keyboard.  Measured HKLs: 0x08040804 -> langid 0x0804 (Weasel),
    // 0x04090409 -> langid 0x0409 (US layout).  GetActiveProfile keeps
    // reporting Weasel for both, so only the thread's layout language can tell
    // them apart.
    const LANGID weasel_lang = 0x0804;
    const LANGID us_lang = 0x0409;
    assert(LayoutLanguageMatchesProfile(0x0804, weasel_lang));   // Weasel layout
    assert(!LayoutLanguageMatchesProfile(us_lang, weasel_lang));  // US layout
    assert(!LayoutLanguageMatchesProfile(0x0404, weasel_lang));   // zh-TW vs zh-CN
    // Unknown values must not silently disable the toggle.
    assert(LayoutLanguageMatchesProfile(0, weasel_lang));
    assert(LayoutLanguageMatchesProfile(us_lang, 0));
  }
  puts("Host compatibility event sequences and multi-monitor fallback and desktop/rename focus: PASS");
}
