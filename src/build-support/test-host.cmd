@echo off
call D:\VisualStudio2022\VC\Auxiliary\Build\vcvars64.bat >nul
cl /nologo /std:c++17 /utf-8 /EHsc /MT test\TestSsfSkin\HostCompatibilityRegression.cpp /Fodist\host-regression.obj /Fedist\host-regression.exe /link user32.lib
