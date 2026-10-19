@echo off
rem Build the dictionary-coverage functional probe (real librime chain).
rem Toolchain paths come from Find-Toolchain.ps1 instead of hard-coded locations.
setlocal
set ROOT=%~dp0..
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Find-Toolchain.ps1" -Query vcvars64`) do set "VCVARS=%%i"
if not defined VCVARS ( echo [ERROR] toolchain probe failed & exit /b 1 )

set OUT=%ROOT%\dist
if not exist "%OUT%" mkdir "%OUT%"
call "%VCVARS%" >nul
cl /nologo /std:c++17 /utf-8 /EHsc /MT ^
  "%ROOT%\test\TestSsfSkin\probes\DictCoverageProbe.cpp" ^
  /Fo"%OUT%\dict-probe.obj" /Fe"%OUT%\dict-probe.exe" ^
  /I"%ROOT%\include" ^
  /link /LIBPATH:"%ROOT%\build-libs" rime_x64.lib user32.lib
if errorlevel 1 ( echo [ERROR] build failed & exit /b 1 )

rem The probe loads rime.dll at runtime.
if not exist "%OUT%\rime.dll" (
  copy /y "%ROOT%\..\weasel-0.17.4\rime.dll" "%OUT%\rime.dll" >nul 2>&1
)
echo built: %OUT%\dict-probe.exe
endlocal