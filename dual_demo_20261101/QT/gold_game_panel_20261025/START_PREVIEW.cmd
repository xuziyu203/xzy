@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RUN_QT_PANEL.ps1" -Preview %*
if errorlevel 1 pause
