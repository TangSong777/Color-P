@echo off
setlocal
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Find-Toolchain.ps1" -Query vcvars64`) do set "VCVARS=%%i"
if not defined VCVARS ( echo [ERROR] toolchain probe failed & exit /b 1 )
call "%VCVARS%" >nul
cl /nologo /std:c++17 /utf-8 /EHsc /MT test\TestSsfSkin\HostCompatibilityRegression.cpp /Fodist\host-regression.obj /Fedist\host-regression.exe /link user32.lib
endlocal
