#!/bin/bash
#
# build_and_install.sh
#
# Builds the VirtualScanner Image Capture device module in Release
# configuration and installs it so macOS can discover it immediately.
#
# By default the module is installed for the current user only
# (~/Library/Image Capture/Devices), which requires no sudo/admin rights.
# Pass --system to install machine-wide to /Library/Image Capture/Devices
# instead (requires sudo).
#
# Usage:
#   ./Scripts/build_and_install.sh [--system]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
SCHEME="VirtualScanner"
CONFIGURATION="Release"
APP_NAME="VirtualScanner.app"

SCOPE="user"
if [[ "${1:-}" == "--system" ]]; then
  SCOPE="system"
fi

if [[ "$SCOPE" == "system" ]]; then
  DEST_DIR="/Library/Image Capture/Devices"
else
  DEST_DIR="$HOME/Library/Image Capture/Devices"
fi

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

echo "==> Installing to $DEST_DIR"
if [[ "$SCOPE" == "system" ]]; then
  sudo mkdir -p "$DEST_DIR"
  sudo rm -rf "$DEST_DIR/$APP_NAME"
  sudo cp -R "$BUILT_APP" "$DEST_DIR/$APP_NAME"
else
  mkdir -p "$DEST_DIR"
  rm -rf "$DEST_DIR/$APP_NAME"
  cp -R "$BUILT_APP" "$DEST_DIR/$APP_NAME"
fi

# If we installed to the user scope but a machine-wide copy also exists, warn
# loudly: icdd prefers the system-scope module, so the user-scope copy we just
# installed would be silently ignored (rebuilds appear to have "no effect").
SYSTEM_COPY="/Library/Image Capture/Devices/$APP_NAME"
if [[ "$SCOPE" == "user" && -d "$SYSTEM_COPY" ]]; then
  echo ""
  echo "warning: a machine-wide copy already exists at:"
  echo "         $SYSTEM_COPY"
  echo "         icdd loads the SYSTEM-scope module in preference to the user copy,"
  echo "         so the copy just installed may be ignored."
  echo "==> Updating the system-scope copy too (admin password may be requested)"
  osascript -e "do shell script \"rm -rf '$SYSTEM_COPY' && cp -R '$BUILT_APP' '$SYSTEM_COPY'\" with administrator privileges" \
    && echo "    System-scope copy updated." \
    || echo "warning: failed to update the system-scope copy; run: sudo ./Scripts/build_and_install.sh --system"
fi

# ---------------------------------------------------------------------------
# Install the Bonjour advertisement LaunchAgent so icdd can discover the
# device. Without it, the scanner only works while a manually started
# `dns-sd -R ...` is running -- which dies on reboot (the classic "works now,
# gone after restart" trap). Delegated to Scripts/bonjour.sh so the agent can
# also be managed by hand afterwards (start/stop/restart/status).
# ---------------------------------------------------------------------------
echo "==> Starting Bonjour advertisement service (LaunchAgent)"
"$SCRIPT_DIR/bonjour.sh" install

echo "==> Restarting Image Capture services so the new device is discovered"
killall VirtualScanner >/dev/null 2>&1 || true
killall icdd >/dev/null 2>&1 || true
killall imagecaptureagent >/dev/null 2>&1 || true
killall ICAgent >/dev/null 2>&1 || true

echo "==> Done. VirtualScanner installed at: $DEST_DIR/$APP_NAME"
echo "    Open Image Capture, or run the Python verification script, to confirm it is detected."
