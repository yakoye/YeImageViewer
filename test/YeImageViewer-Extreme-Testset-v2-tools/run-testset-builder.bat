@echo off
setlocal
cd /d "%~dp0"

echo YeImageViewer Extreme Testset v2
echo.
echo This will download standard/public test images and generate extreme PNG fixtures.
echo Output: YeImageViewer-Extreme-Testset-v2-data
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-YeImageViewer-Testset.ps1"

echo.
echo Finished.
pause
