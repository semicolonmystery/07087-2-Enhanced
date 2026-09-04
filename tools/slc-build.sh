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
: "${SLC_CLI_DIR:?run tools/slc-install.sh first (or export SLC_CLI_DIR/TOOLCHAIN_DIR/SDK_DIR/ZAP_DIR)}"
: "${TOOLCHAIN_DIR:?run tools/slc-install.sh first}"
: "${SDK_DIR:?run tools/slc-install.sh first}"
: "${ZAP_DIR:?run tools/slc-install.sh first}"
: "${COMMANDER_DIR:?run tools/slc-install.sh first}"
export PATH="$SLC_CLI_DIR:$TOOLCHAIN_DIR/bin:$COMMANDER_DIR:$PATH"

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
    -o makefile \
    --copy-sources \
    --tool-path "$ZAP_DIR"

# slc generate's Application Configurator writes a fresh, DEFAULT
# <component>-config.h for every configurable component — it does not know
# these files are already persisted, hand-tuned project state (per
# CLAUDE.md: "config/*.h... persist normally and are picked up by the next
# Studio build"). Studio's IDE build never regenerates a config/*.h that
# already exists; slc-cli's headless generate has no equivalent, so restore
# our tracked values over its defaults before compiling. Without this,
# app.c's own #if guards (mirrored-constant cross-checks) catch the drift
# and fail the build — which is what surfaced this in the first place.
if [[ -d "$PROJECT_DIR/config" ]]; then
    echo ">> restoring tracked config/*.h over slc's regenerated defaults"
    mkdir -p "$OUT_DIR/config"
    cp -f "$PROJECT_DIR"/config/*.h "$OUT_DIR/config/"
fi

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
# POST_BUILD_EXE=true: a project with a `post_build:` profile in its .slcp
# (the bootloader) generates a Makefile step that invokes Studio's own
# post-build tool via $(POST_BUILD_EXE) — undefined here since that tool
# isn't something slt distributes, and the Makefile treats it as a hard
# error rather than skipping. `true` makes that step a no-op regardless of
# what arguments it's called with; the actual combine-first-stage-and-main-
# stage step this would have run is done explicitly in bootloader.yml
# instead (see its "Combine first-stage + main-stage" step).
make -C "$BUILD_SUBDIR" -f "$(basename "$MAKEFILE")" \
    ARM_GCC_DIR="$TOOLCHAIN_DIR" \
    POST_BUILD_EXE=true \
    -j"$(nproc 2>/dev/null || echo 2)"

# The "makefile" output type links a .out but doesn't include the default
# ELF->s37/hex/bin conversion Studio's own IDE build config runs as a matter
# of course after linking. Reproduce it with Commander if nothing already
# produced those formats (the bootloader's *custom* post_build combine step,
# when it actually runs instead of the POST_BUILD_EXE=true no-op above,
# would already have done this itself).
if ! find "$BUILD_SUBDIR" -maxdepth 1 -iname "*.s37" -print -quit | grep -q .; then
    OUT_FILE="$(find "$BUILD_SUBDIR" -iname "*.out" | head -1)"
    if [[ -n "$OUT_FILE" ]]; then
        BASE="$(basename "$OUT_FILE" .out)"
        echo ">> converting $OUT_FILE -> $BASE.{s37,hex,bin} via Commander"
        commander convert "$OUT_FILE" --output "$BUILD_SUBDIR/$BASE.s37"
        commander convert "$OUT_FILE" --output "$BUILD_SUBDIR/$BASE.hex"
        commander convert "$OUT_FILE" --output "$BUILD_SUBDIR/$BASE.bin"
    fi
fi

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
