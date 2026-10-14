<#
.SYNOPSIS
    Builds and installs the Windows TWAIN virtual scanner with one command.

.DESCRIPTION
    Compiles TWAIN-Samples\Twain_DS_sample01 for the requested platform(s) and
    deploys the data source where TWAIN applications look for it:

        C:\Windows\twain_64\sample2   (64 bit data source)
        C:\Windows\twain_32\sample2   (32 bit data source)

    Administrator rights are required for that deployment, so the script
    re-launches itself with elevated rights (UAC) unless -NoElevate is given.

    Each deployment consists of

      TWAINDS_Sample64.ds   the virtual scanner
      FreeImage.dll         its imaging library
      TWAIN_logo.png        fallback picture (single image mode)
      images\               the 11 bundled sample pictures
      Qt5*.dll, platforms\  the Qt runtime, deployed with windeployqt

    The data source looks for pictures in its own "images" folder, so it cycles
    through the bundled samples right after the installation - no configuration
    file is needed. An optional source.json (pointing at another folder) and an
    optional info.json (index / maxcount) still work; see windows\README.md.

.PARAMETER Configuration
    Release (default) or Debug.

.PARAMETER Platform
    x64 (default), Win32 or All. "All" needs a 32 bit and a 64 bit Qt kit.

.PARAMETER QTDir
    The Qt kit directory (the one containing bin\windeployqt.exe). Auto
    detected from the QTDIR environment variable and C:\Qt\* when omitted.

.PARAMETER PlatformToolset
    Override the MSVC toolset (v141, v142, v143, ...). Auto detected.

.PARAMETER WindowsSdkVersion
    Override the Windows SDK version. The newest installed one is used.

.PARAMETER StageDir
    Do not touch C:\Windows but assemble the deployment folder(s) in this
    directory instead (win64\ , win32\). Used to build the installer and the
    portable ZIP; no administrator rights are needed for this mode.

.PARAMETER InstallSamples
    Copy the bundled sample pictures into the "images" folder of the
    deployment (default: true). Pass -InstallSamples:$false to keep the
    data source without pictures (it then falls back to TWAIN_logo.png).

.PARAMETER Uninstall
    Remove the data source folder(s) from C:\Windows.

.PARAMETER AutoInstallQt
    Download and install Qt 5.12.11 with aqtinstall when it is missing.

.PARAMETER NoElevate
    Fail instead of asking for administrator rights.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\build_and_install.ps1

.EXAMPLE
    # build everything a package needs, without touching C:\Windows
    .\scripts\build_and_install.ps1 -StageDir .\dist\stage

.EXAMPLE
    # 32 and 64 bit, and remove it again afterwards
    .\scripts\build_and_install.ps1 -Platform All
    .\scripts\build_and_install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',

    [ValidateSet('x64', 'Win32', 'All')]
    [string] $Platform = 'x64',

    [string] $QTDir,
    [string] $PlatformToolset = '',
    [string] $WindowsSdkVersion = '',
    [string] $StageDir = '',

    [bool] $InstallSamples = $true,

    [switch] $Uninstall,
    [switch] $AutoInstallQt,
    [switch] $NoElevate
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Paths and helpers
# ---------------------------------------------------------------------------

$ScriptDir          = Split-Path -Parent $MyInvocation.MyCommand.Path
$WindowsDir         = Split-Path -Parent $ScriptDir
$RepoRoot           = Split-Path -Parent $WindowsDir
$ProjectDir         = Join-Path $WindowsDir 'TWAIN-Samples\Twain_DS_sample01'
$SolutionPath       = Join-Path $ProjectDir 'visual_studio\TWAINDS_VS2017.sln'
$PublicDir          = Join-Path $WindowsDir 'TWAIN-Samples\pub'
$SampleImagesSource = Join-Path $RepoRoot 'macos\Resources\SampleImages'

if ($StageDir) {
    $StageDir = [System.IO.Path]::GetFullPath($StageDir)
}

function Write-Step { param([string] $Message) Write-Host ""; Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Info { param([string] $Message) Write-Host "    $Message" }
function Write-Note { param([string] $Message) Write-Host "note: $Message" -ForegroundColor DarkGray }
function Write-WarnMsg { param([string] $Message) Write-Host "warning: $Message" -ForegroundColor Yellow }
function Fail { param([string] $Message) Write-Host "error: $Message" -ForegroundColor Red; exit 1 }

function Test-IsAdmin {
    $identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-DeployDir {
    param([string] $TargetPlatform)

    if ($TargetPlatform -eq 'x64') { return (Join-Path $env:SystemRoot 'twain_64\sample2') }
    return (Join-Path $env:SystemRoot 'twain_32\sample2')
}

function Remove-Deployment {
    param([string] $Directory)

    if (-not (Test-Path $Directory)) { return }

    try {
        Remove-Item $Directory -Recurse -Force -ErrorAction Stop
    }
    catch {
        Write-WarnMsg "could not completely clear $Directory (a TWAIN application may still hold the files); overwriting what is possible"
        Remove-Item (Join-Path $Directory '*') -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ---------------------------------------------------------------------------
# Elevation (the data source lives under C:\Windows)
# ---------------------------------------------------------------------------

$needsAdmin = -not $StageDir

if ($needsAdmin -and -not (Test-IsAdmin)) {
    if ($NoElevate) {
        Fail "administrator rights are required to deploy into $env:SystemRoot. Open an elevated PowerShell and run this script again (or use -StageDir to only prepare a package)."
    }

    Write-Host "Administrator rights are required: the scanner is deployed to $env:SystemRoot\twain_64\sample2." -ForegroundColor Yellow
    Write-Host "Relaunching elevated - please accept the UAC prompt..." -ForegroundColor Yellow

    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    foreach ($key in $PSBoundParameters.Keys) {
        $value = $PSBoundParameters[$key]
        if ($value -is [switch]) {
            if ($value.IsPresent) { $arguments += "-$key" }
        }
        elseif ($value -is [bool]) {
            $arguments += "-$key"
            $arguments += "`$$value"
        }
        else {
            $arguments += "-$key"
            $arguments += "`"$value`""
        }
    }

    $elevated = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    if ($elevated) { exit $elevated.ExitCode }
    exit 0
}

# ---------------------------------------------------------------------------
# Visual Studio / Qt discovery
# ---------------------------------------------------------------------------

function Get-VsWherePath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) { return $vswhere }
    return $null
}

function Get-MSBuildPath {
    $vswhere = Get-VsWherePath
    if ($vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe'
        if ($LASTEXITCODE -eq 0 -and $found) { return @($found)[0] }
    }

    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $patterns = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2019\*\MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2019\*\MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2017\*\MSBuild\15.0\Bin\MSBuild.exe')
    )
    foreach ($pattern in $patterns) {
        $hit = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

function Get-InstalledToolsets {
    $vswhere = Get-VsWherePath
    if (-not $vswhere) { return @() }

    $installations = & $vswhere -latest -products * -property installationPath
    $toolsets = @()
    foreach ($installation in @($installations)) {
        $msvcDir = Join-Path $installation 'VC\Tools\MSVC'
        if (-not (Test-Path $msvcDir)) { continue }
        foreach ($directory in Get-ChildItem $msvcDir -Directory -ErrorAction SilentlyContinue) {
            if ($directory.Name -like '14.1*')      { $toolsets += 'v141' }
            elseif ($directory.Name -like '14.2*')  { $toolsets += 'v142' }
            elseif ($directory.Name -like '14.3*')  { $toolsets += 'v143' }
            elseif ($directory.Name -like '14.4*')  { $toolsets += 'v143' }
        }
    }
    return @($toolsets | Select-Object -Unique)
}

function Get-LatestWindowsSdk {
    $root = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
    if (-not $root) { return $null }

    $include = Join-Path $root 'Include'
    if (-not (Test-Path $include)) { return $null }

    $versions = Get-ChildItem $include -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^10\.' } |
                Sort-Object Name -Descending
    if (-not $versions) { return $null }
    return @($versions)[0].Name
}

function Test-QtKit {
    param([string] $Path, [string] $TargetPlatform)

    if (-not $Path -or -not (Test-Path $Path)) { return $false }
    if (-not (Test-Path (Join-Path $Path 'bin\windeployqt.exe'))) { return $false }
    if (-not (Test-Path (Join-Path $Path 'lib\Qt5Core.lib'))) { return $false }   # MSVC kit, not MinGW

    $name = Split-Path -Leaf $Path
    $is64 = $name -match '_64$'
    if ($TargetPlatform -eq 'x64' -and -not $is64) { return $false }
    if ($TargetPlatform -eq 'Win32' -and $is64) { return $false }
    return $true
}

function Find-QtKit {
    param([string] $TargetPlatform, [string] $Explicit)

    $roots = @()

    if ($Explicit) {
        if (Test-QtKit -Path $Explicit -TargetPlatform $TargetPlatform) { return $Explicit }
        # -QTDir may also point at a Qt root (C:\Qt) or a version directory
        # (C:\Qt\5.12.11) instead of the kit itself.
        $roots += $Explicit
    }

    if ($env:QTDIR -and (Test-QtKit -Path $env:QTDIR -TargetPlatform $TargetPlatform)) { return $env:QTDIR }

    $roots += @((Join-Path $env:SystemDrive 'Qt'), (Join-Path $env:SystemDrive 'Qt5'), 'C:\Qt', 'D:\Qt')
    $roots = @($roots | Where-Object { $_ } | Select-Object -Unique)

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }

        # <root>\<5.x.y>\<kit>, e.g. C:\Qt\5.12.11\msvc2017_64
        $kits = Get-ChildItem -Path (Join-Path $root '*\*') -Directory -ErrorAction SilentlyContinue |
                Where-Object { (Split-Path -Leaf (Split-Path -Parent $_.FullName)) -like '5.*' } |
                Sort-Object FullName -Descending
        foreach ($kit in $kits) {
            if (Test-QtKit -Path $kit.FullName -TargetPlatform $TargetPlatform) { return $kit.FullName }
        }

        # <root>\<kit> (root already is the version directory)
        foreach ($kit in @(Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue)) {
            if (Test-QtKit -Path $kit.FullName -TargetPlatform $TargetPlatform) { return $kit.FullName }
        }
    }

    return $null
}

function Install-QtKit {
    param([string] $TargetPlatform)

    $kitName = if ($TargetPlatform -eq 'x64') { 'win64_msvc2017_64' } else { 'win32_msvc2017' }
    $python  = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command python3.exe -ErrorAction SilentlyContinue }
    if (-not $python) {
        Fail "the automatic Qt download needs Python. Install Python 3 (https://www.python.org/downloads/) or install Qt 5.12.11 manually from https://download.qt.io/archive/qt/5.12/5.12.11/"
    }

    $destination = Join-Path $env:SystemDrive 'Qt'
    Write-Step "Downloading Qt 5.12.11 ($kitName) - about 1 GB, this can take a while"
    & $python.Source -m pip install --user --quiet --disable-pip-version-check aqtinstall
    & $python.Source -m aqt install-qt windows desktop 5.12.11 $kitName -O $destination
    if ($LASTEXITCODE -ne 0) { Fail "the Qt download failed; install Qt 5.12.11 manually and pass -QTDir" }

    return (Find-QtKit -TargetPlatform $TargetPlatform -Explicit '')
}

function Test-TwainDataSourceManager {
    $candidates = @(
        (Join-Path $env:SystemRoot 'System32\twaindsm.dll'),
        (Join-Path $env:SystemRoot 'System32\twain_32.dll'),
        (Join-Path $env:SystemRoot 'twain_32.dll'),
        (Join-Path $env:SystemRoot 'SysWOW64\twaindsm.dll')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $true }
    }
    return $false
}

# ---------------------------------------------------------------------------
# Building and publishing
# ---------------------------------------------------------------------------

function Get-BuiltDataSource {
    param([string] $TargetPlatform)

    $name = if ($TargetPlatform -eq 'x64') { 'TWAINDS_Sample64.ds' } else { 'TWAINDS_Sample32.ds' }
    $found = Get-ChildItem -Path $ProjectDir -Filter $name -File -Recurse -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending |
             Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

function Invoke-WindeployQt {
    param([string] $Kit, [string] $TargetFile)

    if (-not (Test-Path $TargetFile)) { return }

    $windeployqt = Join-Path $Kit 'bin\windeployqt.exe'
    if (-not (Test-Path $windeployqt)) {
        Write-WarnMsg "windeployqt.exe is missing in $Kit - the Qt DLLs will not be part of the deployment"
        return
    }

    $directory = Split-Path -Parent $TargetFile
    $mode      = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }

    Write-Info "deploying the Qt runtime with windeployqt"
    & $windeployqt $mode '--compiler-runtime' '--libdir' $directory '--plugindir' $directory $TargetFile |
        Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-WarnMsg "windeployqt reported an error (exit code $LASTEXITCODE)"
    }
}

function Copy-SampleImages {
    param([string] $Destination)

    if (-not (Test-Path $SampleImagesSource)) {
        Write-WarnMsg "sample images not found: $SampleImagesSource"
        return
    }

    if (Test-Path $Destination) { Remove-Item $Destination -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item (Join-Path $SampleImagesSource '*') $Destination -Recurse -Force

    $count = @(Get-ChildItem $Destination -File -ErrorAction SilentlyContinue |
               Where-Object { $_.Extension -match '^\.(png|jpg|jpeg)$' }).Count
    Write-Info "$count sample image(s) in $Destination"
}

# Copies the data source, its dependencies and the Qt runtime into
# $Destination, which becomes the content of the "sample2" folder.
function Publish-DataSource {
    param([hashtable] $Build, [string] $Destination, [bool] $WithSamples)

    $suffix = if ($Build.Platform -eq 'x64') { 'win64' } else { 'win32' }

    Remove-Deployment $Destination
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    Copy-Item $Build.DsPath $Destination -Force
    Copy-Item (Join-Path $PublicDir "external\bin\$suffix\FreeImage.dll") $Destination -Force
    Copy-Item (Join-Path $PublicDir 'bin\TWAIN_logo.png') $Destination -Force

    Invoke-WindeployQt -Kit $Build.Kit -TargetFile (Join-Path $Destination (Split-Path -Leaf $Build.DsPath))

    if ($WithSamples) {
        Copy-SampleImages -Destination (Join-Path $Destination 'images')
    }
}

# ---------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------

if ($Uninstall) {
    Write-Step "Removing the virtual scanner"
    foreach ($targetPlatform in @('x64', 'Win32')) {
        $directory = Get-DeployDir -TargetPlatform $targetPlatform
        if (Test-Path $directory) {
            Remove-Item $directory -Recurse -Force
            Write-Info "removed $directory"
        }
    }
    Write-Step "Done"
    exit 0
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

$platforms = if ($Platform -eq 'All') { @('Win32', 'x64') } else { @($Platform) }

if (-not (Test-Path $SolutionPath)) { Fail "the solution was not found: $SolutionPath" }

$msbuild = Get-MSBuildPath
if (-not $msbuild) {
    Fail "MSBuild was not found. Install Visual Studio 2017 or newer with 'Desktop development with C++' (or the Build Tools), then run this script again."
}
Write-Info "MSBuild: $msbuild"

if (-not $PlatformToolset) {
    $available = Get-InstalledToolsets
    if ($available.Count -gt 0) { $PlatformToolset = @($available | Sort-Object)[-1] }
    Write-Info "MSVC toolset: $(if ($PlatformToolset) { $PlatformToolset } else { 'project default' })"
}
if (-not $WindowsSdkVersion) {
    $WindowsSdkVersion = Get-LatestWindowsSdk
    if ($WindowsSdkVersion) { Write-Info "Windows SDK: $WindowsSdkVersion" }
}

$builtDataSources = @()

foreach ($targetPlatform in $platforms) {
    $kit = Find-QtKit -TargetPlatform $targetPlatform -Explicit $QTDir
    if (-not $kit -and $AutoInstallQt) { $kit = Install-QtKit -TargetPlatform $targetPlatform }
    if (-not $kit) {
        Write-WarnMsg "no Qt 5 kit found for $targetPlatform; skipping it."
        Write-Note "install Qt 5.12.11 (msvc2017_64 / msvc2017) and set QTDIR, pass -QTDir <kit>, or use -AutoInstallQt."
        continue
    }

    Write-Step "Building $Configuration|$targetPlatform"
    Write-Info "Qt kit: $kit"

    $env:QTDIR = $kit
    $msbuildArguments = @(
        "`"$SolutionPath`"",
        '/nologo',
        '/v:m',
        '/m',
        "/p:Configuration=$Configuration",
        "/p:Platform=$targetPlatform"
    )
    if ($PlatformToolset)   { $msbuildArguments += "/p:PlatformToolset=$PlatformToolset" }
    if ($WindowsSdkVersion) { $msbuildArguments += "/p:WindowsTargetPlatformVersion=$WindowsSdkVersion" }

    # The data source is published by this script (see Publish-DataSource), so
    # the file copying build events of the project - which write straight into
    # C:\Windows - are disabled. That keeps packaging possible without
    # administrator rights and leaves the deployment in one place.
    $msbuildArguments += '/p:PreBuildEventUseInBuild=false'
    $msbuildArguments += '/p:PostBuildEventUseInBuild=false'

    & $msbuild @msbuildArguments
    if ($LASTEXITCODE -ne 0) { Fail "the build failed (exit code $LASTEXITCODE)" }

    $dsPath = Get-BuiltDataSource -TargetPlatform $targetPlatform
    if (-not $dsPath) { Fail "the build did not produce TWAINDS_Sample$(if ($targetPlatform -eq 'x64') { '64' } else { '32' }).ds" }

    $builtDataSources += @{ Platform = $targetPlatform; DsPath = $dsPath; Kit = $kit }
}

# ---------------------------------------------------------------------------
# Publish
# ---------------------------------------------------------------------------

$deployed = @()

foreach ($build in $builtDataSources) {
    $stageName   = if ($build.Platform -eq 'x64') { 'win64' } else { 'win32' }
    $destination = if ($StageDir) { Join-Path $StageDir $stageName } else { Get-DeployDir -TargetPlatform $build.Platform }

    # When staging for a package the sample pictures are kept in one shared
    # folder (<StageDir>\images) and copied into every data source by the
    # installer; when deploying they go straight next to the data source.
    Write-Step "Publishing the $($build.Platform) data source to $destination"
    Publish-DataSource -Build $build -Destination $destination -WithSamples ($InstallSamples -and -not $StageDir)

    $deployed += @{ Platform = $build.Platform; Directory = $destination }
}

if ($InstallSamples -and $StageDir) {
    Write-Step "Staging the sample pictures"
    Copy-SampleImages -Destination (Join-Path $StageDir 'images')
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

Write-Step "Done"

foreach ($entry in $deployed) {
    Write-Host "    $($entry.Platform) data source: $($entry.Directory)"
}
if ($deployed.Count -eq 0) {
    Write-WarnMsg "nothing was deployed"
    exit 1
}

Write-Host ""
Write-Host "Test it with:" -ForegroundColor Cyan
Write-Host "  * the Dynamic Web TWAIN online demo: https://demo.dynamsoft.com/web-twain/"
Write-Host "  * or twacker: https://github.com/twain/twain-toolkit/tree/master/releases"

if (-not (Test-TwainDataSourceManager)) {
    Write-Host ""
    Write-WarnMsg "no TWAIN Data Source Manager (twaindsm.dll / twain_32.dll) was found on this machine."
    Write-Note "TWAIN applications need one to see the virtual scanner; install it from"
    Write-Note "https://twain.org/dsm/ (or install any scanner driver / Dynamic Web TWAIN)."
}
