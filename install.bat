@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "APP_NAME=NanoPulse"
set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "VERSION="
for /f "tokens=2" %%V in ('findstr /R /C:"^[ ]*VERSION [0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*" "%PROJECT_ROOT%\CMakeLists.txt"') do set "VERSION=%%V"
if not defined VERSION (
    echo Error: version not found in CMakeLists.txt. 1>&2
    exit /b 1
)
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "MODE=portable"
set "INSTALL_DIR="
set "AUTO_QT=0"
set "BACKUP_DIR="
set "CREATED_INSTALL=0"
set "PATH_ADDED=0"

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--portable" (
    set "MODE=portable"
    shift
    goto parse
)
if /I "%~1"=="--user" (
    set "MODE=user"
    shift
    goto parse
)
if /I "%~1"=="--path" (
    if "%~2"=="" goto missing_path
    set "MODE=custom"
    set "INSTALL_DIR=%~2"
    shift
    shift
    goto parse
)
if /I "%~1"=="--qt-auto" (
    set "AUTO_QT=1"
    shift
    goto parse
)
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
echo Error: unknown option %~1 1>&2
exit /b 2

:missing_path
echo Error: --path requires a directory. 1>&2
exit /b 2

:help
echo Usage: install.bat [--portable^|--user^|--path DIR] [--qt-auto]
echo   --portable  Install to Program Files\NanoPulse ^(default^)
echo   --user      Install to %%APPDATA%%\NanoPulse
echo   --path DIR  Install to a custom directory
echo   --qt-auto   Download Qt 6.8.3 with aqtinstall when Qt is absent
exit /b 0

:parsed
powershell -NoProfile -Command "if ([Environment]::OSVersion.Version.Major -lt 10) { exit 1 }"
if errorlevel 1 (
    echo Error: Windows 10 or newer is required. 1>&2
    exit /b 1
)

if "%MODE%"=="portable" set "INSTALL_DIR=%ProgramFiles%\NanoPulse"
if "%MODE%"=="user" set "INSTALL_DIR=%APPDATA%\NanoPulse"
if not defined INSTALL_DIR (
    echo Error: installation directory is empty. 1>&2
    exit /b 1
)

if "%MODE%"=="portable" (
    net session >nul 2>&1
    if errorlevel 1 (
        echo Error: the default Program Files installation requires an Administrator command prompt. 1>&2
        exit /b 1
    )
)

where cmake.exe >nul 2>&1
if errorlevel 1 (
    echo Error: CMake 3.16 or newer is required. 1>&2
    exit /b 1
)
for /f "tokens=3" %%V in ('cmake --version ^| findstr /B /C:"cmake version"') do set "CMAKE_VERSION=%%V"
powershell -NoProfile -Command "if ([version]'%CMAKE_VERSION%' -lt [version]'3.16') { exit 1 }"
if errorlevel 1 (
    echo Error: CMake 3.16 or newer is required; found %CMAKE_VERSION%. 1>&2
    exit /b 1
)

where cl.exe >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo Error: Visual Studio 2022 Build Tools with MSVC are required. 1>&2
        exit /b 1
    )
    for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
    if not defined VSINSTALL (
        echo Error: MSVC x64 build tools were not found. 1>&2
        exit /b 1
    )
    call "!VSINSTALL!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
    if errorlevel 1 goto failed
)

set "QT_ROOT="
where qtpaths6.exe >nul 2>&1
if not errorlevel 1 (
    for /f "delims=" %%Q in ('qtpaths6 --query QT_INSTALL_PREFIX') do set "QT_ROOT=%%Q"
)
if not defined QT_ROOT if defined Qt6_DIR set "QT_ROOT=%Qt6_DIR%\..\..\.."
if not defined QT_ROOT if defined CMAKE_PREFIX_PATH for /f "tokens=1 delims=;" %%Q in ("%CMAKE_PREFIX_PATH%") do set "QT_ROOT=%%Q"
if not defined QT_ROOT if "%AUTO_QT%"=="1" (
    where python.exe >nul 2>&1
    if errorlevel 1 (
        echo Error: Python is required for --qt-auto. 1>&2
        exit /b 1
    )
    python -m pip install --user aqtinstall
    if errorlevel 1 goto failed
    set "QT_ROOT=%LOCALAPPDATA%\NanoPulse\Qt\6.8.3\msvc2022_64"
    python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O "%LOCALAPPDATA%\NanoPulse\Qt"
    if errorlevel 1 goto failed
)
if not defined QT_ROOT (
    echo Error: Qt 6 development files were not found. Set Qt6_DIR/CMAKE_PREFIX_PATH, add qtpaths6 to PATH, or use --qt-auto. 1>&2
    exit /b 1
)
if not exist "%QT_ROOT%\bin\windeployqt.exe" (
    echo Error: windeployqt.exe was not found under %QT_ROOT%. 1>&2
    exit /b 1
)

set "SQLITE_HEADER="
if defined SQLITE3_ROOT if exist "%SQLITE3_ROOT%\include\sqlite3.h" set "SQLITE_HEADER=%SQLITE3_ROOT%\include\sqlite3.h"
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\installed\x64-windows\include\sqlite3.h" set "SQLITE_HEADER=%VCPKG_ROOT%\installed\x64-windows\include\sqlite3.h"
if not defined VCPKG_ROOT for /f "delims=" %%V in ('where vcpkg.exe 2^>nul') do if not defined VCPKG_ROOT for %%D in ("%%~dpV..") do set "VCPKG_ROOT=%%~fD"
if not defined SQLITE_HEADER (
    where vcpkg.exe >nul 2>&1
    if not errorlevel 1 (
        vcpkg install sqlite3:x64-windows
        if errorlevel 1 goto failed
        if exist "%VCPKG_ROOT%\installed\x64-windows\include\sqlite3.h" set "SQLITE_HEADER=%VCPKG_ROOT%\installed\x64-windows\include\sqlite3.h"
    )
)
if not defined SQLITE_HEADER (
    echo Error: SQLite3 development headers were not found. Set SQLITE3_ROOT or install sqlite3:x64-windows with vcpkg. 1>&2
    exit /b 1
)

if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
if exist "%BUILD_DIR%" (
    echo Error: failed to clean %BUILD_DIR%. 1>&2
    exit /b 1
)

cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_ROOT%"
if errorlevel 1 goto failed
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 goto failed
cmake --install "%BUILD_DIR%" --config Release --prefix "%BUILD_DIR%\stage"
if errorlevel 1 goto failed
"%QT_ROOT%\bin\windeployqt.exe" --release --no-translations --compiler-runtime "%BUILD_DIR%\stage\bin\NanoPulse.exe"
if errorlevel 1 goto failed

if exist "%INSTALL_DIR%" (
    set "BACKUP_DIR=%INSTALL_DIR%.install-backup-%RANDOM%"
    move "%INSTALL_DIR%" "!BACKUP_DIR!" >nul
    if errorlevel 1 goto failed
)
mkdir "%INSTALL_DIR%"
if errorlevel 1 goto failed
set "CREATED_INSTALL=1"
xcopy "%BUILD_DIR%\stage\bin\*" "%INSTALL_DIR%\" /E /I /Q /Y >nul
if errorlevel 1 goto failed
copy /Y "%PROJECT_ROOT%\uninstall.bat" "%INSTALL_DIR%\uninstall.bat" >nul
if errorlevel 1 goto failed

set "REG_ROOT=HKCU"
if "%MODE%"=="portable" set "REG_ROOT=HKLM"
if "%MODE%"=="portable" (
    set "START_MENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs\NanoPulse"
    mkdir "!START_MENU!" 2>nul
    powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('!START_MENU!\NanoPulse.lnk');$s.TargetPath='%INSTALL_DIR%\NanoPulse.exe';$s.WorkingDirectory='%INSTALL_DIR%';$s.Save()"
)
reg add "%REG_ROOT%\Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" /f /v DisplayName /d "NanoPulse" >nul
reg add "%REG_ROOT%\Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" /f /v DisplayVersion /d "%VERSION%" >nul
reg add "%REG_ROOT%\Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" /f /v InstallLocation /d "%INSTALL_DIR%" >nul
reg add "%REG_ROOT%\Software\Microsoft\Windows\CurrentVersion\Uninstall\NanoPulse" /f /v UninstallString /d "\"%INSTALL_DIR%\uninstall.bat\" --installed \"%INSTALL_DIR%\"" >nul
if errorlevel 1 goto failed

set /P "ADD_PATH=Add NanoPulse to PATH? [y/N] "
if /I "!ADD_PATH!"=="Y" (
    if "%MODE%"=="portable" (
        powershell -NoProfile -Command "$p=[Environment]::GetEnvironmentVariable('Path','Machine');if (($p -split ';') -notcontains '%INSTALL_DIR%'){[Environment]::SetEnvironmentVariable('Path',($p.TrimEnd(';')+';%INSTALL_DIR%'),'Machine')}"
    ) else (
        powershell -NoProfile -Command "$p=[Environment]::GetEnvironmentVariable('Path','User');if (($p -split ';') -notcontains '%INSTALL_DIR%'){[Environment]::SetEnvironmentVariable('Path',($p.TrimEnd(';')+';%INSTALL_DIR%'),'User')}"
    )
    if errorlevel 1 goto failed
    set "PATH_ADDED=1"
)

if not exist "%APPDATA%\NanoPulse" mkdir "%APPDATA%\NanoPulse"
(
    echo INSTALL_DIR=%INSTALL_DIR%
    echo MODE=%MODE%
    echo REG_ROOT=%REG_ROOT%
    echo PATH_ADDED=%PATH_ADDED%
    echo VERSION=%VERSION%
) >"%APPDATA%\NanoPulse\install.ini"

if defined BACKUP_DIR rmdir /S /Q "!BACKUP_DIR!"
echo.
echo NanoPulse %VERSION% installed successfully.
echo Binary: %INSTALL_DIR%\NanoPulse.exe
echo Uninstall: %INSTALL_DIR%\uninstall.bat
set /P "LAUNCH=Launch NanoPulse now? [y/N] "
if /I "!LAUNCH!"=="Y" start "" "%INSTALL_DIR%\NanoPulse.exe"
exit /b 0

:failed
echo Error: installation failed; rolling back. 1>&2
if "%CREATED_INSTALL%"=="1" if exist "%INSTALL_DIR%" rmdir /S /Q "%INSTALL_DIR%"
if defined BACKUP_DIR if exist "!BACKUP_DIR!" move "!BACKUP_DIR!" "%INSTALL_DIR%" >nul
exit /b 1
