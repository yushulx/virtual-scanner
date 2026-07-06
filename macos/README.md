# macOS Virtual Scanner
This project is ported from Apple's [Virtual Scanner project](https://developer.apple.com/library/archive/samplecode/VirtualScanner/Introduction/Intro.html).

https://github.com/user-attachments/assets/87718501-f96a-49cb-b466-8ffe2200b76e

## How to Use
1. Import the project into Xcode.
2. Build and run the project.
3. Test the virtual scanner with `Image Capture` or [Dynamic Web TWAIN online demo](https://demo3.dynamsoft.com/web-twain/).

    ![macOS virtual scanner](https://www.dynamsoft.com/codepool/img/2025/03/macos-virtual-scanner.png)

## Build and Install for Distribution
The Xcode project is configured for ad-hoc code signing (`Sign to Run Locally`) and
automatic provisioning, so it builds out of the box without an Apple Developer
account or a specific team ID. Two scripts under [Scripts](Scripts) turn the
project into a ready-to-use ICA device module:

### Option 1: Install for the current user (no admin rights required)
```bash
./Scripts/build_and_install.sh
```
Builds the Release configuration and copies `VirtualScanner.app` into
`~/Library/Image Capture/Devices`, then restarts the Image Capture agent so the
device is picked up immediately.

Pass `--system` to install machine-wide instead (requires `sudo`):
```bash
./Scripts/build_and_install.sh --system
```
This installs into `/Library/Image Capture/Devices` for every user on the Mac.

### Option 2: Build a distributable installer package
```bash
./Scripts/package.sh [version]
```
Builds the Release configuration and produces `dist/VirtualScanner-<version>.pkg`,
a standard macOS installer that places `VirtualScanner.app` in
`/Library/Image Capture/Devices` and restarts Image Capture services via a
postinstall script. Share this `.pkg` with other Macs; install it with:
```bash
sudo installer -pkg dist/VirtualScanner-1.0.pkg -target /
```

> Note: `VirtualScanner.app` is ad-hoc signed for local use. If you plan to
> distribute the `.pkg` outside your own machine, sign and notarize it with
> your own Apple Developer ID (`CODE_SIGN_IDENTITY` / `DEVELOPMENT_TEAM` build
> settings) so Gatekeeper allows it to run without warnings.

## Verify with Python
[`Scripts/verify_scanner.py`](Scripts/verify_scanner.py) verifies that
VirtualScanner is discoverable and can scan an image, using the
[`twain-wia-sane-scanner`](https://pypi.org/project/twain-wia-sane-scanner/)
Python package, which talks to scanners (including macOS ICA devices) through
the Dynamic Web TWAIN Service REST API.

1. Install and start the [Dynamic Web TWAIN Service](https://demo.dynamsoft.com/DWT/DWTResources/dist/DynamsoftServiceSetup.pkg)
   on the same Mac.
2. Install the Python package:
   ```bash
   pip install -r Scripts/requirements.txt
   ```
3. Run the verification script:
   ```bash
   python3 Scripts/verify_scanner.py
   ```
   It lists all ICA scanners found by the service (VirtualScanner should
   appear as `Virtual Scanner EX/AF`), scans a page, and saves it under
   `./output`. Use `--host` and `--license` to point at a different service
   host/port or to supply a Dynamsoft license key.

## Sample Image Selection
Every scan (from `Image Capture`, the Dynamic Web TWAIN online demo, or
`verify_scanner.py`) returns one of the bundled sample images under
`Resources/SampleImages`. By default, VirtualScanner **automatically cycles**
through all bundled images in order (wrapping back to the first after the
last), so clicking Scan repeatedly — even from the online demo, which has no
way to pick an image itself — returns a different image each time. The
current position in the cycle is persisted in
`~/Library/Application Support/VirtualScanner/scan_cycle_index.txt`.

To force a *specific* image instead of auto-cycling, write its filename to
`~/Library/Application Support/VirtualScanner/selected_image.txt`, or use
`verify_scanner.py --image <name>` (e.g. `--image 3.png`, or `--image all` to
scan every bundled image once). Clear/delete that file to resume auto-cycling.

### ADF (document feeder) scanning
When the client enables the **document feeder / ADF** functional unit (for
example by checking "Use ADF" in the online demo), a single scan job returns
**all** bundled sample images at once — one page per image — starting from the
current cycle position. This lets you exercise multi-page scanning without any
manual image selection. Flatbed scans return a single image per Scan click and
advance the cycle by one.

> **Note:** If a machine-wide copy exists at
> `/Library/Image Capture/Devices/VirtualScanner.app`, macOS (`icdd`) loads it
> in preference to the per-user copy. `build_and_install.sh` now detects this
> and offers to update the system-scope copy too; otherwise reinstall with
> `sudo ./Scripts/build_and_install.sh --system` so your rebuilt module is the
> one actually used.


