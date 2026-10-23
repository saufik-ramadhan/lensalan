@echo off
setlocal
cd /d "%~dp0"
if not defined OPENNVR_MSYS_ROOT set "OPENNVR_MSYS_ROOT=C:\msys64"
set "PATH=%OPENNVR_MSYS_ROOT%\ucrt64\bin;%PATH%"
if not exist "%~dp0build\dev\opennvr.exe" (
    echo Build aplikasi terlebih dahulu. Lihat docs\BUILD.md.
    pause
    exit /b 1
)
start "" "%~dp0build\dev\opennvr.exe"
