#!/bin/bash
#
# setup.sh - one-command setup so a fresh Mac can scan with VirtualScanner.
#
#   ./Scripts/setup.sh
#
# Running this single script from a fresh checkout of the repo should be all
# you need. It:
#
#   1. Builds the app, installs it as an Image Capture device and starts the
#      Bonjour advertisement  (delegates to build_and_install.sh).
#   2. Ensures the Dynamic Web TWAIN Service is installed & running. That
#      service is what the browser demo / verify_scanner.py actually talk to;
#      without it the scanner is installed but nothing can scan. If it is
#      missing, it is downloaded and installed (admin password requested).
#   3. Creates a Python venv and installs Scripts/requirements.txt so
#      verify_scanner.py can run.
#
# Options:
#   --skip-dwt     don't install/start the Dynamic Web TWAIN Service
#   --skip-python  don't create the venv / install Python dependencies
#   --skip-build   don't rebuild/reinstall the app (only do DWT + python)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="$SCRIPT_DIR/scanner.config"
UID_NUM="$(id -u)"

skip_dwt=0
skip_python=0
skip_build=0
for arg in "$@"; do
  case "$arg" in
    --skip-dwt)    skip_dwt=1 ;;
    --skip-python) skip_python=1 ;;
    --skip-build)  skip_build=1 ;;
    --help|-h)
      echo "usage: $0 [--skip-dwt] [--skip-python] [--skip-build]" >&2
      exit 0
      ;;
    *) echo "unknown option: $arg" >&2; exit 1 ;;
  esac
done

# --- Config (defaults first, then Scripts/scanner.config overrides) ----------
DWT_SERVICE_URL="http://127.0.0.1:18622"
DWT_SERVICE_PKG_URL="https://demo.dynamsoft.com/DWT/DWTResources/dist/DynamsoftServiceSetup.pkg"
INSTALL_DWT_SERVICE="auto"

if [[ -f "$CONFIG_FILE" ]]; then
  # shellcheck disable=SC1090
  source "$CONFIG_FILE"
fi

# --- Helpers -----------------------------------------------------------------
dwt_running() {
  curl -s -m 2 -o /dev/null -w "%{http_code}" "$DWT_SERVICE_URL/" 2>/dev/null | grep -q "^200$"
}

find_dwt_app() {
  find "$HOME/Applications/Dynamsoft" "/Applications/Dynamsoft" \
    -maxdepth 3 -name "DynamicWebTWAINService.app" -type d 2>/dev/null | head -1
}

install_dwt() {
  echo "==> Downloading Dynamic Web TWAIN Service installer..."
  local pkg
  pkg="$TMPDIR/DynamsoftServiceSetup.pkg"
  if ! curl -L -sS -m 300 -o "$pkg" "$DWT_SERVICE_PKG_URL"; then
    echo "    download failed (network?). Install it manually from:"
    echo "    $DWT_SERVICE_PKG_URL" >&2
    return 1
  fi
  if [[ ! -s "$pkg" ]]; then
    echo "    downloaded file is empty; install manually from:" >&2
    echo "    $DWT_SERVICE_PKG_URL" >&2
    rm -f "$pkg"
    return 1
  fi
  echo "==> Installing Dynamic Web TWAIN Service (admin password may be requested)"
  osascript -e "do shell script \"installer -pkg '$pkg' -target /\" with administrator privileges"
  rm -f "$pkg"
}

start_dwt() {
  # Prefer (re)loading the LaunchAgent installed by the pkg.
  local agent_plist="$HOME/Library/LaunchAgents/com.dynamsoft.dynamicwebtwainservicex64.plist"
  if [[ -f "$agent_plist" ]]; then
    launchctl bootout "gui/$UID_NUM" "$agent_plist" >/dev/null 2>&1 || true
    if launchctl bootstrap "gui/$UID_NUM" "$agent_plist" >/dev/null 2>&1; then
      return 0
    fi
  fi
  # Fall back to opening the installed service app.
  local app
  app="$(find_dwt_app)"
  if [[ -n "$app" ]]; then
    open -a "$app" && return 0
  fi
  return 1
}

wait_for_dwt() {
  local i
  for i in $(seq 1 20); do
    if dwt_running; then return 0; fi
    sleep 1
  done
  return 1
}

# --- 1. App + Bonjour --------------------------------------------------------
if [[ "$skip_build" -ne 1 ]]; then
  "$SCRIPT_DIR/build_and_install.sh"
else
  echo "==> Skipping app build/install (--skip-build)"
fi

# --- 2. Dynamic Web TWAIN Service ---------------------------------------------
if [[ "$skip_dwt" -eq 1 || "$INSTALL_DWT_SERVICE" == "never" ]]; then
  echo "==> Skipping Dynamic Web TWAIN Service setup"
else
  if dwt_running; then
    echo "==> Dynamic Web TWAIN Service already running at $DWT_SERVICE_URL"
  else
    if [[ "$INSTALL_DWT_SERVICE" == "always" ]] || [[ -z "$(find_dwt_app)" ]]; then
      install_dwt || echo "    (continuing without it)"
    fi
    echo "==> Starting Dynamic Web TWAIN Service..."
    start_dwt || true
    if wait_for_dwt; then
      echo "==> Dynamic Web TWAIN Service is up at $DWT_SERVICE_URL"
    else
      echo "warning: DWT Service is not responding at $DWT_SERVICE_URL yet." >&2
      echo "         Try opening it manually, or check the browser demo path." >&2
    fi
  fi
fi

# --- 3. Python venv ------------------------------------------------------------
VENV="$PROJECT_DIR/.venv"
if [[ "$skip_python" -eq 1 ]]; then
  echo "==> Skipping Python environment setup (--skip-python)"
else
  if [[ ! -x "$VENV/bin/python" ]]; then
    echo "==> Creating Python venv at $VENV"
    python3 -m venv "$VENV"
  fi
  echo "==> Installing Python dependencies (requirements.txt)"
  "$VENV/bin/pip" install --quiet -r "$SCRIPT_DIR/requirements.txt"
fi

# --- Summary -------------------------------------------------------------------
echo ""
echo "=================================================================="
echo " Setup complete."
echo "=================================================================="
echo " You can now scan with VirtualScanner:"
echo "   - Image Capture app:  'Virtual Scanner EX/AF' should be listed"
echo "   - Browser demo:       https://demo3.dynamsoft.com/web-twain/"
echo "   - CLI verify:         $VENV/bin/python $SCRIPT_DIR/verify_scanner.py"
echo ""
echo " Note: the bundled Dynamsoft trial license in Scripts/scanner.config"
echo " has EXPIRED (local CLI createJob returns 403). The browser demo works"
echo " because Dynamsoft injects a fresh server-side license. For local CLI"
echo " scanning, put your own key in DWT_LICENSE_KEY in Scripts/scanner.config."
