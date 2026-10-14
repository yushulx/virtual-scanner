# Windows Virtual Scanner

A virtual (software only) TWAIN data source for Windows, ported from
[twain-samples](https://github.com/twain/twain-samples). It appears in TWAIN
applications (twacker, Dynamic Web TWAIN, ...) like a real scanner, but instead
of reading from hardware it returns the picture files of a folder - one picture
per scan, cycling through the whole folder.

The pictures live in the `images` folder **next to the data source**, which the
installer fills with the 11 sample pictures of this repository. Scanned
pictures are therefore different on every scan right after the installation,
with no configuration file involved.

## Quick start (one command)

Right click `build_and_install.cmd` (or run it from a console) - it compiles
the data source, deploys it to `C:\Windows\twain_64\sample2`, copies the sample
pictures into `images\` and tells you what to do next:

```bat
build_and_install.cmd
```

The script asks for administrator rights (a UAC prompt), because the data
source has to be copied into `C:\Windows`. Everything is done by
`scripts\build_and_install.ps1`, which can also be called directly:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_and_install.ps1
```

| Option | Meaning |
| --- | --- |
| `-Platform x64` | 64 bit data source (default); `Win32` or `All` for 32 bit / both |
| `-Configuration Release` | `Release` (default) or `Debug` |
| `-QTDir <dir>` | Qt kit to use (the folder containing `bin\windeployqt.exe`) |
| `-AutoInstallQt` | download Qt 5.12.11 (~1 GB) when it is missing |
| `-PlatformToolset v143` | force a MSVC toolset; auto detected otherwise |
| `-WindowsSdkVersion 10.0.22621.0` | force a Windows SDK; newest installed otherwise |
| `-StageDir <dir>` | don't touch `C:\Windows`, assemble a package instead (no admin needed) |
| `-InstallSamples:$false` | deploy without pictures (the scanner then returns `TWAIN_logo.png`) |
| `-Uninstall` | remove `C:\Windows\twain_64\sample2` (and `twain_32\sample2`) |

### Pre-requisites

* **Visual Studio 2017 or newer** with the *Desktop development with C++*
  workload (the standalone *Build Tools* are enough). The script detects the
  installed toolset and Windows SDK automatically.
* **Qt 5.12.11, msvc2017_64** (32 bit: `msvc2017`). Either
  * set the `QTDIR` environment variable to the kit directory
    (`C:\Qt\5.12.11\msvc2017_64`), or
  * pass `-QTDir C:\Qt\5.12.11\msvc2017_64`, or
  * let the script find it under `C:\Qt\5.12.11\msvc2017_64`, or
  * pass `-AutoInstallQt` to download it (`python -m pip install aqtinstall`).

Qt 6 does not work: the project uses the Qt 5 APIs.

### What gets deployed

```
C:\Windows\twain_64\sample2\
    TWAINDS_Sample64.ds     the virtual scanner
    FreeImage.dll           image decoding
    TWAIN_logo.png          fallback picture (single picture mode)
    images\                 the 11 sample pictures (1.png ... 11.png)
    Qt5*.dll, platforms\, imageformats\, ...   the Qt runtime (windeployqt)
C:\Windows\twain_32\sample2\   the 32 bit data source, when -Platform Win32/All
```

## How the pictures are chosen

1. If `source.json` exists next to the data source, its `folder` entry is used:

   ```json
   { "folder": "C:/Users/me/Pictures/my-scans" }
   ```

   (`source.json` is optional - it is only needed to use pictures from
   somewhere else.)

2. Otherwise the `images` folder next to the data source is used
   (`C:\Windows\twain_64\sample2\images`).
3. If there is no `images` folder either, the single picture
   `TWAIN_logo.png` is returned.

The pictures are sorted in natural order (`2.png` before `10.png`), and one
scan behaves like this:

* **Flatbed**: one picture per scan job; the next scan returns the next picture
  (wrapping around after the last one).
* **Document feeder (ADF)**: one page per picture, so checking *Use ADF* in the
  online demo gives you all 11 pictures as a single multi page document.

The position inside the folder is remembered between scans:

* in `<picture folder>\info.json` when that file exists:

  ```json
  { "index": 0, "maxcount": 11 }
  ```

  `index` is the picture of the next scan, `maxcount` the number of pages of
  one ADF job. The file is updated after every scan, which needs write
  permission for the folder (this is how custom picture folders were
  configured before).
* otherwise (the shipped `images` folder lives in `C:\Windows` and is not
  writable for normal users) in
  `%LOCALAPPDATA%\VirtualScanner\info.json`, per user.

To always get the same picture, set `index` in a writable `info.json`, or point
`source.json` at a folder that holds a single picture.

## Using your own pictures

1. Delete what you don't need in `C:\Windows\twain_64\sample2\images` and copy
   your own `.png`/`.jpg` files in there (an administrator prompt is needed for
   that folder).
2. Restart the TWAIN application: the folder is read when a scan starts.

Or copy your pictures somewhere writable and point `source.json` at that
folder - then no elevation is needed, and `info.json` (if you create one) is
updated in place.

If the folder contains more than `maxcount` pictures, an ADF job returns
`maxcount` pages starting at `index`; if it contains fewer, `maxcount` is
lowered to the number of pictures.

## Packaging (installer and portable ZIP)

```bat
build_installer.cmd -Version 1.0.0
```

or

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_installer.ps1 -Version 1.0.0
```

produces

| File | Purpose |
| --- | --- |
| `dist\VirtualScannerSetup-1.0.0.exe` | Inno Setup installer: 64 bit (and optionally 32 bit) data source + sample pictures; asks for administrator rights |
| `dist\VirtualScanner-1.0.0-win64-portable.zip` | the same content as a ZIP, to be unpacked into `C:\Windows\twain_64\sample2` |
| `dist\stage\` | the staged files the two packages are built from |

Needs [Inno Setup 6](https://jrsoftware.org/isinfo.php)
(`winget install JRSoftware.InnoSetup` or `choco install innosetup`) and,
unlike a plain installation, **no administrator rights**: nothing is written to
`C:\Windows`, the packages are assembled in `windows\dist`.

The same packages are built by the GitHub Actions workflow
`.github/workflows/build-installers.yml` and attached to a release when a
`v*` tag is pushed.

Uninstall the installer with *Apps & features* or

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_and_install.ps1 -Uninstall
```

## Building in Visual Studio by hand

If you prefer the IDE:

1. Set the `QTDIR` environment variable to the Qt kit (`msvc2017_64`).
2. Start Visual Studio **as administrator** (the project copies the data source
   into `C:\Windows` in its build events).
3. Open `TWAIN-Samples\Twain_DS_sample01\visual_studio\TWAINDS_VS2017.sln` and
   build it.

`build_and_install.ps1` disables those build events
(`/p:PreBuildEventUseInBuild=false /p:PostBuildEventUseInBuild=false`) and
publishes the deployment itself, which is why it can also build a package
without touching `C:\Windows`.

## How to test

* [Dynamic Web TWAIN online demo](https://demo.dynamsoft.com/web-twain/) -
  select *Virtual Scanner* (the 64 bit data source) and scan.
* [twacker](https://github.com/twain/twain-toolkit/tree/master/releases).

Both need a **TWAIN Data Source Manager** (`twaindsm.dll` or `twain_32.dll`) on
the machine; every scanner driver and Dynamic Web TWAIN install one. Without it
TWAIN applications cannot enumerate data sources at all
(https://twain.org/dsm/). The installer and the PowerShell script check for it
and say so when it is missing.

## How to debug

1. Run twacker.
2. In Visual Studio, set a breakpoint in `DS_Entry()` and use
   `Debug > Attach to Process` to attach to `twacker.exe`.
3. Scan to hit the breakpoint.

The data source writes diagnostic messages to `stderr` when the environment
variable `TWAIN_SHOWCONSOLEMESSAGES=1` is set (which picture folder was picked,
how many pictures were found, ...).

## Blog

[How to Build Windows Virtual Scanner and Feed Custom Images](https://www.dynamsoft.com/codepool/windows-virtual-scanner-custom-images.html)
