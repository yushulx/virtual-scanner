@echo off
rem ---------------------------------------------------------------------------
rem build_and_install.cmd - double-click friendly wrapper around
rem scripts\build_and_install.ps1 (it asks for administrator rights itself).
rem
rem   build_and_install.cmd                      build + deploy (64 bit)
rem   build_and_install.cmd -UsePrebuilt         deploy the shipped release
rem   build_and_install.cmd -Platform All         build 32 and 64 bit
rem   build_and_install.cmd -Uninstall            remove the data source
rem ---------------------------------------------------------------------------
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_and_install.ps1" %*
set "CODE=%ERRORLEVEL%"

if not "%CODE%"=="0" (
    echo.
    echo error: the deployment failed ^(exit code %CODE%^), see the output above.
    pause
    exit /b %CODE%
)

if "%~1"=="" pause
exit /b 0
