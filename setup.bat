@echo off
echo ========================================
echo UE Agent Bridge - TS Setup
echo ========================================
echo.
echo For full install (C++ + TS + MCP config):
echo   Run install.bat instead.
echo.
echo This script: npm install + tsc only.
echo.

pushd "%~dp0server"

echo [1/2] Installing npm dependencies...
call npm install
if %ERRORLEVEL% neq 0 (
    echo [ERROR] npm install failed
    popd
    pause
    exit /b 1
)
echo [OK] npm install done

echo.
echo [2/2] Building TypeScript...
call npx tsc
if %ERRORLEVEL% neq 0 (
    echo [ERROR] tsc build failed
    popd
    pause
    exit /b 1
)
echo [OK] tsc build done

popd
echo.
echo [DONE] TypeScript server ready.
echo Run: node server/dist/index.js
echo.
pause
