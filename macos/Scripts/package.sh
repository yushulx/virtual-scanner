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
# Signing / notarization (all optional - without them the package is ad-hoc
# signed and macOS Gatekeeper blocks it on other machines):
#
#   SIGNING_IDENTITY   codesign identity for the app, e.g. "Developer ID
#                      Application: YOUR NAME (TEAMID)". "-" keeps the app
#                      ad-hoc signed.
#   INSTALLER_IDENTITY pkgbuild signing identity, e.g. "Developer ID
#                      Installer: YOUR NAME (TEAMID)". Required for a
#                      Gatekeeper-friendly package.
#   KEYCHAIN_PROFILE   `notarytool` keychain profile (created once with
#                      `xcrun notarytool store-credentials`). When set, the
#                      package is notarized and stapled.
#   APPLE_ID / APPLE_PASSWORD / APPLE_TEAM_ID
#                      alternative notarytool auth (app-specific password).
#                      Used only when KEYCHAIN_PROFILE is not set.
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

  # ARCHS_OVERRIDE="x86_64 arm64" builds a universal binary that runs on both
  # Intel Macs and Apple silicon (used by the release workflow).
  extra_build_args=()
  if [[ -n "${ARCHS_OVERRIDE:-}" ]]; then
    extra_build_args+=("ARCHS=$ARCHS_OVERRIDE" "ONLY_ACTIVE_ARCH=NO")
    echo "==> Architectures: $ARCHS_OVERRIDE"
  fi

  xcodebuild \
    -project "$PROJECT_DIR/VirtualScanner.xcodeproj" \
    -scheme "$SCHEME" \
    -configuration "$CONFIGURATION" \
    -derivedDataPath "$BUILD_DIR" \
    CODE_SIGN_IDENTITY="-" \
    CODE_SIGNING_REQUIRED=YES \
    CODE_SIGNING_ALLOWED=YES \
    MARKETING_VERSION="$VERSION" \
    CURRENT_PROJECT_VERSION="$VERSION" \
    "${extra_build_args[@]}" \
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

# Stamp the release version into the app bundle. The Xcode project's
# VirtualScanner-Info.plist hardcodes CFBundleShortVersionString=1.0, so
# without this every build reports 1.0 and stale copies are
# indistinguishable from fresh ones. Editing the plist invalidates the
# build's ad-hoc signature, so the app is always re-signed below.
STAMPED_PLIST="$PKG_ROOT$APP_INSTALL_DIR/$APP_NAME/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$STAMPED_PLIST" >/dev/null
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $VERSION" "$STAMPED_PLIST" >/dev/null
echo "==> Stamped app version: $VERSION"

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

# --- Sign the app bundle (re-sign after the plist stamping) ----------------------
SIGN_IDENTITY="${SIGNING_IDENTITY:--}"
if [[ "$SIGN_IDENTITY" != "-" ]]; then
  echo "==> Signing $APP_NAME with: $SIGN_IDENTITY"
  codesign --force --deep --options runtime --timestamp \
    --sign "$SIGN_IDENTITY" \
    "$PKG_ROOT$APP_INSTALL_DIR/$APP_NAME"
else
  echo "==> No SIGNING_IDENTITY set - re-signing the app ad-hoc"
  codesign --force --deep --sign - "$PKG_ROOT$APP_INSTALL_DIR/$APP_NAME"
fi

# --- Build the installer package -------------------------------------------------
mkdir -p "$DIST_DIR"
PKG_PATH="$DIST_DIR/VirtualScanner-$VERSION.pkg"

INSTALLER_IDENTITY="${INSTALLER_IDENTITY:-}"
pkgbuild_args=(
  --root "$PKG_ROOT"
  --identifier "$BUNDLE_ID"
  --version "$VERSION"
  --scripts "$SCRIPT_DIR/pkg-scripts"
  --install-location "/"
)

if [[ -n "$INSTALLER_IDENTITY" ]]; then
  echo "==> Signing the package with: $INSTALLER_IDENTITY"
  pkgbuild_args+=(--sign "$INSTALLER_IDENTITY")
else
  echo "==> No INSTALLER_IDENTITY set - the package will be UNSIGNED"
  echo "    (Gatekeeper will block it on other Macs; install via CLI with:"
  echo "     sudo installer -pkg <pkg> -target /  or remove the quarantine"
  echo "     attribute: xattr -d com.apple.quarantine <pkg>)"
fi

echo "==> Building installer package: $PKG_PATH"
pkgbuild "${pkgbuild_args[@]}" "$PKG_PATH"

# --- Notarize and staple (optional) ----------------------------------------------
KEYCHAIN_PROFILE="${KEYCHAIN_PROFILE:-}"
if [[ -n "$INSTALLER_IDENTITY" && -n "$KEYCHAIN_PROFILE" ]]; then
  echo "==> Submitting the package for notarization (profile: $KEYCHAIN_PROFILE)"
  xcrun notarytool submit "$PKG_PATH" --keychain-profile "$KEYCHAIN_PROFILE" --wait
  echo "==> Stapling the notarization ticket"
  xcrun stapler staple "$PKG_PATH"
elif [[ -n "$INSTALLER_IDENTITY" && -n "${APPLE_ID:-}" && -n "${APPLE_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
  echo "==> Submitting the package for notarization (Apple ID: $APPLE_ID)"
  xcrun notarytool submit "$PKG_PATH" \
    --apple-id "$APPLE_ID" --password "$APPLE_PASSWORD" --team-id "$APPLE_TEAM_ID" --wait
  echo "==> Stapling the notarization ticket"
  xcrun stapler staple "$PKG_PATH"
else
  echo "==> Skipping notarization (set KEYCHAIN_PROFILE, or APPLE_ID +"
  echo "    APPLE_PASSWORD + APPLE_TEAM_ID, to enable it)"
fi

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
