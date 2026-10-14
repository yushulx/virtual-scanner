; VirtualScanner.iss - Inno Setup 6 script for the Windows virtual scanner.
;
; It installs the TWAIN data source where Windows TWAIN applications look for
; it, together with the 11 sample pictures in the "images" folder next to it.
; The data source looks for pictures in that folder by default, so it cycles
; through them right after the installation - no configuration file needed.
;
; Build it with the Inno Setup command line compiler (iscc.exe), ideally
; through scripts\build_installer.ps1 which stages everything first:
;
;   iscc.exe /DVersion=1.0.0 /DStageDir=<abs path to stage> VirtualScanner.iss
;
; The staging directory (<StageDir>) is produced by
; scripts\build_and_install.ps1 -StageDir <dir> and contains:
;
;   win64\    64 bit data source + Qt/FreeImage runtime + images\  -> C:\Windows\twain_64\sample2
;   win32\    32 bit data source (optional)                        -> C:\Windows\twain_32\sample2
;   images\   the 11 sample pictures, shared by both platforms
;
; Requires Inno Setup 6 (https://jrsoftware.org/isinfo.php).

#ifndef Version
  #define Version "1.0.0"
#endif

#ifndef StageDir
  #define StageDir "..\dist\stage"
#endif

; Set to "0" (iscc /DIncludeWin32=0) when no 32 bit data source was staged:
; the component is then left out of the installer.
#ifndef IncludeWin32
  #define IncludeWin32 "1"
#endif

#define AppName        "Virtual Scanner (TWAIN Data Source)"
#define AppPublisher   "Dynamsoft"
#define AppUrl         "https://github.com/yushulx/virtual-scanner"

[Setup]
AppId={{8E4C6B2D-2F13-4A86-9C2E-2B1F9C6A5D31}
AppName={#AppName}
AppVersion={#Version}
AppVerName={#AppName} {#Version}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
DefaultDirName={win}\twain_64\sample2
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=VirtualScannerSetup-{#Version}
Compression=lzma2/max
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
WizardStyle=modern
UninstallDisplayName={#AppName} {#Version}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#Version}

[Types]
Name: "full";    Description: "Install the virtual scanner together with the 11 sample pictures (recommended)"
Name: "minimal"; Description: "Install the virtual scanner without the sample pictures"
Name: "custom";  Description: "Custom installation"; Flags: iscustom

[Components]
Name: "ds64";    Description: "64 bit virtual scanner (for 64 bit TWAIN applications)";                            Types: full minimal
#if IncludeWin32 == "1"
Name: "ds32";    Description: "32 bit virtual scanner (for 32 bit TWAIN applications)";                            Types: full minimal
#endif
Name: "samples"; Description: "Sample pictures (11 images; the scanner returns a different one on every scan)";   Types: full

[Files]
; 64 bit data source
Source: "{#StageDir}\win64\*";  DestDir: "{win}\twain_64\sample2"; Components: ds64; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace skipifsourcedoesntexist
; the sample pictures go into the "images" folder of every installed data source
Source: "{#StageDir}\images\*"; DestDir: "{win}\twain_64\sample2\images"; Components: samples and ds64; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; how to use it
Source: "PORTABLE-README.txt";  DestDir: "{win}\twain_64\sample2"; Components: ds64; Flags: ignoreversion

#if IncludeWin32 == "1"
; 32 bit data source
Source: "{#StageDir}\win32\*";  DestDir: "{win}\twain_32\sample2"; Components: ds32; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace skipifsourcedoesntexist
Source: "{#StageDir}\images\*"; DestDir: "{win}\twain_32\sample2\images"; Components: samples and ds32; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "PORTABLE-README.txt";  DestDir: "{win}\twain_32\sample2"; Components: ds32; Flags: ignoreversion
#endif

[UninstallDelete]
Type: filesandordirs; Name: "{win}\twain_64\sample2"; Components: ds64
#if IncludeWin32 == "1"
Type: filesandordirs; Name: "{win}\twain_32\sample2"; Components: ds32
#endif

[Code]
function DataSourceManagerInstalled: Boolean;
begin
  Result := FileExists(ExpandConstant('{sys}\twaindsm.dll')) or
            FileExists(ExpandConstant('{sys}\twain_32.dll')) or
            FileExists(ExpandConstant('{win}\twain_32.dll'));
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID <> wpFinished then
    Exit;

  if not DataSourceManagerInstalled then
    MsgBox('No TWAIN Data Source Manager (twaindsm.dll / twain_32.dll) was found on this computer.' + #13#10#13#10 +
           'TWAIN applications need one before they can see the virtual scanner. ' +
           'Install one (for example from https://twain.org/dsm/) or install Dynamic Web TWAIN, ' +
           'then restart the application.', mbInformation, MB_OK);
end;
