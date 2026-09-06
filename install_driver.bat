@echo off
setlocal
rem Runs the driver install pipeline. Keeps the window open on failure so
rem you can read the error. Requires an elevated (Run as Administrator) shell.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_driver.ps1"
set RC=%ERRORLEVEL%
echo.
echo ============================================
echo installer exit code: %RC%
echo ============================================
pause
endlocal
exit /b %RC%