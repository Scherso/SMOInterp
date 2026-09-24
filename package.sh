#!/bin/sh
# Zip deploy/ in Eden's load-directory layout: <title id>/SMOInterp/exefs/{subsdk9,main.npdm}.
set -e
cd "$(dirname "$0")"

[ -f deploy/subsdk9 ] && [ -f deploy/main.npdm ] || {
    echo "run ./build.sh first" >&2
    exit 1
}

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

dir="$stage/0100000000010000/SMOInterp/exefs"
mkdir -p "$dir"
cp deploy/subsdk9 deploy/main.npdm "$dir/"

out="$PWD/SMOInterp-$(git describe --tags --always 2>/dev/null || echo dev).zip"
(cd "$stage" && zip -qr "$out" 0100000000010000)
echo "$out"
