@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "PACKAGE_WORK=%PROJECT_ROOT%\build-portable-windows"
set "INSTALL_ROOT=%PACKAGE_WORK%\install"
set "PORTABLE_ROOT=%PACKAGE_WORK%\NanoPulse-portable"
set "DIST_DIR=%PROJECT_ROOT%\dist"

if /I "%~1"=="--help" (
    echo Usage: build-portable.bat
    echo Build the current source as a Windows x64 portable ZIP.
    exit /b 0
)
if not "%~1"=="" (
    echo Error: unknown option %~1 1>&2
    exit /b 2
)

call "%PROJECT_ROOT%\run.bat" --build-only --clean
if errorlevel 1 exit /b 1
if /I not "%PACKAGE_WORK%"=="%PROJECT_ROOT%\build-portable-windows" (
    echo Error: unsafe package directory %PACKAGE_WORK%. 1>&2
    exit /b 1
)
if exist "%PACKAGE_WORK%" rmdir /S /Q "%PACKAGE_WORK%"
mkdir "%PORTABLE_ROOT%"
if errorlevel 1 exit /b 1
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

cmake --install "%PROJECT_ROOT%\build-run" --config Release --prefix "%INSTALL_ROOT%"
if errorlevel 1 exit /b 1
if not exist "%INSTALL_ROOT%\bin\NanoPulse.exe" (
    echo Error: install step did not produce NanoPulse.exe. 1>&2
    exit /b 1
)
xcopy "%INSTALL_ROOT%\bin\*" "%PORTABLE_ROOT%\" /E /I /Q /Y >nul
if errorlevel 1 exit /b 1

set "WINDEPLOYQT="
for /f "delims=" %%W in ('where windeployqt.exe 2^>nul') do if not defined WINDEPLOYQT set "WINDEPLOYQT=%%W"
if not defined WINDEPLOYQT (
    where qtpaths6.exe >nul 2>&1
    if not errorlevel 1 for /f "delims=" %%Q in ('qtpaths6 --query QT_INSTALL_PREFIX') do if exist "%%Q\bin\windeployqt.exe" set "WINDEPLOYQT=%%Q\bin\windeployqt.exe"
)
if not defined WINDEPLOYQT (
    echo Error: windeployqt.exe was not found. 1>&2
    exit /b 1
)
"%WINDEPLOYQT%" --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --compiler-runtime --skip-plugin-types "generic,iconengines,imageformats,networkinformation,styles" --exclude-plugins "qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql,qcertonlybackend,qopensslbackend" "%PORTABLE_ROOT%\NanoPulse.exe"
if errorlevel 1 exit /b 1

(
    echo [Paths]
    echo Plugins=.
) >"%PORTABLE_ROOT%\qt.conf"
(
    echo NanoPulse portable build.
    echo Run NanoPulse.exe from this directory.
) >"%PORTABLE_ROOT%\README.txt"

set "VERSION="
for /f "tokens=2" %%V in ('findstr /R /C:"^[ ]*VERSION [0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*" "%PROJECT_ROOT%\CMakeLists.txt"') do set "VERSION=%%V"
if not defined VERSION (
    echo Error: version not found in CMakeLists.txt. 1>&2
    exit /b 1
)
set "ARCHIVE=%DIST_DIR%\NanoPulse-portable-windows-x64-v%VERSION%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%PORTABLE_ROOT%\*' -DestinationPath '%ARCHIVE%' -CompressionLevel Optimal -Force; $h=(Get-FileHash '%ARCHIVE%' -Algorithm SHA256).Hash.ToLower(); ('{0}  NanoPulse-portable-windows-x64-v%VERSION%.zip' -f $h) | Set-Content '%ARCHIVE%.sha256' -Encoding Ascii"
if errorlevel 1 exit /b 1
echo Portable ZIP: %ARCHIVE%
echo Checksum: %ARCHIVE%.sha256
exit /b 0
