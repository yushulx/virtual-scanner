#!/usr/bin/env bash
#
# make_deb.sh [version] - builds a Debian/Ubuntu package containing the
# virtual scanner SANE backend, its configuration, the sample images and the
# dll.d drop-in that enables the backend:
#
#   dist/sane-custom-scanner_<version>_<arch>.deb
#
# Install it with:  sudo apt install ./dist/sane-custom-scanner_<version>_<arch>.deb
#
# Requires: dpkg-deb (dpkg-dev), and ImageMagick for the sample images (the
# sample set is generated when samples/ is still empty).
#
set -euo pipefail

VERSION="${1:-1.0.0}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="$LINUX_DIR/dist"

PACKAGE="sane-custom-scanner"
IMAGE_DIR="/usr/share/custom_scanner/images"
BACKEND="custom_scanner"

if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "error: dpkg-deb is required (install it with: sudo apt install dpkg-dev)" >&2
  exit 1
fi

ARCH="$(dpkg --print-architecture 2>/dev/null || dpkg-architecture -qDEB_HOST_ARCH 2>/dev/null || echo amd64)"

# The sample set is generated on demand so that a plain "make deb" works on a
# fresh checkout.
if ! ls "$LINUX_DIR"/samples/*.pnm >/dev/null 2>&1; then
  if command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1; then
    echo "==> samples/ is empty, generating the sample image set"
    "$SCRIPT_DIR/make_samples.sh"
  else
    echo "note: no sample images (ImageMagick is not installed); the package will"
    echo "      only ship test.pnm"
  fi
fi

PKG_ROOT="$(mktemp -d)"
trap 'rm -rf "$PKG_ROOT"' EXIT

# Pre-create dll.d so that "make install" enables the backend through the
# drop-in instead of touching /etc/sane.d/dll.conf.
mkdir -p "$PKG_ROOT/etc/sane.d/dll.d"

echo "==> Building the backend"
make -C "$LINUX_DIR" IMAGE_DIR="$IMAGE_DIR"

echo "==> Staging the package"
make -C "$LINUX_DIR" install DESTDIR="$PKG_ROOT" PREFIX=/usr IMAGE_DIR="$IMAGE_DIR"

mkdir -p "$PKG_ROOT/DEBIAN"

cat > "$PKG_ROOT/DEBIAN/control" <<EOF
Package: $PACKAGE
Version: $VERSION
Architecture: $ARCH
Maintainer: Dynamsoft <support@dynamsoft.com>
Section: graphics
Priority: optional
Depends: libsane1 | libsane (>= 1.0.25)
Installed-Size: $(du -sk "$PKG_ROOT" | cut -f1)
Description: Virtual SANE scanner that feeds images from disk
 A SANE backend implementing a virtual scanner: it appears like a normal
 scanner to SANE applications (scanimage, xsane, Dynamic Web TWAIN, ...) but
 returns the PNM images found in $IMAGE_DIR.
 .
 A single scan job returns one image, scanning again returns the next image
 (the position is remembered between jobs, like the Windows and macOS virtual
 scanners). Selecting the "Automatic Document Feeder" source returns every
 image of the set as a multi page job.
 .
 Convert your own images with:
   convert input.png -depth 8 output.pnm
 and drop them into $IMAGE_DIR.
EOF

printf '/etc/sane.d/custom_scanner.conf\n' > "$PKG_ROOT/DEBIAN/conffiles"

cat > "$PKG_ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

if [ "$1" = "configure" ]; then
  # Make sure the backend is enabled on SANE versions without dll.d support.
  if [ ! -d /etc/sane.d/dll.d ] && [ -f /etc/sane.d/dll.conf ] &&
     ! grep -qxF custom_scanner /etc/sane.d/dll.conf; then
    echo custom_scanner >> /etc/sane.d/dll.conf
  fi
fi

exit 0
EOF
chmod 755 "$PKG_ROOT/DEBIAN/postinst"

cat > "$PKG_ROOT/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e

if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
  if [ -f /etc/sane.d/dll.conf ]; then
    grep -vxF custom_scanner /etc/sane.d/dll.conf > /etc/sane.d/dll.conf.tmp || true
    mv /etc/sane.d/dll.conf.tmp /etc/sane.d/dll.conf
  fi
fi

exit 0
EOF
chmod 755 "$PKG_ROOT/DEBIAN/prerm"

mkdir -p "$DIST_DIR"
DEB="$DIST_DIR/${PACKAGE}_${VERSION}_${ARCH}.deb"
rm -f "$DEB"

echo "==> Building $DEB"
dpkg-deb --root-owner-group --build "$PKG_ROOT" "$DEB"

echo ""
echo "==> Done: $DEB"
echo "    Install it with: sudo apt install $DEB"
dpkg-deb --info "$DEB" | sed -n '1,12p'
