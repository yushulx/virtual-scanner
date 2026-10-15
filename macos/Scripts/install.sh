#!/bin/bash
#
# install.sh
#
# One-line installer for the Virtual Scanner ICA device module.
#
# Why this exists: the .pkg is not signed with a Developer ID, so downloading
# it in a browser (or any tool that sets com.apple.quarantine) makes Gatekeeper
# refuse to open it with "Apple could not verify ...". Files fetched with curl
# carry no quarantine attribute, and `installer` does not run the Gatekeeper
# assessment at all - so this path installs cleanly on any Mac without a
# Developer ID certificate.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/yushulx/virtual-scanner/main/macos/Scripts/install.sh | bash
#
#   or, for a pinned version:
#   curl -fsSL .../install.sh | bash -s -- 1.0.3
#
set -euo pipefail

REPO="${VIRTUALSCANNER_REPO:-yushulx/virtual-scanner}"
VERSION="${1:-}"

if [[ -z "$VERSION" ]]; then
  echo "==> Resolving the latest release of $REPO"
  VERSION="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
    | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' \
    | head -1)"
  if [[ -z "$VERSION" ]]; then
    echo "error: could not determine the latest release version" >&2
    exit 1
  fi
fi

PKG_NAME="VirtualScanner-$VERSION.pkg"
URL="https://github.com/$REPO/releases/download/v$VERSION/$PKG_NAME"
TMP_PKG="${TMPDIR:-/tmp}/$PKG_NAME"

echo "==> Downloading $PKG_NAME"
curl -fL --progress-bar "$URL" -o "$TMP_PKG"

# Belt and braces: curl does not set the quarantine attribute, but strip it if
# anything on the machine did.
xattr -d com.apple.quarantine "$TMP_PKG" 2>/dev/null || true

echo "==> Installing (administrator rights are required)"
sudo installer -pkg "$TMP_PKG" -target /

rm -f "$TMP_PKG"

echo ""
echo "==> Done. Open Image Capture and the virtual scanner should be listed."
echo "    Verify the Dynamic Web TWAIN side with:"
echo "    python3 '/Library/Application Support/VirtualScanner/verify_scanner.py'"
