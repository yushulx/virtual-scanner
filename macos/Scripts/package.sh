#!/bin/bash
#
# package.sh
#
# Builds the VirtualScanner Image Capture device module in Release
# configuration and packages it into a signed-or-unsigned .pkg installer
# for distribution. The installer places the module in
# /Library/Image Capture/Devices so it is available to every user on
# the machine, and restarts Image Capture services via a postinstall
# script so the device is picked up without a reboot.
#
# Usage:
#   ./Scripts/package.sh [version]
#
# Output:
#   dist/VirtualScanner-<version>.pkg
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
DIST_DIR="$PROJECT_DIR/dist"
PKG_ROOT="$BUILD_DIR/pkgroot"
SCHEME="VirtualScanner"
CONFIGURATION="Release"
APP_NAME="VirtualScanner.app"
BUNDLE_ID="com.dynamsoft.VirtualScanner"
VERSION="${1:-1.0}"
INSTALL_LOCATION="/Library/Image Capture/Devices"

echo "==> Building $SCHEME ($CONFIGURATION)"
xcodebuild \
  -project "$PROJECT_DIR/VirtualScanner.xcodeproj" \
  -scheme "$SCHEME" \
  -configuration "$CONFIGURATION" \
  -derivedDataPath "$BUILD_DIR" \
  CODE_SIGN_IDENTITY="-" \
  CODE_SIGNING_REQUIRED=YES \
  CODE_SIGNING_ALLOWED=YES \
  build

BUILT_APP="$BUILD_DIR/Build/Products/$CONFIGURATION/$APP_NAME"
if [[ ! -d "$BUILT_APP" ]]; then
  echo "error: build did not produce $BUILT_APP" >&2
  exit 1
fi

echo "==> Assembling package root"
rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT$INSTALL_LOCATION"
cp -R "$BUILT_APP" "$PKG_ROOT$INSTALL_LOCATION/$APP_NAME"

mkdir -p "$DIST_DIR"
PKG_PATH="$DIST_DIR/VirtualScanner-$VERSION.pkg"

echo "==> Building installer package"
pkgbuild \
  --root "$PKG_ROOT" \
  --identifier "$BUNDLE_ID" \
  --version "$VERSION" \
  --scripts "$SCRIPT_DIR/pkg-scripts" \
  --install-location "/" \
  "$PKG_PATH"

echo "==> Done. Installer created at: $PKG_PATH"
echo "    Distribute this .pkg. On the target Mac, double-click it (or run"
echo "    'sudo installer -pkg $PKG_PATH -target /') to install the scanner"
echo "    system-wide and restart Image Capture services automatically."
