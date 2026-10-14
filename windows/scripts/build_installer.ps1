<#
.SYNOPSIS
    Builds the Windows packages: the Inno Setup installer and a portable ZIP.

.DESCRIPTION
    Everything needed for distribution:

      dist\VirtualScannerSetup-<version>.exe           the installer (admin
                                                       rights, deploys the
                                                       64/32 bit data source,
                                                       the sample images and
                                                       configures them)
      dist\VirtualScanner-<version>-win64-portable.zip everything in one folder
                                                       plus configure-samples.cmd
      dist\stage\                                      the staged files

    The data source is compiled from source (Visual Studio + Qt 5 required);
    staging and packaging never write into C:\Windows, so no administrator
    rights are needed.

    Requires Inno Setup 6 (iscc.exe) for the installer; pass -SkipInstaller to
    build only the portable ZIP.

.PARAMETER Version
    Version stamped into the installer and the file names (default 1.0.0).

.PARAMETER Platform
    x64 (default), Win32 or All. "All" needs both a 32 bit and a 64 bit Qt kit.

.PARAMETER QTDir
    Qt kit directory passed through to build_and_install.ps1.

.PARAMETER IsccPath
    Full path of ISCC.exe when it cannot be found automatically.

.PARAMETER SkipInstaller
    Do not run Inno Setup, only create the portable ZIP.

.PARAMETER SkipPortable
    Do not create the portable ZIP.

.EXAMPLE
    .\scripts\build_installer.ps1 -Version 1.0.0

.EXAMPLE
    .\scripts\build_installer.ps1 -Version 1.1.0 -Platform All
#>
[CmdletBinding()]
param(
    [string] $Version = '1.0.0',

    [ValidateSet('x64', 'Win32', 'All')]
    [string] $Platform = 'x64',

    [string] $QTDir = '',
    [string] $IsccPath = '',

    [switch] $SkipInstaller,
    [switch] $SkipPortable
)

$ErrorActionPreference = 'Stop'

$ScriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$WindowsDir   = Split-Path -Parent $ScriptDir
$InstallerDir = Join-Path $WindowsDir 'installer'
$DistDir      = Join-Path $WindowsDir 'dist'
$StageDir     = Join-Path $DistDir 'stage'

function Write-Step { param([string] $Message) Write-Host ""; Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Info { param([string] $Message) Write-Host "    $Message" }
function Write-WarnMsg { param([string] $Message) Write-Host "warning: $Message" -ForegroundColor Yellow }
function Fail { param([string] $Message) Write-Host "error: $Message" -ForegroundColor Red; exit 1 }

function Find-Iscc {
    $candidates = @()
    if ($IsccPath) { $candidates += $IsccPath }

    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }

    $candidates += @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    return $null
}

# ---------------------------------------------------------------------------
# 1. Stage the deployment
# ---------------------------------------------------------------------------

if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Force -Path $DistDir | Out-Null }

$stagingArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $ScriptDir 'build_and_install.ps1'),
    '-StageDir', $StageDir,
    '-Platform', $Platform
)
if ($QTDir) { $stagingArguments += @('-QTDir', $QTDir) }

Write-Step "Staging the deployment in $StageDir"
& powershell.exe @stagingArguments
if ($LASTEXITCODE -ne 0) { Fail "staging failed (exit code $LASTEXITCODE)" }

# The portable ZIP is a drop-in replacement for C:\Windows\twain_64\sample2: the
# data source plus its "images" folder with the sample pictures.
if (-not $SkipPortable) {
    Write-Step "Creating the portable ZIP"
    $sourceDir = Join-Path $StageDir 'win64'
    if (-not (Test-Path $sourceDir)) { Fail "no 64 bit data source was staged in $sourceDir" }

    $portableDir = Join-Path $DistDir 'portable'
    if (Test-Path $portableDir) { Remove-Item $portableDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $portableDir | Out-Null

    Copy-Item (Join-Path $sourceDir '*') $portableDir -Recurse -Force
    if (Test-Path (Join-Path $StageDir 'images')) {
        Copy-Item (Join-Path $StageDir 'images') (Join-Path $portableDir 'images') -Recurse -Force
    }
    Copy-Item (Join-Path $InstallerDir 'PORTABLE-README.txt') $portableDir -Force

    $portableZip = Join-Path $DistDir "VirtualScanner-$Version-win64-portable.zip"
    if (Test-Path $portableZip) { Remove-Item $portableZip -Force }
    Compress-Archive -Path (Join-Path $portableDir '*') -DestinationPath $portableZip -CompressionLevel Optimal
    Remove-Item $portableDir -Recurse -Force

    Write-Info $portableZip
}

# ---------------------------------------------------------------------------
# 2. Build the installer
# ---------------------------------------------------------------------------

$installerPath = Join-Path $DistDir "VirtualScannerSetup-$Version.exe"

if ($SkipInstaller) {
    Write-WarnMsg "-SkipInstaller was given: no setup executable was built"
}
else {
    $iscc = Find-Iscc
    if (-not $iscc) {
        Fail "ISCC.exe (Inno Setup 6) was not found. Install it with 'winget install JRSoftware.InnoSetup' or 'choco install innosetup', pass -IsccPath, or use -SkipInstaller."
    }

    Write-Step "Building the installer with $iscc"
    if (Test-Path $installerPath) { Remove-Item $installerPath -Force }

    $includeWin32 = if (Test-Path (Join-Path $StageDir 'win32')) { 1 } else { 0 }
    if ($includeWin32 -eq 0) { Write-Info "no 32 bit data source staged: the installer offers the 64 bit one only" }

    & $iscc "/DVersion=$Version" "/DStageDir=$StageDir" "/DIncludeWin32=$includeWin32" (Join-Path $InstallerDir 'VirtualScanner.iss')
    if ($LASTEXITCODE -ne 0) { Fail "the installer build failed (exit code $LASTEXITCODE)" }
}

# ---------------------------------------------------------------------------
# 3. Summary
# ---------------------------------------------------------------------------

Write-Step "Packages"

foreach ($file in @($installerPath, (Join-Path $DistDir "VirtualScanner-$Version-win64-portable.zip"))) {
    if (Test-Path $file) {
        $size = [math]::Round((Get-Item $file).Length / 1MB, 1)
        Write-Host ("    {0}  ({1} MB)" -f $file, $size)
    }
}

Write-Host ""
Write-Host "Install with:  $installerPath" -ForegroundColor Cyan
Write-Host "Uninstall with: 'Virtual Scanner (TWAIN Data Source)' in Apps & features,"
Write-Host "                or: powershell -ExecutionPolicy Bypass -File .\scripts\build_and_install.ps1 -Uninstall"
