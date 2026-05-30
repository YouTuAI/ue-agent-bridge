@echo off
echo Cleaning UBT logs...
echo.

set "UBT_LOG=%LocalAppData%\UnrealBuildTool"

if not exist "%UBT_LOG%" (
    echo No UBT log directory found.
    pause
    exit /b 0
)

echo Deleting all log files in: %UBT_LOG%
echo.

del /F /Q "%UBT_LOG%\Log-backup-*.txt" 2>nul
del /F /Q "%UBT_LOG%\Log-backup-*.uba" 2>nul
del /F /Q "%UBT_LOG%\Log.txt" 2>nul

echo Done! You can now run build.bat.
echo.
pause
