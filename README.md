# Virtual Scanners for Windows, Linux and macOS

Virtual (software only) scanners for **Windows**, **Linux** and **macOS**.
Applications see them as real scanning devices, but instead of reading from
hardware they return picture files from disk - which makes them handy for
testing scanning workflows, demos and CI without hardware.

All three implementations share the same behaviour and the same 11 sample
pictures ([`macos/Resources/SampleImages`](macos/Resources/SampleImages)):

| Platform | Integration | Implementation | Package |
| --- | --- | --- | --- |
| Windows | TWAIN data source | C++ / Qt 5 / FreeImage | `VirtualScannerSetup-<version>.exe` (Inno Setup) |
| Linux | [SANE](http://www.sane-project.org/) backend | C | `sane-custom-scanner_<version>_amd64.deb` |
| macOS | Image Capture device module (ICA) + Bonjour | Objective-C | `VirtualScanner-<version>.pkg` |

## Download and install

The installers are built and attached to every release by the
[Build installers](.github/workflows/build-installers.yml) workflow - either
push a `v*` tag or start it from *Actions*. Every package ships the 11 sample
pictures, so the scanner returns a different picture on every scan right after
the installation, and a scan from the document feeder (ADF) returns all
pictures as one multi page document.

**Windows** (needs a TWAIN Data Source Manager, which every scanner driver or
Dynamic Web TWAIN installs):

```bat
VirtualScannerSetup-1.0.0.exe            :: run it, it asks for administrator rights
```

or unpack `VirtualScanner-1.0.0-win64-portable.zip` into
`C:\Windows\twain_64\sample2`.

**Linux** (Debian/Ubuntu):

```bash
sudo apt install ./sane-custom-scanner_1.0.0_amd64.deb
scanimage -L
scanimage -d custom_scanner:virtual > out.pnm
```

**macOS**:

```bash
sudo installer -pkg VirtualScanner-1.0.0.pkg -target /
# then open Image Capture: "Virtual Scanner EX/AF"
```

## Common behaviour

* A scan returns the pictures of a folder, one picture per scan job
  (*flatbed*), and the next scan returns the next picture.
* A scan from the **document feeder / ADF** returns one page per picture, so a
  single job produces a multi page document.
* The position inside the folder is remembered between scans, so scanning
  repeatedly - even from the online demo, which cannot pick a picture itself -
  keeps showing a different picture.
* A specific picture, or a folder of your own pictures, can be selected
  (macOS: `selected_image.txt`, Windows: `source.json`/`info.json`,
  Linux: `custom_scanner.conf`).
* Pictures are sorted in natural order (`2.png` before `10.png`).

| Platform | Where the pictures come from by default |
| --- | --- |
| Windows | `C:\Windows\twain_64\sample2\images` (next to the data source) |
| Linux | `/usr/share/custom_scanner/images` (`directory` in `/etc/sane.d/custom_scanner.conf`) |
| macOS | `Resources/SampleImages` inside `VirtualScanner.app` |

## Build from source

* [windows/README.md](windows/README.md) - one command builds and deploys the
  TWAIN data source, and a second one produces the installer and the portable
  ZIP:

  ```bat
  windows\build_and_install.cmd
  windows\build_installer.cmd -Version 1.0.0
  ```

* [linux/README.md](linux/README.md):

  ```bash
  ./linux/scripts/install.sh        # build + install
  make -C linux test                # self test, no scanner needed
  make -C linux deb                 # build the .deb
  ```

* [macos/README.md](macos/README.md):

  ```bash
  ./macos/Scripts/setup.sh          # build + install + Bonjour + service
  ./macos/Scripts/package.sh 1.0.0 --build
  ```

## Repository layout

```
linux/    SANE backend (custom_scanner.c), Makefile, packaging + self test
macos/    Image Capture device module (Xcode project), scripts, sample images
windows/  TWAIN data source sample + one click build/packaging scripts
.github/  build-installers.yml: builds and publishes the three installers
```

## Testing on a machine without the target OS

* **Linux**: the backend is plain user space - `make test` (or `scanimage`)
  runs in a Docker container, no kernel driver, USB device or QEMU needed.
* **Windows**: the data source needs Windows + a TWAIN application. The one
  click script also builds the packages without administrator rights
  (`-StageDir`), which is what CI does.
* **macOS**: Image Capture cannot be faked in a container; use a real Mac (or
  the GitHub `macos` runner, as the release workflow does).

## License

The Windows data source is ported from
[twain-samples](https://github.com/twain/twain-samples) (TWAIN Working Group,
BSD style license), the macOS module from Apple's Virtual Scanner sample code.
