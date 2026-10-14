@echo off
rem ---------------------------------------------------------------------------
rem build_installer.cmd - double-click friendly wrapper around
rem scripts\build_installer.ps1, which produces
rem
rem   dist\VirtualScannerSetup-<version>.exe            the installer
rem   dist\VirtualScanner-<version>-win64-portable.zip  the portable ZIP
rem
rem   build_installer.cmd                       package (compiles when possible)
rem   build_installer.cmd -Version 1.2.0        with an explicit version
rem   build_installer.cmd -UsePrebuilt          without Visual Studio
rem ---------------------------------------------------------------------------
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_installer.ps1" %*
set "CODE=%ERRORLEVEL%"

if not "%CODE%"=="0" (
    echo.
    echo error: packaging failed ^(exit code %CODE%^), see the output above.
    pause
    exit /b %CODE%
)

if "%~1"=="" pause
exit /b 0
