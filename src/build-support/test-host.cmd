@echo off
rem 构建宿主兼容性回归测试。工具链位置由 Find-Toolchain.ps1 探测，不再写死本机路径。
setlocal
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Find-Toolchain.ps1" -Query vcvars64`) do set "VCVARS=%%i"
if not defined VCVARS ( echo [ERROR] toolchain probe failed & exit /b 1 )
rem 本脚本约定从 src\ 目录运行（输出到 dist\）。
call "%VCVARS%" >nul
cl /nologo /std:c++17 /utf-8 /EHsc /MT test\TestSsfSkin\HostCompatibilityRegression.cpp /Fodist\host-regression.obj /Fedist\host-regression.exe /link user32.lib
endlocal
