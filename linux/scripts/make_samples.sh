#!/usr/bin/env bash
#
# make_samples.sh - generates the PNM sample image set handed out by the
# virtual scanner, from the PNG images bundled with this repository.
#
#   ./scripts/make_samples.sh [source-dir]
#
# The SANE backend only understands the PNM family (P5/P6), so the images are
# converted with ImageMagick. Everything is configurable through the
# environment:
#
#   SAMPLES_COUNT    11      how many images to convert (0 = all of them)
#   SAMPLES_WIDTH    700     scale down to this maximum width
#   SAMPLES_OUT_DIR  ../samples
#
# All 11 sample images are shipped so that the backend can be used as a multi
# image scanner right after the installation. PNM is uncompressed, hence the
# scale-down; raise SAMPLES_WIDTH for more detail and a bigger package.
#
# Examples:
#   ./scripts/make_samples.sh                                   # all 11, 700 px wide
#   SAMPLES_WIDTH=1200 ./scripts/make_samples.sh                # all 11, 1200 px wide
#   SAMPLES_COUNT=3 ./scripts/make_samples.sh                   # only the first 3
#   ./scripts/make_samples.sh ~/Pictures/documents
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$LINUX_DIR/.." && pwd)"

SOURCE_DIR="${1:-$REPO_ROOT/macos/Resources/SampleImages}"
OUT_DIR="${SAMPLES_OUT_DIR:-$LINUX_DIR/samples}"
COUNT="${SAMPLES_COUNT:-11}"
MAX_WIDTH="${SAMPLES_WIDTH:-700}"

if command -v magick >/dev/null 2>&1; then
  MAGICK=(magick)
elif command -v convert >/dev/null 2>&1; then
  MAGICK=(convert)
else
  echo "error: ImageMagick is required (install it with: sudo apt install imagemagick)" >&2
  exit 1
fi

if [[ ! -d "$SOURCE_DIR" ]]; then
  echo "error: source directory '$SOURCE_DIR' does not exist" >&2
  exit 1
fi

shopt -s nullglob nocaseglob
FILES=("$SOURCE_DIR"/*.png "$SOURCE_DIR"/*.jpg "$SOURCE_DIR"/*.jpeg)
shopt -u nullglob nocaseglob

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "error: no PNG/JPEG images found in '$SOURCE_DIR'" >&2
  exit 1
fi

# Natural order, so that 2.png comes before 10.png.
mapfile -t FILES < <(printf '%s\n' "${FILES[@]}" | sort -V)

mkdir -p "$OUT_DIR"

converted=0
for file in "${FILES[@]}"; do
  if [[ "$COUNT" -gt 0 && "$converted" -ge "$COUNT" ]]; then
    break
  fi

  base="$(basename "$file")"
  name="${base%.*}"
  output="$OUT_DIR/$name.pnm"

  echo "==> $base -> $(basename "$output")"
  "${MAGICK[@]}" "$file" -auto-orient \
    -resize "${MAX_WIDTH}x${MAX_WIDTH}>" \
    -colorspace sRGB -depth 8 \
    "$output"

  converted=$((converted + 1))
done

echo ""
echo "==> $converted image(s) written to $OUT_DIR"
echo "    The backend uses this directory as its compiled-in default"
echo "    ($LINUX_DIR/Makefile sets IMAGE_DIR), or point it somewhere else with"
echo "    CUSTOM_SCANNER_IMAGE_DIR=<dir> scanimage -d custom_scanner:virtual > out.pnm"
