#pragma once

// Identity of the Weasel text service, shared by the TSF module (which registers
// it), WeaselServer (which needs to know whether Weasel currently owns the
// keyboard) and the regression tests.
//
// WeaselTSF's Globals.cpp and this header must agree; the assertion below is the
// only thing keeping a silent divergence from turning into "bare Shift stops
// working" or "bare Shift fires under another keyboard".

#include <windows.h>
#include <guiddef.h>

namespace weasel_tsf {

// {A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}
inline const GUID& TextServiceClsid() {
  static const GUID kClsid = {
      0xa3f4cded,
      0xb1e9,
      0x41ee,
      {0x9c, 0xa6, 0x7b, 0x4d, 0xd, 0xe6, 0xcb, 0xa}};
  return kClsid;
}

// {3D02CAB6-2B8E-4781-BA20-1C9267529467}
inline const GUID& ProfileGuid() {
  static const GUID kGuid = {
      0x3d02cab6,
      0x2b8e,
      0x4781,
      {0xba, 0x20, 0x1c, 0x92, 0x67, 0x52, 0x94, 0x67}};
  return kGuid;
}

// True when an active input-processor profile belongs to Weasel.  Pure, so it
// can be regression-tested with literal values.
inline bool ProfileIsWeasel(const GUID& clsid) {
  return IsEqualGUID(clsid, TextServiceClsid()) != FALSE;
}

// True when the profile's layout handle agrees with the layout the foreground
// thread actually has selected.  A profile can be registered for a language
// without being the one in use, so this prunes that case.  A zero hkl means the
// profile does not carry one and is therefore not evidence against.
inline bool ProfileHklMatches(const void* profile_hkl, const void* thread_hkl) {
  if (profile_hkl == nullptr || thread_hkl == nullptr) return true;
  return profile_hkl == thread_hkl;
}

// Compares the language of the layout a foreground thread has selected against
// the language of the active input-processor profile.
//
// This is the check that actually distinguishes "typing with Weasel" from
// "typing with another layout": on the machine this was developed against,
// GetActiveProfile keeps reporting Weasel even after the US layout is selected,
// while the thread's layout moves from 0x08040804 (langid 0x0804) to
// 0x04090409 (langid 0x0409).  Either side being unknown is treated as "no
// evidence against", so a failed lookup does not silently disable the feature.
inline bool LayoutLanguageMatchesProfile(LANGID layout_langid,
                                         LANGID profile_langid) {
  if (layout_langid == 0 || profile_langid == 0) return true;
  return layout_langid == profile_langid;
}

}  // namespace weasel_tsf
