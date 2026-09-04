#!/usr/bin/env bash
# tools/slc-build.sh — generate + build one Simplicity Studio project headlessly
# with slc-cli, reproducing what Studio's GUI build does. Used identically by
# CI and local Docker builds (tools/slc-install.sh must have run first).
#
# Usage:
#   tools/slc-build.sh <project-dir> <part-id> <out-dir>
#
#   project-dir   e.g. Zigbee-Remote-_TYZB01_7qf81wty
#                 (expects <project-dir>/<project-dir>.slcp to exist)
#   part-id       e.g. EFR32MG13P732F512GM48
#   out-dir       where slc generates the build tree; build artifacts are
#                 copied out of it into <out-dir>/artifacts/
#
# Requires tools/.cache/env.sh (written by tools/slc-install.sh) to be
# sourced, or SLC_CLI_DIR/TOOLCHAIN_DIR/SDK_DIR already exported.
#
# NOTE: the exact `slc generate` flags and the name of the makefile it emits
# were assembled from Silicon Labs' documented slc-cli usage, but this
# project cannot be built on this (Windows) development machine to pre-verify
# them — see docs/BUILD.md's "CI / CLI build" section. Treat the first real
# CI run as the place these get shaken out, the same way the exact toolchain
# invocation for the other (Telink) project here was clearly iterated against
# a real Docker run.
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

PROJECT_DIR="${1:?usage: slc-build.sh <project-dir> <part-id> <out-dir>}"
PART_ID="${2:?usage: slc-build.sh <project-dir> <part-id> <out-dir>}"
OUT_DIR="${3:?usage: slc-build.sh <project-dir> <part-id> <out-dir>}"

ENV_FILE="tools/.cache/env.sh"
if [[ -f "$ENV_FILE" ]]; then
    # shellcheck source=/dev/null
    source "$ENV_FILE"
fi
: "${SLC_CLI_DIR:?run tools/slc-install.sh first (or export SLC_CLI_DIR/TOOLCHAIN_DIR/SDK_DIR)}"
: "${TOOLCHAIN_DIR:?run tools/slc-install.sh first}"
: "${SDK_DIR:?run tools/slc-install.sh first}"
export PATH="$SLC_CLI_DIR:$TOOLCHAIN_DIR/bin:$PATH"

SLCP="$PROJECT_DIR/$(basename "$PROJECT_DIR").slcp"
if [[ ! -f "$SLCP" ]]; then
    echo "!! $SLCP not found" >&2
    exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

echo ">> slc generate $SLCP -> $OUT_DIR (part $PART_ID)"
slc generate "$SLCP" \
    -s "$SDK_DIR" \
    --with "$PART_ID" \
    -tlcn gcc \
    -d "$OUT_DIR" \
    --copy-sources

# The generated makefile's name isn't fixed across slc-cli versions (it has
# been seen both as <project>.Makefile and as a plain `makefile` alongside
# per-toolchain subdirectories, matching what Studio's own IDE build produces
# — see the tracked "GNU ARM v12.2.1 - Default/makefile" layout this repo
# already has from Studio builds). Detect rather than hardcode, and fail
# loudly with what was actually found if detection is ambiguous.
mapfile -t MAKEFILES < <(find "$OUT_DIR" -maxdepth 2 \( -iname "*.makefile" -o -iname "makefile" -o -iname "GNUmakefile" \) | sort)
if [[ ${#MAKEFILES[@]} -eq 0 ]]; then
    echo "!! no generated makefile found under $OUT_DIR. Contents:" >&2
    find "$OUT_DIR" -maxdepth 2 >&2
    exit 1
elif [[ ${#MAKEFILES[@]} -gt 1 ]]; then
    echo "!! multiple candidate makefiles found, refusing to guess:" >&2
    printf '  %s\n' "${MAKEFILES[@]}" >&2
    exit 1
fi
MAKEFILE="${MAKEFILES[0]}"
BUILD_SUBDIR="$(dirname "$MAKEFILE")"

echo ">> make -C $BUILD_SUBDIR -f $(basename "$MAKEFILE")"
make -C "$BUILD_SUBDIR" -f "$(basename "$MAKEFILE")" \
    ARM_GCC_DIR="$TOOLCHAIN_DIR" \
    -j"$(nproc 2>/dev/null || echo 2)"

# ---- collect artifacts ---------------------------------------------------
ARTIFACT_DIR="$OUT_DIR/artifacts"
mkdir -p "$ARTIFACT_DIR"
FOUND=0
for ext in s37 hex bin; do
    while IFS= read -r -d '' f; do
        cp "$f" "$ARTIFACT_DIR/"
        echo ">> collected $(basename "$f")"
        FOUND=1
    done < <(find "$BUILD_SUBDIR" -maxdepth 1 -iname "*.$ext" -print0)
done
if [[ "$FOUND" -eq 0 ]]; then
    echo "!! build finished but no .s37/.hex/.bin found under $BUILD_SUBDIR" >&2
    exit 1
fi

echo ">> artifacts in $ARTIFACT_DIR:"
ls -la "$ARTIFACT_DIR"
