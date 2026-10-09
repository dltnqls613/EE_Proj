@echo off
setlocal
cd /d "%~dp0"
git pull --ff-only
if errorlevel 1 goto error
echo GIT PULL COMPLETE
pause
exit /b 0
:error
echo GIT PULL FAILED. Local files are preserved.
pause
exit /b 1
