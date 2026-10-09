@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\git_commit.ps1"
set "ee_exit=%errorlevel%"
pause
exit /b %ee_exit%
