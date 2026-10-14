# Linux Virtual Scanner

A virtual (software only) scanner for Linux, implemented as a
[SANE](http://www.sane-project.org/) backend. Applications that talk to SANE
(`scanimage`, `xsane`, the Dynamic Web TWAIN service, ...) see it as a normal
scanner, but instead of reading from hardware it returns the PNM pictures of a
folder, one picture per scan.

Like the Windows and macOS virtual scanners it hands out a **different picture
on every scan** (the position is remembered), and a scan from the *Automatic
Document Feeder* returns **every picture of the set** as a multi page document.

## Quick start (one command)

```bash
./scripts/install.sh
```

It installs the build dependencies when needed (Debian/Ubuntu, Fedora, Arch),
generates the 11 sample images (needs ImageMagick), compiles and installs the
backend, its configuration, the sample images and the `dll.d` drop-in that
enables it. Then:

```bash
scanimage -L
scanimage -d custom_scanner:virtual > out.pnm
```

Remove everything again with `./scripts/install.sh --uninstall`, or install the
package instead of building from source:

```bash
sudo apt install ./dist/sane-custom-scanner_1.0.0_amd64.deb
```

The package is built with `make deb` (the release workflow attaches it to the
GitHub releases).

## Pre-requisites

```bash
sudo apt install build-essential libsane-dev sane-utils imagemagick
```

* `libsane-dev` - the SANE headers the backend is built against
* `sane-utils` - `scanimage`, for testing
* `imagemagick` - only to convert the bundled sample pictures to PNM

## Multi image behaviour

The backend reads the pictures of the configured folder (`.pnm`, `.ppm` and
`.pgm` files, sorted in natural order so that `2.pnm` comes before `10.pnm`) and
uses them like this:

| Mode | One scan job returns |
| --- | --- |
| `auto` (default) & *Flatbed* | one picture; the next scan returns the next one |
| `auto` (default) & *Automatic Document Feeder* | every picture, one page per `sane_start` |
| `cycle` | always one picture, cycling through the set |
| `adf` | always a multi page job, one page per picture |
| `single` | always the same picture |

The position inside the set is persisted (`index`), so scan jobs continue where
the previous one stopped - the same behaviour as the Windows `info.json` index
and the macOS `scan_cycle_index.txt`.

```bash
# flatbed: a different picture every time
scanimage -d custom_scanner:virtual > out.pnm
scanimage -d custom_scanner:virtual > out2.pnm     # different picture

# document feeder: all pictures as one multi page document
scanimage -d custom_scanner:virtual --source 'Automatic Document Feeder' --batch=page-%02d.pnm

# color mode like a real scanner (Color | Gray | Binary)
scanimage -d custom_scanner:virtual --mode Gray > out-gray.pnm
```

## Configuration

Every setting has a built-in default; the configuration file is optional:

| File | Purpose |
| --- | --- |
| `/etc/sane.d/custom_scanner.conf` | system wide settings |
| `~/.config/custom_scanner.conf` | per user settings, override the system file |
| `$CUSTOM_SCANNER_CONFIG` | point somewhere else for a single run |

```ini
# where the pictures are (default: /usr/share/custom_scanner/images)
directory = /home/me/pictures/scans

# a single picture instead of a folder (never cycles)
#image = /home/me/pictures/document.pnm

# auto | cycle | adf | single
mode = auto

# pages of one ADF job, 0 = every picture
#maxcount = 0

# start the set at a fixed position instead of cycling
#index = 0

# where the position is remembered (default: ~/.local/state/custom_scanner/index)
#state_file = ~/.local/state/custom_scanner/index

# gbr (legacy behaviour, only needed by old frontends)
# rgb (plain SANE RGB, the default)
#channel_order = rgb

# device name (the SANE dll layer shows it as custom_scanner:virtual)
#device_name = virtual
```

`CUSTOM_SCANNER_DEBUG=1` makes the backend log what it is doing to `stderr`:

```bash
CUSTOM_SCANNER_DEBUG=1 scanimage -L
```

## Color mode

The backend exposes the standard SANE `mode` option, so frontends control the
pixel type like with any real scanner:

| Mode | What the frontend receives |
| --- | --- |
| `Color` | 8 bit `SANE_FRAME_RGB` (a gray source is expanded to rgb) |
| `Gray` | 8 bit `SANE_FRAME_GRAY` (color sources are converted with the BT.601 luminance) |
| `Binary` / `Lineart` | 1 bit `SANE_FRAME_GRAY`, threshold at 128, MSB first |

scanimage selects it with `--mode Gray`, the Dynamic Web TWAIN demo with its
*Pixel Type* setting (B&W / Gray / Color). The `channel_order` rotation only
applies to `Color` frames.

## Using your own pictures

SANE only understands the PNM family, so convert other formats first:

```bash
convert input.png -depth 8 output.pnm
```

Then either drop the files into the picture folder, or point the backend at
your own folder for one run:

```bash
CUSTOM_SCANNER_IMAGE_DIR=~/Pictures/documents scanimage -d custom_scanner:virtual > out.pnm
```

An 8 bit PNM file is what you want; 16 bit files (maxval > 255) are scaled down
to 8 bit automatically. `scripts/make_samples.sh` converts a folder of
PNG/JPEG files into PNM; by default it uses the 11 pictures of
[`macos/Resources/SampleImages`](../macos/Resources/SampleImages) so that all
three platforms ship the same sample set:

```bash
SAMPLES_WIDTH=1200 ./scripts/make_samples.sh                      # more detail
./scripts/make_samples.sh ~/Pictures/documents                    # your own folder
```

## Manual build and install

```bash
make                       # build libsane-custom_scanner.so.1
sudo make install          # backend + configuration + images + dll.d drop-in
sudo make uninstall        # remove it again
make test                  # self test, no scanner needed
make deb                   # build linux/dist/sane-custom-scanner_*.deb
```

`make install` honours `PREFIX` (default `/usr`), `DESTDIR`, `CONF_DIR` and
`IMAGE_DIR`:

```bash
make install PREFIX=/usr/local IMAGE_DIR=/srv/scans
```

By default the pictures come from `/usr/share/custom_scanner/images`; run
`make samples` before `make install` to fill that folder with the sample set
(otherwise the single bundled `test.pnm` is installed).

Installed files:

```
/usr/lib/<multiarch>/sane/libsane-custom_scanner.so.1   the backend
/etc/sane.d/custom_scanner.conf                         its configuration
/etc/sane.d/dll.d/custom_scanner.conf                   enables the backend
/usr/share/custom_scanner/images/*.pnm                  the pictures
```

## Tests

`make test` builds `tests/selftest.c`, drives the backend exactly like a SANE
frontend does (open, `sane_start`, `sane_get_parameters`, `sane_read` until
`NO_DOCS`) and checks the multi image behaviour, the ADF pages, the formats
(gray, rgb, 16 bit, commented PNM headers) and the error paths - all with
pictures it creates itself, so no SANE frontend or hardware is needed. It runs
in Docker too:

```bash
docker run --rm -v "$PWD/..:/src:ro" ubuntu:22.04 \
  bash -c "apt-get update -qq && apt-get install -y -qq build-essential libsane-dev &&
           cp -r /src /work && cd /work/linux && make test"
```

A virtual backend needs no kernel driver or USB device, so a container is
enough - `scanimage` can be used in there as well (see the Docker line above,
plus `sane-utils`).

## Troubleshooting

```bash
SANE_DEBUG_DLL=255 scanimage -L          # is the backend loaded?
CUSTOM_SCANNER_DEBUG=1 scanimage -d custom_scanner:virtual > out.pnm
```

* *No scanners were identified* - is the backend enabled? `ls /etc/sane.d/dll.d/`
  (or `grep custom_scanner /etc/sane.d/dll.conf`) and check that
  `libsane-custom_scanner.so.1` is in the SANE library directory
  (`grep -r . /etc/sane.d/dll.conf`, `dpkg -L sane-custom-scanner`).
* *Document feeder out of documents* - the picture folder is empty, does not
  exist, or contains no readable PNM file (the backend says so on `stderr`).
* *Wrong colours* - the backend hands out plain RGB (`channel_order = rgb`)
  as the SANE standard requires. If an old frontend still shows rotated
  colours, set `channel_order = gbr`, the legacy behaviour of earlier
  releases.
* The device is listed as `custom_scanner:virtual` (the SANE `dll` layer
  prefixes the name reported by the backend) - use that name with `-d`.

## Reference

* [test.c](https://gitlab.com/sane-project/backends/-/blob/master/backend/test.c) -
  SANE test backend implementation
* [SANE standard](http://www.sane-project.org/docs.html) - backend API
