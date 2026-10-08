// Minimal MFC-free shim for the identifiers that Weasel's .rc files expect from
// the MFC resource header (afxres.h).
//
// WHY THIS FILE EXISTS
// --------------------
// Visual Studio installations without the optional "C++ MFC for latest v143
// build tools" component do not ship afxres.h, yet WeaselServer.rc and
// WeaselTSF.rc both `#include "afxres.h"`. This shim supplies the same symbols
// so rc.exe can run on a plain Desktop-C++ toolchain.
//
// DO NOT move the definitions below inside an `#ifndef APSTUDIO_READONLY_SYMBOLS`
// guard. The Weasel .rc files do this:
//
//     #define APSTUDIO_READONLY_SYMBOLS      <- line 10
//     #include "afxres.h"                  <- line 15
//
// i.e. they define the guard BEFORE including this header. Anything hidden
// behind that guard is therefore silently dead, which is exactly how the
// original failure presented:
//
//     RC2144: PRIMARY LANGUAGE ID not a number   (on the LANGUAGE statement)
//     RC2135: file not found: 100 / 0xE100 / 0x7468 ...   (cascade)
//
// The real MFC afxres.h gets away with the guard because it `#include <winres.h>`
// outside it and MFC's own .rc files include afxres.h before defining the guard.
// Weasel's do not.
//
// rc.exe substitutes these macros straight into its own grammar, so every value
// must be a *bare* numeric literal -- a parenthesised form such as (-1) is
// rejected. Do not "tidy" them.
//
// This header is intentionally additive; it never redefines anything from
// windows.h / winres.h.

#pragma once

// winres.h pulls in winresrc.h -> dlgs.h -> windows.h, which is what normally
// supplies the LANG_* constants. Keep it: it is harmless when the constants are
// already present and required when they are not.
#include <winres.h>

#ifndef IDC_STATIC
#define IDC_STATIC -1
#endif

// ---------------------------------------------------------------------------
// Language / sublanguage constants used by the LANGUAGE statements, e.g.
//   LANGUAGE LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED
//   LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US
//
// These are standard Windows values (winnt.h). They are re-declared here with
// guards so this is a no-op on toolchains where the include chain already
// provided them.
// ---------------------------------------------------------------------------
#ifndef LANG_CHINESE
#define LANG_CHINESE 0x04
#endif
#ifndef LANG_CHINESE_SIMPLIFIED
#define LANG_CHINESE_SIMPLIFIED 0x04
#endif
#ifndef LANG_CHINESE_TRADITIONAL
#define LANG_CHINESE_TRADITIONAL 0x7c04
#endif
#ifndef SUBLANG_CHINESE_SIMPLIFIED
#define SUBLANG_CHINESE_SIMPLIFIED 0x02
#endif
#ifndef SUBLANG_CHINESE_TRADITIONAL
#define SUBLANG_CHINESE_TRADITIONAL 0x01
#endif
#ifndef LANG_ENGLISH
#define LANG_ENGLISH 0x09
#endif
#ifndef SUBLANG_ENGLISH_US
#define SUBLANG_ENGLISH_US 0x01
#endif

// ---------------------------------------------------------------------------
// MFC-private resource IDs that WeaselServer.rc references but that are not
// defined anywhere in the Weasel or WinSparkle trees. In an official MFC
// toolchain these come from afxres.h.
//
// Their exact numeric values are irrelevant here: WinSparkle resolves the
// update feed by *name*, via
//     GetCustomResource("FeedURL", "APPCAST")
// (winsparkle/src/settings.h), and the other three APPCAST resources are not
// referenced by any code path in this tree. The values only have to be distinct
// so each APPCAST block compiles to its own resource entry.
// ---------------------------------------------------------------------------
#ifndef FEEDURL
#define FEEDURL 0xE100
#endif
#ifndef MANUALUPDATEFEEDURL
#define MANUALUPDATEFEEDURL 0xE101
#endif
#ifndef TESTINGFEEDURL
#define TESTINGFEEDURL 0xE102
#endif
#ifndef TESTINGMANUALUPDATEFEEDURL
#define TESTINGMANUALUPDATEFEEDURL 0xE103
#endif

// Resource-editor placeholder counters. The Visual Studio resource editor emits
// references to these into generated .rc fragments; defining them keeps such
// fragments compilable by rc.exe even without MFC.
#ifndef _APS_NEXT_RESOURCE_VALUE
#define _APS_NEXT_RESOURCE_VALUE 1000
#endif
#ifndef _APS_NEXT_COMMAND_VALUE
#define _APS_NEXT_COMMAND_VALUE 40000
#endif
#ifndef _APS_NEXT_CONTROL_VALUE
#define _APS_NEXT_CONTROL_VALUE 1000
#endif
#ifndef _APS_NEXT_SYMED_VALUE
#define _APS_NEXT_SYMED_VALUE 1000
#endif
