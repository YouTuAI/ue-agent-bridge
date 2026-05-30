@echo off
setlocal enabledelayedexpansion
echo ================================================
echo   UE Agent Bridge - Build Script
echo ================================================
echo.

REM ============================================
REM  Load configuration
REM  Priority: environment variables ^> build-config.bat
REM ============================================

REM Source build-config.bat if it exists
if exist "%~dp0build-config.bat" (
    echo [INFO] Loading build-config.bat...
    call "%~dp0build-config.bat"
)

REM Environment variables override config file
if defined UE_ENGINE_PATH_OVERRIDE set "UE_ENGINE_PATH=%UE_ENGINE_PATH_OVERRIDE%"
if defined UE_PROJECT_PATH_OVERRIDE set "UE_PROJECT_PATH=%UE_PROJECT_PATH_OVERRIDE%"

REM ============================================
REM  Validate engine path
REM ============================================
if not defined UE_ENGINE_PATH (
    echo [ERROR] UE_ENGINE_PATH not set.
    echo   Create build-config.bat: copy build-config.example.bat build-config.bat
    echo   Or set environment variable: set UE_ENGINE_PATH=D:\UE5\UE_5.6
    exit /b 1
)
set "UE_ENGINE=%UE_ENGINE_PATH%"

set "UBT_DLL=%UE_ENGINE%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
if not exist "%UBT_DLL%" (
    echo [ERROR] UnrealBuildTool not found at:
    echo   %UBT_DLL%
    echo   Check UE_ENGINE_PATH in build-config.bat
    exit /b 1
)

REM ============================================
REM  Validate project path
REM ============================================
if not defined UE_PROJECT_PATH (
    echo [ERROR] UE_PROJECT_PATH not set.
    echo   Create build-config.bat: copy build-config.example.bat build-config.bat
    echo   Or set environment variable: set UE_PROJECT_PATH=D:\your\project
    exit /b 1
)
set "UE_PROJECT=%UE_PROJECT_PATH%"

if not exist "%UE_PROJECT%\" (
    echo [ERROR] Project directory not found: %UE_PROJECT%
    exit /b 1
)

REM ============================================
REM  Find .uproject and derive target name
REM ============================================
set "UE_PROJECT_FILE="
for %%f in ("%UE_PROJECT%\*.uproject") do (
    set "UE_PROJECT_FILE=%%f"
    set "PROJECT_NAME=%%~nf"
)

if not defined PROJECT_NAME (
    echo [ERROR] No .uproject file found in %UE_PROJECT%
    exit /b 1
)
set "TARGET_NAME=%PROJECT_NAME%Editor"

REM ============================================
REM  Auto-computed paths (no hardcode needed)
REM ============================================
set "PLUGIN_SRC=%~dp0plugin\UEAgentBridge"
set "PLUGIN_DST=%UE_PROJECT%\Plugins\UEAgentBridge"

echo [CONFIG] Engine:  %UE_ENGINE%
echo [CONFIG] Project: %UE_PROJECT_FILE%
echo [CONFIG] Target:  %TARGET_NAME%
echo.

REM ============================================
REM  [1/4] Check UE Editor not running
REM ============================================
echo [1/4] Checking UE Editor...
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor" >nul
if !ERRORLEVEL!==0 (
    echo [ERROR] UnrealEditor.exe is running!
    echo Please close UE Editor first, then re-run this script.
    exit /b 1
)
echo       OK - Editor not running

REM ============================================
REM  [2/4] Sync plugin source to project
REM ============================================
echo.
echo [2/4] Syncing plugin source...
if not exist "%PLUGIN_SRC%" (
    echo [ERROR] Plugin source not found: %PLUGIN_SRC%
    exit /b 1
)
if not exist "%PLUGIN_DST%" mkdir "%PLUGIN_DST%"
xcopy "%PLUGIN_SRC%\*" "%PLUGIN_DST%\" /E /Y /Q /I >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo [WARNING] xcopy failed, trying direct copy...
    copy /Y "%PLUGIN_SRC%\UEAgentBridge.uplugin" "%PLUGIN_DST%\" >nul 2>&1
)
echo       Done

REM ============================================
REM  [3/4] Clean Intermediate
REM ============================================
echo.
echo [3/4] Cleaning Intermediate...
if exist "%PLUGIN_DST%\Intermediate" (
    rmdir /S /Q "%PLUGIN_DST%\Intermediate"
    echo       Intermediate deleted - full recompile
) else (
    echo       Already clean
)

REM ============================================
REM  [4/4] Compile C++ plugin
REM ============================================
echo.
echo [4/4] Compiling C++ plugin (%TARGET_NAME%)...
echo       This takes 30-60 seconds...
echo.
call dotnet "%UBT_DLL%" %TARGET_NAME% Win64 Development "%UE_PROJECT_FILE%"
set BUILD_RESULT=%ERRORLEVEL%

REM ============================================
REM  Report
REM ============================================
echo.
echo ================================================
if %BUILD_RESULT%==0 (
    echo   BUILD SUCCESS
    echo ================================================
    echo.
    echo Next: start Unreal Editor, enable plugin "UE Agent Bridge"
) else (
    echo   BUILD FAILED (exit code %BUILD_RESULT%)
    echo ================================================
    echo.
    echo Check: %%LocalAppData%%\UnrealBuildTool\Log.txt
)
echo.
exit /b %BUILD_RESULT%
