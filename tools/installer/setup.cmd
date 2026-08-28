@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0installLocal.ps1" %*
exit /b %ERRORLEVEL%
