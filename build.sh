#!/bin/sh
# Build subsdk9 + main.npdm into deploy/ inside the official devkitA64 container.
set -e
cd "$(dirname "$0")"

runtime=$(command -v podman || command -v docker) || {
    echo "need podman or docker" >&2
    exit 1
}

# Mounted at /src, so exlaunch names its intermediate outputs src.*.
"$runtime" run --rm -v "$PWD":/src:Z -w /src docker.io/devkitpro/devkita64 make -j"$(nproc)" "$@"
