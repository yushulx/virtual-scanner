#!/usr/bin/env bash
#
# install.sh - one command build + install of the Linux virtual scanner.
#
#   ./scripts/install.sh
#
# It compiles the SANE backend, generates the sample image set when
# ImageMagick is available, installs everything (the backend, its
# configuration, the images and the dll.d drop-in that enables it) and tells
# you how to scan. Administrator rights are used for the installation step
# only.
#
# Options:
#   --uninstall    remove everything the installer copied
#   --no-deps      never install build dependencies automatically
#   --samples      install ImageMagick if needed and generate the sample set
#   --no-samples   skip the sample image generation
#   -y, --yes      answer yes to every question
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BACKEND="custom_scanner"
IMAGE_DIR="/usr/share/custom_scanner/images"

ACTION=install
INSTALL_DEPS=1
WANT_SAMPLES=auto
ASSUME_YES=0

usage() {
  sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

for arg in "$@"; do
  case "$arg" in
    --uninstall)  ACTION=uninstall ;;
    --no-deps)    INSTALL_DEPS=0 ;;
    --samples)    WANT_SAMPLES=yes ;;
    --no-samples) WANT_SAMPLES=no ;;
    -y|--yes)     ASSUME_YES=1 ;;
    -h|--help)    usage; exit 0 ;;
    *) echo "unknown option: $arg" >&2; usage; exit 1 ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "error: this script is meant to run on Linux" >&2
  exit 1
fi

run_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  else
    if ! command -v sudo >/dev/null 2>&1; then
      echo "error: root rights are required and sudo is not installed" >&2
      exit 1
    fi
    sudo "$@"
  fi
}

confirm() {
  [[ "$ASSUME_YES" -eq 1 ]] && return 0
  local answer
  read -r -p "$1 [y/N] " answer
  [[ "$answer" == "y" || "$answer" == "Y" ]]
}

install_packages() {
  # Installs the given packages with the native package manager. The package
  # names differ per distribution, so the caller passes them per manager.
  local apt_pkgs="$1" rpm_pkgs="$2" arch_pkgs="$3" zypp_pkgs="$4"

  if command -v apt-get >/dev/null 2>&1; then
    run_root apt-get update
    run_root apt-get install -y $apt_pkgs
  elif command -v dnf >/dev/null 2>&1; then
    run_root dnf install -y $rpm_pkgs
  elif command -v yum >/dev/null 2>&1; then
    run_root yum install -y $rpm_pkgs
  elif command -v pacman >/dev/null 2>&1; then
    run_root pacman -Sy --needed --noconfirm $arch_pkgs
  elif command -v zypper >/dev/null 2>&1; then
    run_root zypper --non-interactive install $zypp_pkgs
  else
    return 1
  fi
}

has_sane_headers() {
  [[ -f /usr/include/sane/sane.h ]] ||
    [[ -f /usr/local/include/sane/sane.h ]] ||
    pkg-config --exists sane-backends 2>/dev/null
}

# ---------------------------------------------------------------------------

if [[ "$ACTION" == "uninstall" ]]; then
  echo "==> Removing the virtual scanner ($IMAGE_DIR is removed as well)"
  run_root make -C "$LINUX_DIR" uninstall
  echo "==> Done."
  exit 0
fi

# --- build dependencies ----------------------------------------------------
missing=()
command -v make >/dev/null 2>&1 || missing+=(make)
command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || missing+=(compiler)
has_sane_headers || missing+=(sane-headers)

if [[ ${#missing[@]} -gt 0 ]]; then
  echo "==> Missing build dependencies: ${missing[*]}"
  if [[ "$INSTALL_DEPS" -eq 1 ]]; then
    if confirm "Install them now?"; then
      install_packages \
        "build-essential libsane-dev sane-utils" \
        "gcc make sane-backends-devel sane-backends" \
        "base-devel sane" \
        "gcc make sane-backends-devel sane-backends" ||
        echo "warning: could not install the packages automatically" >&2
    fi
  fi
fi

if ! command -v make >/dev/null 2>&1; then
  echo "error: make is required (sudo apt install build-essential)" >&2
  exit 1
fi
if ! has_sane_headers; then
  echo "error: the SANE development headers are required (sudo apt install libsane-dev)" >&2
  exit 1
fi

if command -v scanimage >/dev/null 2>&1; then
  :
else
  echo "note: scanimage (sane-utils) is not installed; it is only needed for testing"
fi

# --- sample images ---------------------------------------------------------
have_magick=0
if command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1; then
  have_magick=1
fi

if [[ "$WANT_SAMPLES" != "no" ]]; then
  if [[ "$have_magick" -eq 0 && "$WANT_SAMPLES" == "yes" && "$INSTALL_DEPS" -eq 1 ]]; then
    if confirm "Install ImageMagick to generate the sample image set?"; then
      install_packages "imagemagick" "ImageMagick" "imagemagick" "ImageMagick" || true
      command -v magick >/dev/null 2>&1 || command -v convert >/dev/null 2>&1 && have_magick=1
    fi
  fi

  if [[ "$have_magick" -eq 1 ]]; then
    echo "==> Generating the sample image set"
    SAMPLES_OUT_DIR="$LINUX_DIR/samples" bash "$SCRIPT_DIR/make_samples.sh"
  else
    echo "note: skipping the sample image set (ImageMagick is not installed)"
    echo "      the package ships test.pnm as a single fallback image"
  fi
fi

# --- build and install -----------------------------------------------------
echo "==> Building the backend"
make -C "$LINUX_DIR" IMAGE_DIR="$IMAGE_DIR"

echo "==> Installing (administrator rights are required)"
run_root make -C "$LINUX_DIR" install IMAGE_DIR="$IMAGE_DIR"

# --- summary ---------------------------------------------------------------
echo ""
echo "=================================================================="
echo " Setup complete."
echo "=================================================================="
echo " Test the virtual scanner:"
echo "   scanimage -L"
echo "   scanimage -d ${BACKEND}:virtual > out.pnm"
echo "   (multi page, one page per image:)"
echo "   scanimage -d ${BACKEND}:virtual --source 'Automatic Document Feeder' --batch=page-%d.pnm"
echo ""
echo " Images are taken from $IMAGE_DIR;"
echo " drop your own *.pnm files in there or point the backend elsewhere with:"
echo "   CUSTOM_SCANNER_IMAGE_DIR=~/Pictures scanimage -d ${BACKEND}:virtual > out.pnm"
echo ""
echo " To remove everything again: $SCRIPT_DIR/install.sh --uninstall"
