#!/bin/bash
#
# package.sh
#
# Packages the VirtualScanner ICA device module PLUS the whole setup flow into a
# single .pkg installer, so installing it on a fresh Mac is all that's needed:
#
#   - /Library/Image Capture/Devices/VirtualScanner.app
#       the device module itself (prebuilt, no compile needed)
#   - /Library/LaunchAgents/com.dynamsoft.virtualscanner.bonjour.plist
#       Bonjour advertisement auto-start (loads at login, survives reboots)
#   - /Library/Application Support/VirtualScanner/
#       scanner.config (configuration) + verify_scanner.py + requirements.txt
#
# A postinstall script then activates the Bonjour advertisement for the
# logged-in user, restarts Image Capture so the device is picked up, and
# (best-effort) ensures the Dynamic Web TWAIN Service is available -- the
# runtime the browser demo / verify_scanner.py talk to.
#
# By default this script REUSES the app already built under build/ (it does
# NOT compile). Pass --build to force a fresh Release build first.
#
# Usage:
#   ./Scripts/package.sh [version] [--build] [--no-dwt]
#
#   version   e.g. 1.0 (default) -> dist/VirtualScanner-1.0.pkg
#   --build   rebuild the app with xcodebuild instead of reusing build/
#   --no-dwt  bake INSTALL_DWT_SERVICE=never into the packaged config so the
#             postinstall will not install the Dynamic Web TWAIN Service
#
# Install on a target Mac with:
#   sudo installer -pkg dist/VirtualScanner-1.0.pkg -target /
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
VERSION="1.0"
DO_BUILD=0
DWT_POLICY="auto"

for arg in "$@"; do
  case "$arg" in
    --build)  DO_BUILD=1 ;;
    --no-dwt) DWT_POLICY="never" ;;
    --help|-h)
      echo "usage: $0 [version] [--build] [--no-dwt]" >&2
      exit 0
      ;;
    *)
      if [[ "$arg" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
        VERSION="$arg"
      else
        echo "unknown option: $arg" >&2
        exit 1
      fi
      ;;
  esac
done

# --- Optional rebuild ---------------------------------------------------------
if [[ "$DO_BUILD" -eq 1 ]]; then
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
fi

BUILT_APP="$BUILD_DIR/Build/Products/$CONFIGURATION/$APP_NAME"
if [[ ! -d "$BUILT_APP" ]]; then
  echo "error: no prebuilt app found at $BUILT_APP" >&2
  echo "       Run ./Scripts/build_and_install.sh once (or pass --build)." >&2
  exit 1
fi
echo "==> Packaging prebuilt app: $BUILT_APP"

# --- Assemble payload ----------------------------------------------------------
APP_INSTALL_DIR="/Library/Image Capture/Devices"
SUPPORT_DIR="/Library/Application Support/VirtualScanner"

rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT$APP_INSTALL_DIR"
mkdir -p "$PKG_ROOT/Library/LaunchAgents"
mkdir -p "$PKG_ROOT$SUPPORT_DIR"

cp -R "$BUILT_APP" "$PKG_ROOT$APP_INSTALL_DIR/$APP_NAME"
cp "$SCRIPT_DIR/com.dynamsoft.virtualscanner.bonjour.plist" \
   "$PKG_ROOT/Library/LaunchAgents/com.dynamsoft.virtualscanner.bonjour.plist"
cp "$SCRIPT_DIR/verify_scanner.py" \
   "$SCRIPT_DIR/requirements.txt" \
   "$PKG_ROOT$SUPPORT_DIR/"

# Bake the DWT install policy into the packaged config.
if [[ "$DWT_POLICY" == "never" ]]; then
  sed 's/^INSTALL_DWT_SERVICE=.*/INSTALL_DWT_SERVICE=never/' \
      "$SCRIPT_DIR/scanner.config" > "$PKG_ROOT$SUPPORT_DIR/scanner.config"
else
  cp "$SCRIPT_DIR/scanner.config" "$PKG_ROOT$SUPPORT_DIR/scanner.config"
fi

# --- Build the installer package -------------------------------------------------
mkdir -p "$DIST_DIR"
PKG_PATH="$DIST_DIR/VirtualScanner-$VERSION.pkg"

echo "==> Building installer package: $PKG_PATH"
pkgbuild \
  --root "$PKG_ROOT" \
  --identifier "$BUNDLE_ID" \
  --version "$VERSION" \
  --scripts "$SCRIPT_DIR/pkg-scripts" \
  --install-location "/" \
  "$PKG_PATH"

echo "==> Done. Single-command installer created at:"
echo "    $PKG_PATH"
echo "    Install on any Mac with:"
echo "    sudo installer -pkg $PKG_PATH -target /"
echo ""
echo "    The installer includes the prebuilt app, the Bonjour auto-start"
echo "    LaunchAgent, the scanner config, and the verification script. Its"
echo "    postinstall activates the advertisement, restarts Image Capture,"
echo "    and installs the Dynamic Web TWAIN Service if it is missing"
echo "    (policy: $DWT_POLICY; set INSTALL_DWT_SERVICE or use --no-dwt to change)."
