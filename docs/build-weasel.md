# Building Weasel from this tree

This records how to reproduce the binaries in `dist\` on this machine. Weasel's
own `build.bat` is **not** used, because both of the things it insists on
building from source are impractical here. Read the two "why" sections before
changing anything — they represent several hours of dead ends.

Target environment:

| Component | Version / path |
|---|---|
| Visual Studio | Community 2022 17.14, `D:\VisualStudio2022` |
| MSVC | 14.44.35207, toolset `v143` |
| Windows SDK | 10.0.26100.0, `D:\Windows Kits\10` |
| Boost | 1.82, source at `D:\boost182` |
| librime | 1.13.1 (headers from the `1.13.1` tag; runtime reused from the installed Weasel) |

---

## 1. Why not b2 for Boost

Boost 1.82's msvc toolset derives its setup script from

```
<MSVC>/bin/Hostx64/vcvarsall.bat
```

which **does not exist** in a Visual Studio 2022 layout — the real file is in
`VC\Auxiliary\Build`. When that probe fails, b2 never generates the
`msvc-setup.nup` node, and every compile and archive target is reported as

```
...skipped <p...>foo.obj for lack of <p...>msvc-setup.nup
```

b2 then prints `...updated N targets` and produces **zero `.lib` files**, with no
error. All of the following were tried and **did not change this behaviour**:

* passing `<setup>` in `user-config.jam` pointing at the real `vcvarsall.bat`
* registering the toolset under a distinct version label (`14.3vs2022`) so it
  could not be shadowed by the built-in auto-detection
* supplying explicit `<linker>`, `<rc>`, `<mt>`, `<archiver>` paths
* calling `bootstrap.bat` from inside a `vcvars64.bat` environment
* building at an ASCII-only path (the checkout path contains non-ASCII
  characters, which was a real secondary problem worth ruling out)

Instead, `build-boost-libs.ps1` drives `cl.exe` and `lib.exe` directly. It
compiles only the five libraries Weasel links:

| Library | Why it is needed |
|---|---|
| `thread` | `boost::thread`, `boost::thread_specific_ptr` — WeaselIPC, WeaselIPCServer |
| `serialization` | the IPC archive types in `WeaselIPCData.h` |
| `wserialization` | wide-char archives (`text_woarchive`), which MSVC auto-links |
| `system` | `boost::system::error_code`, a dependency of `thread` |
| `chrono` | a hard dependency of `boost::thread` |

Output names match MSVC's auto-link pragma exactly:
`libboost_<name>-vc143-mt-s-<arch>-1_82.lib`, i.e. **static runtime (`/MT`)**,
matching Weasel's `<RuntimeLibrary>MultiThreaded</RuntimeLibrary>`. Getting this
wrong produces `LNK1104: cannot open file 'libboost_...-mt-s-...'`.

```powershell
pwsh -File build-boost-libs.ps1             # x64 -> D:\boost182\stage\lib
pwsh -File build-boost-libs.ps1 -Arch x86   # x86 -> D:\boost182\stage32\lib
```

> Note: the Anaconda Boost in `D:\Anaconda\Library` is **not** usable — it is
> built against the shared runtime (`/MD`), so the auto-link pragma looks for
> `-mt-` libraries and fails.

## 2. Why not build librime

Building librime needs CMake, its own Boost, and a full dependency build. Weasel
only needs `rime_api.h`, `rime_levers_api.h` and an import library, so:

* **headers** — copied from a checkout of `rime/librime` at tag `1.13.1`, which
  is what the installed Weasel runs against. Using a different librime version's
  headers would be an ABI gamble, so the tag matters.
* **`rime.lib`** — synthesised from the matching `rime.dll`:

  ```powershell
  dumpbin /exports rime.dll > exports.txt
  # build a .def listing every exported name, then:
  lib /def:rime.def /machine:x64 /out:rime_x64.lib
  ```

  The x64 `rime.dll` comes from the installed Weasel
  (`D:\Rime\weasel-0.17.4\rime.dll`). The x86 one comes from the official
  0.17.4 installer, which is an NSIS self-extracting archive and can be unpacked
  with `7z x`. Both export 723 symbols, which is a useful cross-check.

  Use the matching-architecture `dumpbin`/`lib` (the `Hostx64\x64` and
  `Hostx64\x86` directories) or the resulting library will have the wrong
  machine type.

## 3. The `afxres.h` shim

`WeaselServer.rc` and `WeaselTSF.rc` both `#include "afxres.h"`, which ships only
with the optional **"C++ MFC for latest v143 build tools"** VS component. That
component is not installed here (and installing it needs elevation), so
`build-support/afxres.h` supplies the handful of symbols they need.

Three details in that file are load-bearing. All three were found by debugging a
build failure, not by reading documentation:

1. **The definitions must sit outside `#ifndef APSTUDIO_READONLY_SYMBOLS`.**
   The Weasel `.rc` files do this:

   ```
   #define APSTUDIO_READONLY_SYMBOLS     <- line 10
   #include "afxres.h"                 <- line 15
   ```

   i.e. they define the guard *before* including the header, so anything hidden
   behind it is dead. The symptom is
   `RC2144: PRIMARY LANGUAGE ID not a number` followed by a cascade of bogus
   `RC2135: file not found: 100 / FEEDURL / 0x7468` errors.

2. **`#include <winres.h>` must stay.** The real MFC header begins with it, and
   it is what pulls in the `LANG_*` / `SUBLANG_*` constants. This toolchain does
   not reliably deliver them to `rc.exe`, so they are also spelled out explicitly
   with `#ifndef` guards — a no-op wherever `winnt.h` already provided them.

3. **Every value must be a bare numeric literal.** `rc.exe` substitutes these
   macros into its own grammar; a parenthesised `(-1)` is rejected with
   `RC2144` / `RC2135`. Do not "tidy" them into `(-1)`.

4. **MFC-private resource IDs** (`FEEDURL`, `MANUALUPDATEFEEDURL`,
   `TESTINGFEEDURL`, `TESTINGMANUALUPDATEFEEDURL`) are only defined in the real
   MFC header. Their numeric values are irrelevant: WinSparkle looks the feed up
   **by name** (`GetCustomResource("FeedURL", "APPCAST")`,
   `winsparkle/src/settings.h`), and no code path in this tree reads the other
   three. They only need to be distinct.

`rc.exe` also needs the Windows SDK and MSVC include directories, or it cannot
resolve `LANG_CHINESE` and reports the same cascade. `weasel.props` passes them.

## 4. `weasel.props` is generated, then hand-patched

`build.bat` normally renders `weasel.props` from `weasel.props.template` with
`cscript render.js`. The generated file is committed here, with three
deliberate changes:

* `BOOST_ROOT` is a **literal path**, not `$(BOOST_ROOT)`. The template's
  `<BuildMacro ...><EnvironmentVariable>true</EnvironmentVariable>` makes MSBuild
  re-resolve the property against the *environment*, which yields an empty string
  and silently drops Boost from the include path.
* `WEASEL_ROOT`, `WEASEL_SDK_ROOT`, `WEASEL_SDK_VER`, `WEASEL_MSVC_ROOT` are
  added, each with a `Condition` so they can also be passed on the command line.
* `ResourceCompile/AdditionalOptions` gains the SDK include paths and
  `build-support`. `AdditionalIncludeDirectories` is *not* used for
  `ResourceCompile`: that metadata is not picked up by this project layout,
  whereas `rcflags` (which `AdditionalOptions` feeds) is appended verbatim.

## 5. Building

```powershell
# both architectures
pwsh -File build-weasel.ps1 -Both

# x64 only
pwsh -File build-weasel.ps1
```

Artifacts land in `dist\x64\` and `dist\x86\`.

### Assembling a redistributable install set: watch the bitness

If you build a deployment package by unpacking the official installer with 7z,
note that **the installer is a 32-bit NSIS executable, so 7z yields the 32-bit
payload.** Two files in it are architecture specific and must be replaced with
their x64 counterparts before they sit next to the x64 `WeaselServer.exe`, which
imports them directly:

| File | 32-bit (from an installer unpack) | 64-bit (correct, from `output\`) |
|---|---|---|
| `rime.dll` | 3,034,624 B | **3,524,096 B** |
| `WinSparkle.dll` | 1,930,240 B | **2,797,056 B** |

Getting this wrong fails silently and confusingly:

```
WeaselDeployer.exe  exit 0xC0000142  (STATUS_DLL_INIT_FAILED)
WeaselServer.exe    exit 0xC000007B  (STATUS_INVALID_IMAGE_FORMAT)
```

Neither code names the offending file, and the install otherwise looks complete.
The reliable check is to walk the import table of each x64 binary and confirm
that every DLL resolved from the *local* directory has machine type `x64`.
The deployment script must perform that check before it installs a rebuilt
package, and abort instead of installing a broken set. Its PE parser was
verified against `dumpbin` (identical machine types for seven binaries,
identical import lists for three).

Also note that `WeaselSetup.exe` copies `weaselx64.dll` to
`C:\Windows\System32` and `weasel.dll` to `SysWOW64`, so whatever sits in the
install directory under those two names is what gets registered as the text
service. `WeaselSetup.exe` itself is x86 in the official payload and stays that
way.

### Platform output-path collisions

Each project writes to a fixed path that is **not** always
platform-differentiated:

| Project | x64 | Win32 |
|---|---|---|
| WeaselServer / WeaselDeployer | `output\` | `output\Win32\` |
| WeaselTSF | `output\weaselx64.dll` | `output\weasel.dll` |
| WeaselSetup | `output\WeaselSetup.exe` | `output\WeaselSetup.exe` ← collides |

Both platforms also emit an import library literally called `rime.lib` into
`lib\` (Win32) and `lib64\` (x64). So the two architectures **cannot both be
harvested from one final state**: `build-weasel.ps1` stages `rime.lib` and the
Boost archives immediately before each platform's build and copies that
platform's artifacts out before switching, which is why the script looks the way
it does. Do not "simplify" that away.

Boost needs no such dance: the x64 and x86 archives differ by the `-x64-` /
`-x32-` token, so both sets can sit in one directory and MSVC picks the right
one. The script copies the x86 set into `stage\lib` for that reason.

`WeaselSetup.exe` is a 32-bit bootstrapper and is byte-identical in both
architectures. That is upstream behaviour, not a build error.

## 6. Tests and preview tool

```powershell
pwsh -File build-ssf-tests.ps1 -Run     # 232 checks
pwsh -File build-ssf-preview.ps1
dist\ssf_preview.exe <skin-dir> --out preview.png
```

Both compile only the platform-free SSF sources plus (for the preview) GDI+, so
neither needs ATL, WTL, Boost, WinSparkle or librime, and they build in seconds.

> Both scripts pass object paths to `link.exe` **inline** rather than through a
> response file. The checkout path contains non-ASCII characters, and a response
> file written as ASCII turns them into `?`; `link.exe` then reports no object
> files at all (`LNK4001` followed by an unresolved `mainCRTStartup`), which
> looks nothing like an encoding problem.

## 7. Things that bite in this codebase

* **`min` / `max` are macros.** `WeaselUI/stdafx.h` deliberately does not define
  `WIN32_LEAN_AND_MEAN`, so `windows.h` leaves `min` and `max` defined. Bare
  `std::min(a, b)` inside the SSF sources therefore fails to compile with
  confusing errors (`C2589: illegal token on right side of '::'`). The SSF
  sources use `(std::min)(a, b)`, matching Weasel's own convention, and the SSF
  headers define `NOMINMAX` before their own includes. Note that `NOMINMAX` in
  the SSF header is **not** sufficient on its own when the header is reached
  after `windows.h`, which is why the parenthesised calls exist too.
* **`gdiplus.h` needs `objidl.h` first**, since it declares methods taking
  `IStream`. `stdafx.h` provides that via ATL; the precompiled-header-free SSF
  translation units include it explicitly.
* **`ID2D1DCRenderTarget::BeginDraw()` returns `void`**, unlike the other Direct2D
  render targets, so it must not be wrapped in `SUCCEEDED()`.
* **`Layout` is both a type and a member-function name** in the Weasel UI. A
  `Layout` member inside a class derived from `weasel::Layout` is hidden by the
  member function, so the SSF layout result type is named `SsfLayoutResult`.
* **`.rc` files are UTF-16LE with a BOM.** `rc.exe` handles that natively; do not
  convert them.
* **`skin.ini` for the Color-P skin is UTF-16LE too** — see `ssf-skin.md` §3.1.

## 8. If you upgrade Weasel

The SSF layer adds files under `WeaselUI/ssf/`, registers them in
`WeaselUI.vcxproj`, and patches `WeaselIPCData.h`, `RimeWithWeasel.cpp`,
`WeaselPanel.h/.cpp` and `WeaselUI/WeaselPanel.cpp`. A rebase onto a newer
upstream will conflict in those files but nowhere else; the SSF sources
themselves are self-contained and should apply cleanly.

`weasel.props` and `build-support/afxres.h` are build plumbing and will need to
be re-created against the newer tree — both are documented above.
