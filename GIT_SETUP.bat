@echo off
setlocal
cd /d "%~dp0"
if exist .git goto already
where git >nul 2>nul
if errorlevel 1 goto error
git init -b main
if errorlevel 1 goto error
git remote add origin https://github.com/dltnqls613/EE_Proj.git
if errorlevel 1 goto error
git fetch origin main
if errorlevel 1 goto error
rem Initial ZIP setup only: set the Git baseline, keeping every working file.
git reset --mixed origin/main
if errorlevel 1 goto error
git branch --set-upstream-to=origin/main main
if errorlevel 1 goto error
echo SETUP COMPLETE. Run GIT_COMMIT.bat to commit and push changes.
pause
exit /b 0
:already
echo Git is already initialized. No changes made.
pause
exit /b 0
:error
echo SETUP FAILED. Review the Git error above. Working files are preserved.
pause
exit /b 1
