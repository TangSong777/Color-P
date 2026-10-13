@echo off
rem 构建「裸 Shift 切换中英文」状态机的回归测试。
rem 判定逻辑在 include\WeaselIdleShift.h，测试在 test\TestSsfSkin\IdleShiftRegression.cpp。
rem 与 test-host.cmd 同样只依赖 Windows SDK，不需要 librime/Boost/WTL。
call D:\VisualStudio2022\VC\Auxiliary\Build\vcvars64.bat >nul
cl /nologo /std:c++17 /utf-8 /EHsc /MT test\TestSsfSkin\IdleShiftRegression.cpp /Fodist\idle-shift.obj /Fedist\idle-shift.exe /Iinclude
