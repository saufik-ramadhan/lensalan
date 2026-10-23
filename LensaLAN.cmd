@echo off
setlocal
cd /d "%~dp0"
if not defined LENSALAN_MSYS_ROOT (
    if defined OPENNVR_MSYS_ROOT (
        set "LENSALAN_MSYS_ROOT=%OPENNVR_MSYS_ROOT%"
    ) else (
        set "LENSALAN_MSYS_ROOT=C:\msys64"
    )
)
set "PATH=%LENSALAN_MSYS_ROOT%\ucrt64\bin;%PATH%"
if not exist "%~dp0build\dev\opennvr.exe" (
    echo Build aplikasi terlebih dahulu. Lihat docs\BUILD.md.
    pause
    exit /b 1
)
start "" "%~dp0build\dev\opennvr.exe"
