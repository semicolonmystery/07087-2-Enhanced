#!/usr/bin/env bash
# Local Docker build. Builds the same way CI does: tools/slc-install.sh
# fetches slc-cli + the ARM toolchain + gecko_sdk into tools/.cache/ (cached
# on the host across runs), then tools/slc-build.sh generates and compiles a
# project, inside a Debian/Ubuntu container. No host compiler or Simplicity
# Studio install is used.
#
#   ./build.sh                                          # build the app
#   ./build.sh bootloader-storage-internal-single-512k \
#              EFR32MG13P732F512GM48                     # build the bootloader
#   ./build.sh clean                                     # wipe tools/.cache
#
# Requirements: Docker, and your user in the `docker` group (or run with sudo).
#
# NOTE: slc-cli and the ARM toolchain are x86_64 Linux binaries, so the build
# image is linux/amd64. On an x86_64 machine this runs natively. On ARM
# (e.g. a Raspberry Pi) it runs under QEMU emulation, which is SLOW — on a Pi
# you almost certainly want ./fetch.sh (download the released firmware)
# instead of building.
set -euo pipefail
cd "$(dirname "$0")"

IMAGE=ts1001-build
PROJECT="${1:-Zigbee-Remote-_TYZB01_7qf81wty}"
PART="${2:-EFR32MG13P732F512GM48}"

if [[ "$PROJECT" == "clean" ]]; then
    rm -rf tools/.cache
    echo ">> removed tools/.cache"
    exit 0
fi

# ---- Docker present? -------------------------------------------------------
if ! command -v docker >/dev/null 2>&1; then
    cat >&2 <<'EOF'
!! docker not found.

On a Raspberry Pi you probably don't want to build at all — the toolchain is
x86_64 and only runs here under slow emulation. Instead download the released
firmware:

    ./fetch.sh && ./flash.sh app -r

To install Docker anyway:  curl -fsSL https://get.docker.com | sudo sh
EOF
    exit 1
fi

# ---- Docker daemon reachable? ----------------------------------------------
if ! docker info >/dev/null 2>&1; then
    cat >&2 <<'EOF'
!! cannot talk to the Docker daemon (permission or not running).

Fix permissions (then log out/in):   sudo usermod -aG docker "$USER"
Or just run this script with sudo:    sudo ./build.sh
EOF
    exit 1
fi

# ---- ARM host: set up amd64 emulation --------------------------------------
ARCH="$(uname -m)"
if [[ "$ARCH" != "x86_64" && "$ARCH" != "amd64" ]]; then
    echo ">> host is $ARCH; slc-cli/the toolchain need amd64 emulation (slow)."
    echo ">> tip: on a Pi, './fetch.sh' downloads a released firmware in seconds."
    echo ">> installing binfmt/qemu amd64 handler..."
    docker run --privileged --rm tonistiigi/binfmt --install amd64 >/dev/null 2>&1 \
        || echo "!! binfmt install failed; build may not work. Consider ./fetch.sh"
fi

echo ">> building docker image $IMAGE"
docker build -q -t "$IMAGE" -f Dockerfile . >/dev/null

OUT_DIR="build/$PROJECT"
echo ">> running: tools/slc-install.sh && tools/slc-build.sh $PROJECT $PART $OUT_DIR"
docker run --rm -v "$PWD":/work -w /work "$IMAGE" \
    bash -c "tools/slc-install.sh && tools/slc-build.sh '$PROJECT' '$PART' '$OUT_DIR'"

echo ">> artifacts: $OUT_DIR/artifacts/"
