@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "BUILD_DIR=%PROJECT_ROOT%\build-run"
set "BUILD_ONLY=0"
set "CLEAN=0"

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--clean" (
    set "CLEAN=1"
    shift
    goto parse
)
if /I "%~1"=="--build-only" (
    set "BUILD_ONLY=1"
    shift
    goto parse
)
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
echo Error: unknown option %~1 1>&2
exit /b 2

:help
echo Usage: run.bat [--clean] [--build-only] [--help]
echo   --clean       Remove the latest-code build directory first
echo   --build-only  Build without launching NanoPulse
echo   --help        Show this help
exit /b 0

:parsed
where cmake.exe >nul 2>&1
if errorlevel 1 (
    echo Error: CMake is required. 1>&2
    exit /b 1
)
if "%CLEAN%"=="1" (
    if /I not "%BUILD_DIR%"=="%PROJECT_ROOT%\build-run" (
        echo Error: unsafe build directory %BUILD_DIR%. 1>&2
        exit /b 1
    )
    if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
    if exist "%BUILD_DIR%" (
        echo Error: failed to clean %BUILD_DIR%. 1>&2
        exit /b 1
    )
)

set "QT_ROOT="
where qtpaths6.exe >nul 2>&1
if not errorlevel 1 for /f "delims=" %%Q in ('qtpaths6 --query QT_INSTALL_PREFIX') do set "QT_ROOT=%%Q"
if not defined QT_ROOT if defined Qt6_DIR set "QT_ROOT=%Qt6_DIR%\..\..\.."
if not defined QT_ROOT if defined CMAKE_PREFIX_PATH for /f "tokens=1 delims=;" %%Q in ("%CMAKE_PREFIX_PATH%") do set "QT_ROOT=%%Q"

set "QT_SPEC="
if defined QT_ROOT if exist "%QT_ROOT%\bin\qmake6.exe" for /f "delims=" %%S in ('"%QT_ROOT%\bin\qmake6.exe" -query QMAKE_XSPEC') do set "QT_SPEC=%%S"
if /I "!QT_SPEC!"=="win32-g++" (
    where g++.exe >nul 2>&1
    if errorlevel 1 (
        for /D %%D in ("%QT_ROOT%\..\..\Tools\mingw*_64") do if exist "%%~fD\bin\g++.exe" set "PATH=%%~fD\bin;!PATH!"
    )
    where g++.exe >nul 2>&1
    if errorlevel 1 (
        echo Error: the Qt MinGW compiler was not found. 1>&2
        exit /b 1
    )
    if defined QT_ROOT (
        cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_ROOT%"
    ) else (
        cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    )
) else (
    if defined QT_ROOT (
        cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -DCMAKE_PREFIX_PATH="%QT_ROOT%"
    ) else (
        cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%"
    )
)
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b 1
if "%BUILD_ONLY%"=="1" exit /b 0

set "EXECUTABLE=%BUILD_DIR%\NanoPulse.exe"
if not exist "%EXECUTABLE%" set "EXECUTABLE=%BUILD_DIR%\Release\NanoPulse.exe"
if not exist "%EXECUTABLE%" (
    echo Error: build completed without producing NanoPulse.exe. 1>&2
    exit /b 1
)
"%EXECUTABLE%"
exit /b %ERRORLEVEL%
