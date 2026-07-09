@echo off
setlocal

cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\update-github.ps1"
set EXIT_CODE=%ERRORLEVEL%

echo.
if not "%EXIT_CODE%"=="0" (
    echo GitHub update failed with exit code %EXIT_CODE%.
) else (
    echo GitHub update complete.
)

echo.
pause
exit /b %EXIT_CODE%
