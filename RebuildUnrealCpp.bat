@echo off
setlocal

set "REPO_ROOT=%~dp0"
set "UE_ROOT=C:\GameDev\UE_5.8"
set "PROJECT_FILE=%REPO_ROOT%SimCopterRemake\SimCopterRemake.uproject"
set "BUILD_SCRIPT=%UE_ROOT%\Engine\Build\BatchFiles\Build.bat"
set "BUILD_TARGET=SimCopterRemakeEditor"
set "BUILD_CONFIG=Development"

rem Optional Shipping build verifies the game target used by packaging.
if /I "%~1"=="Shipping" (
    set "BUILD_TARGET=SimCopterRemake"
    set "BUILD_CONFIG=Shipping"
) else if not "%~1"=="" (
    echo Usage: RebuildUnrealCpp.bat [Shipping]
    set "EXIT_CODE=1"
    goto :finish
)

if not exist "%BUILD_SCRIPT%" (
    echo Unreal Build.bat was not found:
    echo   %BUILD_SCRIPT%
    set "EXIT_CODE=1"
    goto :finish
)

if not exist "%PROJECT_FILE%" (
    echo Unreal project file was not found:
    echo   %PROJECT_FILE%
    set "EXIT_CODE=1"
    goto :finish
)

call "%BUILD_SCRIPT%" %BUILD_TARGET% Win64 %BUILD_CONFIG% -Project="%PROJECT_FILE%" -WaitMutex -NoLiveCoding
set "EXIT_CODE=%ERRORLEVEL%"

:finish
echo.
if "%EXIT_CODE%"=="0" (
    echo Build completed successfully.
) else (
    echo Build failed with exit code %EXIT_CODE%.
)
pause
exit /b %EXIT_CODE%

pause
