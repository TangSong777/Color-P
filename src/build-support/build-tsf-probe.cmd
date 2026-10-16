@echo off
rem Build the standalone TSF profile probe (read-only; it never changes the
rem active input method).  Used to verify that the bare-Shift guard can tell
rem Weasel's keyboard profile apart from another layout.
rem
rem 工具链位置不再写死：由 find-toolchain 探测（环境变量覆盖优先，其次 vswhere）。
setlocal
rem %~dp0 ends with the trailing separator, so one level up is the source root.
set ROOT=%~dp0..

for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Find-Toolchain.ps1" -Query all`) do (
  for /f "tokens=1,* delims==" %%a in ("%%i") do (
    if /i "%%a"=="VCVARS"    set "VCVARS=%%b"
    if /i "%%a"=="SDKINC"    set "SDKINC=%%b"
    if /i "%%a"=="SDKLIB"    set "SDKLIB=%%b"
    if /i "%%a"=="ATLMFCINC" set "ATLMFCINC=%%b"
    if /i "%%a"=="ATLMFCLIB" set "ATLMFCLIB=%%b"
  )
)
if not defined VCVARS ( echo [ERROR] toolchain probe failed & exit /b 1 )

call "%VCVARS%" >nul
cl /nologo /std:c++17 /utf-8 /EHsc /MT ^
  "%ROOT%\test\TestSsfSkin\probes\TsfProfileProbe.cpp" ^
  /Fo"%TEMP%\tsfprobe.obj" /Fe"%TEMP%\tsf-profile-probe.exe" ^
  /I"%ROOT%\include" ^
  /I"%SDKINC%\ucrt" ^
  /I"%SDKINC%\um" ^
  /I"%SDKINC%\shared" ^
  /I"%ATLMFCINC%" ^
  /link /LIBPATH:"%SDKLIB%\ucrt\x64" ^
        /LIBPATH:"%SDKLIB%\um\x64" ^
        /LIBPATH:"%ATLMFCLIB%" ^
        user32.lib ole32.lib
endlocal
