# Build environment for compiling the firmware from source with slc-cli,
# without Simplicity Studio. amd64 — slc-cli and the ARM toolchain are Linux
# x86_64 binaries. Used by build.sh locally and by the ci/release/bootloader
# GitHub Actions workflows (tools/slc-install.sh, tools/slc-build.sh).
#
# slc-cli bundles its own Java runtime, so no separate JDK is installed here.
FROM --platform=linux/amd64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        make curl unzip tar git ca-certificates python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
