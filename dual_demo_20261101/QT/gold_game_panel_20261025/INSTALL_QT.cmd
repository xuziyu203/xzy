@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RUN_QT_PANEL.ps1" -Install %*
if errorlevel 1 (
  echo Installation failed. Read the error above.
) else (
  echo Installed. Double-click START_PANEL.cmd to launch.
)
pause
