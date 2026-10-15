# macOS Virtual Scanner
This project is ported from Apple's [Virtual Scanner project](https://developer.apple.com/library/archive/samplecode/VirtualScanner/Introduction/Intro.html).

https://github.com/user-attachments/assets/87718501-f96a-49cb-b466-8ffe2200b76e

## How to Use

### Quick start (one command)
On a fresh Mac, one script does everything needed to start scanning — builds
and installs the device module, starts the Bonjour advertisement, makes sure
the Dynamic Web TWAIN Service (the runtime the browser demo / verify script
talks to) is installed and running, and prepares the Python environment:

```bash
./Scripts/setup.sh
```

> Requires Xcode (for `xcodebuild`). If the Dynamic Web TWAIN Service is not
> already installed, `setup.sh` downloads and installs it and asks for your
> admin password once. Pass `--skip-dwt` to skip that, `--skip-python` to skip
> the Python environment, or `--skip-build` to only do the DWT/Python parts.
>
> **No Xcode?** Use the single-installer package instead (see
> [Option 2](#option-2-build-a-single-command-installer-package-no-compile-needed)):
> someone builds `dist/VirtualScanner-1.0.pkg` once, and on the target Mac a
> single `sudo installer -pkg ... -target /` does everything — app + Bonjour +
> Dynamic Web TWAIN Service — with no compiling.

Then test the virtual scanner with `Image Capture` or the
[Dynamic Web TWAIN online demo](https://demo3.dynamsoft.com/web-twain/):

![macOS virtual scanner](https://www.dynamsoft.com/codepool/img/2025/03/macos-virtual-scanner.png)

All bundled sample images and configuration live in this repo
(`Resources/SampleImages/`, `Scripts/scanner.config`), so a fresh checkout is
self-contained — no manual file copying needed.

### Configuration
The few values you might need to change are centralized in
[`Scripts/scanner.config`](Scripts/scanner.config) and are read automatically
by `setup.sh` and `verify_scanner.py`:

- `DWT_SERVICE_URL` — where the Dynamic Web TWAIN Service listens (default `http://127.0.0.1:18622`).
- `DWT_SERVICE_PKG_URL` — where `setup.sh` downloads the service installer from.
- `INSTALL_DWT_SERVICE` — `auto` (install only if missing), `always`, or `never`.
- `DWT_LICENSE_KEY` — your Dynamsoft license for **local** CLI scanning. The
  bundled default is a public trial key that has **expired**, so local
  `createJob` returns HTTP 403. The browser demo still works (Dynamsoft
  injects a fresh server-side license). For local scanning, put your own key
  here from https://www.dynamsoft.com/customer/license/trialLicense/?product=dcv&package=cross-platform

Every key is optional; if a key is missing the scripts fall back to built-in
defaults.

## Build and Install for Distribution
The Xcode project is configured for ad-hoc code signing (`Sign to Run Locally`) and
automatic provisioning, so it builds out of the box without an Apple Developer
account or a specific team ID. Three scripts under [Scripts](Scripts) turn the
project into a ready-to-use ICA device module:

> [`setup.sh`](Scripts/setup.sh) is the all-in-one path above (app + Bonjour +
> Dynamic Web TWAIN Service + Python). The two options below are the underlying
> building blocks it uses.

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

### Option 2: Build a single-command installer package (no compile needed)
```bash
./Scripts/package.sh [version] [--build] [--no-dwt]
```
By default `package.sh` **reuses the app already built** under `build/` — it
does **not** compile. (Pass `--build` to force a fresh Release build, or
`--no-dwt` to prevent the installer from installing the Dynamic Web TWAIN
Service.) It produces `dist/VirtualScanner-<version>.pkg`, a single installer
that contains the **entire flow**:

- `/Library/Image Capture/Devices/VirtualScanner.app` — the device module (prebuilt)
- `/Library/LaunchAgents/com.dynamsoft.virtualscanner.bonjour.plist` — Bonjour auto-start
- `/Library/Application Support/VirtualScanner/` — `scanner.config` + `verify_scanner.py` + `requirements.txt`

Its `postinstall` script then activates the Bonjour advertisement for the
logged-in user, restarts Image Capture so the device appears immediately, and
(best-effort) installs the Dynamic Web TWAIN Service if it is missing — so a
fresh Mac is fully working after a single install:

```bash
# On the target Mac (no Xcode, no compiling):
sudo installer -pkg dist/VirtualScanner-1.0.pkg -target /
```

> Note: `VirtualScanner.app` is ad-hoc signed for local use. If you plan to
> distribute the `.pkg` outside your own machine, sign and notarize it so
> Gatekeeper allows it to run without warnings.
>
> `Scripts/package.sh` supports this via environment variables:
>
> ```bash
> SIGNING_IDENTITY="Developer ID Application: YOUR NAME (TEAMID)" \
> INSTALLER_IDENTITY="Developer ID Installer: YOUR NAME (TEAMID)" \
> KEYCHAIN_PROFILE="notarytool-profile" \
> ./Scripts/package.sh 1.0.3
> ```
>
> - `SIGNING_IDENTITY` — codesigns the app with hardened runtime. Requires a
>   **Developer ID Application** certificate.
> - `INSTALLER_IDENTITY` — signs the `.pkg` itself. Requires a **Developer ID
>   Installer** certificate (both are free from <https://developer.apple.com>).
> - `KEYCHAIN_PROFILE` — submits the signed package to Apple notary service and
>   staples the ticket. Create the profile once with
>   `xcrun notarytool store-credentials`. Alternatively set `APPLE_ID`,
>   `APPLE_PASSWORD` (app-specific password) and `APPLE_TEAM_ID`.
>
> The GitHub Actions workflow (`.github/workflows/build-installers.yml`) picks
> the same values up from the `MACOS_SIGNING_IDENTITY`,
> `MACOS_INSTALLER_IDENTITY`, `MACOS_KEYCHAIN_PROFILE`, `MACOS_APPLE_ID`,
> `MACOS_APPLE_PASSWORD` and `MACOS_APPLE_TEAM_ID` repository secrets when they
> are configured. Without signing, users can still install by removing the
> quarantine attribute (`xattr -d com.apple.quarantine VirtualScanner-*.pkg`)
> or from the terminal (`sudo installer -pkg ... -target /`).

## Bonjour Advertisement (why it's needed)

macOS does **not** discover this device from the mere presence of
`VirtualScanner.app` on disk — `icdd` finds it via **Bonjour**
(`_scanner._tcp`). While a
`dns-sd -R "Virtual Scanner" _scanner._tcp local 9500 mdl="Virtual Scanner" mfg=Apple`
advertisement is running, `icdd`'s Bonjour discovery sees the device and
launches the module on demand.

If you ever started that `dns-sd` command manually in a terminal, the scanner
only worked until you closed the terminal or **restarted the Mac** — after a
reboot the advertisement is gone and the scanner silently disappears, no matter
how many times you reinstall the app.

To fix this permanently, `build_and_install.sh` installs a **LaunchAgent**
(`Scripts/com.dynamsoft.virtualscanner.bonjour.plist`) into
`~/Library/LaunchAgents` and starts it automatically. It runs in your login
session, is loaded at login (`RunAtLoad`) and is restarted if it dies
(`KeepAlive`), so the scanner re-appears on its own after every reboot.

The TXT record keys (`mdl=Virtual Scanner`, `mfg=Apple`) match the
`BonjourNetwork` matcher in `Resources/DeviceMatchingInfo.plist`.

### Managing the advertisement by hand

`Scripts/bonjour.sh` wraps the LaunchAgent so you don't need to remember
`launchctl` syntax:

```bash
./Scripts/bonjour.sh install    # copy the plist to ~/Library/LaunchAgents and start
./Scripts/bonjour.sh start      # load the agent (idempotent)
./Scripts/bonjour.sh stop       # unload the agent
./Scripts/bonjour.sh restart    # stop then start
./Scripts/bonjour.sh status     # is it loaded / running / browsable?
```

`build_and_install.sh` calls `bonjour.sh install` for you, so this is only
needed if you want to pause or inspect the advertisement.

### If the scanner is missing after a reboot

1. Check the service: `./Scripts/bonjour.sh status`
   — `dns-sd process: yes` and a `Virtual Scanner` browse hit mean the
   advertisement is fine.
2. Start it if it stopped: `./Scripts/bonjour.sh start`
3. Confirm the device is discoverable (Dynamic Web TWAIN Service must be
   running): `curl -s http://127.0.0.1:18622/DWTAPI/Scanners` — you should see
   `"Virtual Scanner EX/AF"` in the list.
4. Open **Image Capture** — `Virtual Scanner EX/AF` should appear under
   Devices.

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


