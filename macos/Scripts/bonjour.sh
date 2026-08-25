#!/bin/bash
#
# bonjour.sh - manage the VirtualScanner Bonjour advertisement LaunchAgent.
#
# macOS discovers the VirtualScanner ICA device via Bonjour (_scanner._tcp),
# NOT from the mere presence of VirtualScanner.app on disk. While the
# `dns-sd -R "Virtual Scanner" _scanner._tcp ...` advertisement is running,
# icdd's Bonjour discovery finds and launches the device. A manually started
# dns-sd dies on reboot / terminal close, so the scanner silently disappears
# after a restart -- this agent fixes that (RunAtLoad + KeepAlive).
#
# Usage:
#   ./Scripts/bonjour.sh install    # copy plist to ~/Library/LaunchAgents and start it
#   ./Scripts/bonjour.sh start      # load the agent (idempotent)
#   ./Scripts/bonjour.sh stop       # unload the agent
#   ./Scripts/bonjour.sh restart    # stop then start
#   ./Scripts/bonjour.sh status     # is it loaded / running / browsable?
#
# build_and_install.sh calls `bonjour.sh install` automatically, so you usually
# don't need to run this by hand.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLIST_SRC="$SCRIPT_DIR/com.dynamsoft.virtualscanner.bonjour.plist"
PLIST_NAME="$(basename "$PLIST_SRC")"
LAUNCH_DIR="$HOME/Library/LaunchAgents"
PLIST_DST="$LAUNCH_DIR/$PLIST_NAME"
LABEL="com.dynamsoft.virtualscanner.bonjour"
LAUNCH_TARGET="gui/$(id -u)"

cmd="${1:-status}"

ensure_plist() {
  if [[ ! -f "$PLIST_SRC" ]]; then
    echo "error: $PLIST_SRC not found" >&2
    exit 1
  fi
}

install() {
  ensure_plist
  mkdir -p "$LAUNCH_DIR"
  cp "$PLIST_SRC" "$PLIST_DST"
  echo "Installed $PLIST_DST"
  start
}

start() {
  ensure_plist
  if ! launchctl print "$LAUNCH_TARGET/$LABEL" >/dev/null 2>&1; then
    launchctl bootstrap "$LAUNCH_TARGET" "$PLIST_DST"
    echo "Started $LABEL"
  else
    echo "$LABEL already running"
  fi
}

stop() {
  # bootout is idempotent: ignore the "not currently loaded" error.
  launchctl bootout "$LAUNCH_TARGET" "$PLIST_DST" >/dev/null 2>&1 || true
  echo "Stopped $LABEL"
}

restart() {
  stop
  start
}

status() {
  local loaded=no running=no
  if launchctl print "$LAUNCH_TARGET/$LABEL" >/dev/null 2>&1; then
    loaded=yes
  fi
  if pgrep -fq "dns-sd -R Virtual Scanner" 2>/dev/null; then
    running=yes
  fi
  echo "LaunchAgent loaded : $loaded"
  echo "dns-sd process     : $running"
  if [[ "$running" == "yes" ]]; then
    ps aux | grep '[d]ns-sd -R Virtual Scanner' | sed 's/^/  /'
  fi

  # Briefly browse Bonjour for _scanner._tcp and report whether the
  # advertisement is visible (matching mdl/mfg makes icdd load the device).
  echo "Bonjour browse     :"
  local browse_out
  browse_out="$( { dns-sd -B _scanner._tcp local & pid=$!; sleep 2; kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; } 2>/dev/null )" || true
  if grep -q "Virtual Scanner" <<<"$browse_out"; then
    grep "Virtual Scanner" <<<"$browse_out" | sed 's/^/  /'
  else
    echo "  (no _scanner._tcp advertisements seen)"
  fi
}

case "$cmd" in
  install) install ;;
  start)   start ;;
  stop)    stop ;;
  restart) restart ;;
  status)  status ;;
  *)
    echo "usage: $0 {install|start|stop|restart|status}" >&2
    exit 1
    ;;
esac
