@echo off
setlocal enabledelayedexpansion
echo.
echo ================================================
echo   UE Agent Bridge - One-Click Install
echo ================================================
echo.

REM ============================================
REM  Step 0: Load configuration
REM ============================================

REM Accept project path as argument
if "%~1" neq "" (
    set "UE_PROJECT_PATH=%~1"
    echo [OK] Using project from argument: !UE_PROJECT_PATH!
)

REM Load build-config.bat for UE_ENGINE_PATH
if exist "%~dp0build-config.bat" (
    call "%~dp0build-config.bat"
)

REM Environment override
if defined UE_ENGINE_PATH_OVERRIDE set "UE_ENGINE_PATH=%UE_ENGINE_PATH_OVERRIDE%"
if defined UE_PROJECT_PATH_OVERRIDE set "UE_PROJECT_PATH=%UE_PROJECT_PATH_OVERRIDE%"

REM ============================================
REM  Validate
REM ============================================

if not defined UE_ENGINE_PATH (
    echo [ERROR] UE_ENGINE_PATH not set.
    echo   Option A: Create build-config.bat from build-config.example.bat
    echo   Option B: set UE_ENGINE_PATH=D:\UE5\UE_5.6
    exit /b 1
)

if not defined UE_PROJECT_PATH (
    echo [ERROR] UE_PROJECT_PATH not set.
    echo   Usage: install.bat "D:\MyProject\MyProject.uproject"
    echo   Or configure build-config.bat
    exit /b 1
)

REM Find .uproject file
if not exist "%UE_PROJECT_PATH%" (
    set "UE_PROJECT_FILE="
    for %%f in ("%UE_PROJECT_PATH%\*.uproject") do set "UE_PROJECT_FILE=%%f"
    if not defined UE_PROJECT_FILE (
        echo [ERROR] Cannot find project: %UE_PROJECT_PATH%
        exit /b 1
    )
    set "UE_PROJECT_PATH=!UE_PROJECT_FILE!"
)

for %%f in ("%UE_PROJECT_PATH%") do (
    set "PROJECT_NAME=%%~nf"
    set "PROJECT_DIR=%%~dpf"
)
set "TARGET_NAME=%PROJECT_NAME%Editor"

echo.
echo [CONFIG] Engine:  %UE_ENGINE_PATH%
echo [CONFIG] Project: %UE_PROJECT_PATH%
echo [CONFIG] Target:  %TARGET_NAME%
echo.

REM ============================================
REM  Step 1: Sync plugin to project
REM ============================================
echo ================================================
echo   [1/4] Syncing plugin to project...
echo ================================================

set "PLUGIN_SRC=%~dp0plugin\UEAgentBridge"
set "PLUGIN_DST=%PROJECT_DIR%Plugins\UEAgentBridge"

if not exist "%PLUGIN_SRC%" (
    echo [ERROR] Plugin source missing: %PLUGIN_SRC%
    exit /b 1
)

if not exist "%PLUGIN_DST%" mkdir "%PLUGIN_DST%"
xcopy "%PLUGIN_SRC%\*" "%PLUGIN_DST%\" /E /Y /Q /I >nul 2>&1
if !ERRORLEVEL! neq 0 (
    copy /Y "%PLUGIN_SRC%\UEAgentBridge.uplugin" "%PLUGIN_DST%\" >nul 2>&1
)
echo [OK] Plugin synced

REM ============================================
REM  Step 2: Compile C++ plugin
REM ============================================
echo.
echo ================================================
echo   [2/4] Compiling C++ plugin...
echo ================================================

tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor" >nul
if !ERRORLEVEL! equ 0 (
    echo [WARNING] UnrealEditor.exe is running!
    echo Please close UE Editor first, then press any key to retry...
    pause >nul
    tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor" >nul
    if !ERRORLEVEL! equ 0 (
        echo [ERROR] Editor still running. Aborting.
        exit /b 1
    )
)

REM Clean intermediate for fresh build
if exist "%PLUGIN_DST%\Intermediate" (
    rmdir /S /Q "%PLUGIN_DST%\Intermediate"
    echo [INFO] Cleaned Intermediate (fresh build)
)

set "UBT_DLL=%UE_ENGINE_PATH%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
if not exist "%UBT_DLL%" (
    echo [ERROR] UnrealBuildTool not found: %UBT_DLL%
    exit /b 1
)

echo [INFO] Building %TARGET_NAME%...
call dotnet "%UBT_DLL%" %TARGET_NAME% Win64 Development "%UE_PROJECT_PATH%"
set BUILD_RESULT=%ERRORLEVEL%

if %BUILD_RESULT% neq 0 (
    echo.
    echo [ERROR] C++ build failed (exit code %BUILD_RESULT%)
    echo Check: %LocalAppData%\UnrealBuildTool\Log.txt
    exit /b %BUILD_RESULT%
)
echo [OK] C++ build successful

REM ============================================
REM  Step 3: npm install + tsc
REM ============================================
echo.
echo ================================================
echo   [3/4] Building TypeScript server...
echo ================================================

pushd "%~dp0server"

echo [INFO] npm install...
call npm install
if !ERRORLEVEL! neq 0 (
    echo [ERROR] npm install failed
    popd
    exit /b 1
)

echo [INFO] tsc compile...
call npx tsc
if !ERRORLEVEL! neq 0 (
    echo [ERROR] TypeScript compile failed
    popd
    exit /b 1
)
echo [OK] TypeScript build successful
popd

REM ============================================
REM  Step 4: Generate MCP config
REM ============================================
echo.
echo ================================================
echo   [4/4] Generating MCP config...
echo ================================================

set "SERVER_JS=%~dp0server\dist\index.js"

REM Normalize path: backslash to forward slash
set "SERVER_PATH=%SERVER_JS:\=/%"

REM Output JSON config file
(
echo {
echo   "ue-agent-bridge": {
echo     "command": "node",
echo     "args": ["%SERVER_PATH%"],
echo     "disabled": false
echo   }
echo }
) > "%~dp0mcp-config.json"

echo [OK] mcp-config.json generated
echo.
echo   Copy this into your MCP client config ^(~/.workbuddy/mcp.json^):
echo.
type "%~dp0mcp-config.json"

REM ============================================
REM  Done
REM ============================================
echo.
echo ================================================
echo   INSTALL COMPLETE
echo ================================================
echo.
echo   Next steps:
echo   1. Copy mcp-config.json section into ~/.workbuddy/mcp.json
echo   2. Open UE project: %UE_PROJECT_PATH%
echo   3. Enable plugin: Edit ^> Plugins ^> "UE Agent Bridge"
echo   4. Restart your MCP client and test: ue_ping
echo.
pause
