@echo off
rem Deploy the rebuilt TSF DLL to System32 / SysWOW64 (needs administrator).
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%~dp0deploy-tsf.ps1""'"