@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "INSTALL_DIR="
set "MODE="
set "REG_ROOT="
set "PATH_ADDED=0"
set "PURGE=0"

:parse
if "%~1"=="" goto load
if /I "%~1"=="--installed" (
    if "%~2"=="" (
        echo Error: --installed requires a directory. 1>&2
        exit /b 2
    )
    set "INSTALL_DIR=%~2"
    shift
    shift
    goto parse
)
if /I "%~1"=="--purge" (
    set "PURGE=1"
    shift
    goto parse
)
if /I "%~1"=="--help" (
    echo Usage: uninstall.bat [--installed DIR] [--purge]
    exit /b 0
)
echo Error: unknown option %~1 1>&2
exit /b 2

:load
set "METADATA=%APPDATA%\NanoPulse\install.ini"
if exist "%METADATA%" (
    for /f "usebackq tokens=1,* delims==" %%A in ("%METADATA%") do (
        if /I "%%A"=="INSTALL_DIR" if not defined INSTALL_DIR set "INSTALL_DIR=%%B"
        if /I "%%A"=="MODE" set "MODE=%%B"
        if /I "%%A"=="REG_ROOT" set "REG_ROOT=%%B"
        if /I "%%A"=="PATH_ADDED" set "PATH_ADDED=%%B"
    )
)
if not defined INSTALL_DIR if exist "%ProgramFiles%\NanoPulse\NanoPulse.exe" set "INSTALL_DIR=%ProgramFiles%\NanoPulse"
if not defined INSTALL_DIR if exist "%APPDATA%\NanoPulse\NanoPulse.exe" set "INSTALL_DIR=%APPDATA%\NanoPulse"

set "FOUND=0"
if defined INSTALL_DIR if exist "%INSTALL_DIR%\NanoPulse.exe" set "FOUND=1"

if "%PATH_ADDED%"=="1" if defined INSTALL_DIR (
    if /I "%MODE%"=="portable" (
        powershell -NoProfile -Command "$p=[Environment]::GetEnvironmentVariable('Path','Machine');$n=(($p -split ';'|?{$_ -and $_ -ne '%INSTALL_DIR%'}) -join ';');[Environment]::SetEnvironmentVariable('Path',$n,'Machine')"
    ) else (
        powershell -NoProfile -Command "$p=[Environment]::GetEnvironmentVariable('Path','User');$n=(($p -split ';'|?{$_ -and $_ -ne '%INSTALL_DIR%'}) -join ';');[Environment]::SetEnvironmentVariable('Path',$n,'User')"
    )
)

reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" /f >nul 2>&1
if exist "%ProgramData%\Microsoft\Windows\Start Menu\Programs\NanoPulse" (
    rmdir /S /Q "%ProgramData%\Microsoft\Windows\Start Menu\Programs\NanoPulse"
    echo Removed: Start Menu shortcuts
)
if exist "%APPDATA%\Microsoft\Windows\Start Menu\Programs\NanoPulse" (
    rmdir /S /Q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\NanoPulse"
    echo Removed: user Start Menu shortcuts
)

set "REMOVE_CONFIG=0"
if "%PURGE%"=="1" (
    set "REMOVE_CONFIG=1"
) else (
    set /P "ANSWER=Remove NanoPulse user configuration and database? [y/N] "
    if /I "!ANSWER!"=="Y" set "REMOVE_CONFIG=1"
)
if "!REMOVE_CONFIG!"=="1" (
    if exist "%APPDATA%\NanoPulse\NanoPulse" rmdir /S /Q "%APPDATA%\NanoPulse\NanoPulse"
    if exist "%LOCALAPPDATA%\NanoPulse\NanoPulse" rmdir /S /Q "%LOCALAPPDATA%\NanoPulse\NanoPulse"
    if exist "%LOCALAPPDATA%\NanoPulse" rmdir /S /Q "%LOCALAPPDATA%\NanoPulse"
    echo Removed: user configuration
)

if exist "%PROJECT_ROOT%\build" (
    rmdir /S /Q "%PROJECT_ROOT%\build"
    echo Removed: %PROJECT_ROOT%\build
)
if exist "%LOCALAPPDATA%\NanoPulse\cache" rmdir /S /Q "%LOCALAPPDATA%\NanoPulse\cache"
if exist "%METADATA%" del /F /Q "%METADATA%"

if "%FOUND%"=="0" (
    echo NanoPulse was not installed in a known location. Remaining artifacts were cleaned.
    exit /b 0
)

for %%I in ("%INSTALL_DIR%") do set "INSTALL_PARENT=%%~dpI"
if /I "%PROJECT_ROOT%"=="%INSTALL_DIR%" (
    echo Scheduled removal: %INSTALL_DIR%
    start "" /B cmd /C "timeout /T 2 /NOBREAK >nul & rmdir /S /Q \"%INSTALL_DIR%\""
) else (
    rmdir /S /Q "%INSTALL_DIR%"
    if exist "%INSTALL_DIR%" (
        echo Error: failed to remove %INSTALL_DIR%. 1>&2
        exit /b 1
    )
    echo Removed: %INSTALL_DIR%
)
echo NanoPulse uninstallation completed.
exit /b 0
