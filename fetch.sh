#!/usr/bin/env bash
# =============================================================================
# fetch.sh — download prebuilt firmware from GitHub Releases into build/bin/,
# so you can flash without building. This is the path to use on a Raspberry Pi:
# the firmware is built in Simplicity Studio, which is x86_64 only.
#
#   ./fetch.sh            # latest release
#   ./fetch.sh v1.0.13    # a specific tag
#
# Downloads the wired-flash image (.hex) and the OTA image (.ota). The
# bootloader is NOT downloaded — it never changes between firmware releases and
# is committed at bin/bootloader-combined.s37, so a checkout already has it.
#
# Then:  ./flash.sh app      (or ./flash.sh all, for a first-ever install)
#
# Requires: curl.
# =============================================================================
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

REPO="semicolonmystery/07087-2-Enhanced"
BASENAME="TS1001_TYZB01_7qf81wty_Enhanced"
TAG="${1:-}"

command -v curl >/dev/null 2>&1 || { echo "!! curl not found: sudo apt install -y curl"; exit 1; }

mkdir -p build/bin

if [[ -z "$TAG" ]]; then
    echo ">> finding latest release..."
    JSON="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest")" \
        || { echo "!! could not query releases (none published yet?)"; exit 1; }
    TAG="$(printf '%s' "$JSON" | grep -m1 '"tag_name"' | sed -E 's/.*"tag_name": *"([^"]+)".*/\1/')"
    [[ -n "$TAG" ]] || { echo "!! no release found"; exit 1; }
fi

# Assets are named with the version, e.g. TS1001_..._Enhanced-v1.0.13.hex
echo ">> release: $TAG"
for ext in hex ota; do
    f="$BASENAME-$TAG.$ext"
    url="https://github.com/$REPO/releases/download/$TAG/$f"
    echo ">> downloading $f"
    curl -fSL "$url" -o "build/bin/$BASENAME.$ext" \
        || { echo "!! failed to download $f from $url"; exit 1; }
done

echo
ls -l build/bin/
echo
echo ">> saved to build/bin/ — flash with:  ./flash.sh app"
echo "   (first-ever install, bootloader included:  ./flash.sh all)"
