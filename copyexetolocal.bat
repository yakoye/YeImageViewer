@echo off

set "DEST=C:\Users\color\AppData\Local\Programs\YeImageViewer"

if not exist "%DEST%" mkdir "%DEST%"

echo Copying "%~dp0x64\Release\YeImageViewer.exe"...
copy /Y "%~dp0x64\Release\YeImageViewer.exe" "%DEST%\"

echo Copying "%~dp0x64\Release\YeThumbnailProvider.dll"...
copy /Y "%~dp0x64\Release\YeThumbnailProvider.dll" "%DEST%\"

pause