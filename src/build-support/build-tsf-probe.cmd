@echo off
rem Build the standalone TSF profile probe (read-only; it never changes the
rem active input method).  Used to verify that the bare-Shift guard can tell
rem Weasel's keyboard profile apart from another layout.
setlocal
rem %~dp0 ends with the trailing separator, so one level up is the source root.
set ROOT=%~dp0..
call D:\VisualStudio2022\VC\Auxiliary\Build\vcvars64.bat >nul
cl /nologo /std:c++17 /utf-8 /EHsc /MT ^
  "%ROOT%\test\TestSsfSkin\probes\TsfProfileProbe.cpp" ^
  /Fo"%TEMP%\tsfprobe.obj" /Fe"%TEMP%\tsf-profile-probe.exe" ^
  /I"%ROOT%\include" ^
  /I"D:\Windows Kits\10\Include\10.0.26100.0\ucrt" ^
  /I"D:\Windows Kits\10\Include\10.0.26100.0\um" ^
  /I"D:\Windows Kits\10\Include\10.0.26100.0\shared" ^
  /I"D:\VisualStudio2022\VC\Tools\MSVC\14.44.35207\atlmfc\include" ^
  /link /LIBPATH:"D:\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64" ^
        /LIBPATH:"D:\Windows Kits\10\Lib\10.0.26100.0\um\x64" ^
        /LIBPATH:"D:\VisualStudio2022\VC\Tools\MSVC\14.44.35207\atlmfc\lib\x64" ^
        user32.lib ole32.lib
endlocal
