#!/bin/sh
# Build corasip-cli for Linux x86_64 inside the pinned build image (M1 step 4).
# Usage: scripts/build-linux.sh [outdir]   (default outdir: dist/)
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="corasip-build:m1"
OUT="${1:-$ROOT/dist}"
mkdir -p "$OUT"

docker build -q -t "$IMG" -f "$ROOT/ci/build-image/Dockerfile" "$ROOT/ci/build-image" >/dev/null

# Run as the invoking user so the repo files stay owned by that user.
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e GIT_CONFIG_COUNT=2 \
    -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/work \
    -e GIT_CONFIG_KEY_1=safe.directory -e GIT_CONFIG_VALUE_1=/work/third_party/pjproject \
    -v "$ROOT:/work" -w /work \
    "$IMG" sh -c '
        set -eu
        sh scripts/fetch-pjsip.sh
        sh scripts/build-pjsip.sh >/tmp/pjsip-build.log 2>&1 || { tail -n 50 /tmp/pjsip-build.log; exit 1; }
        cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release >/dev/null
        cmake --build build/linux
        cp build/linux/corasip-cli dist/corasip-cli-linux-x86_64
    '
echo "built: $OUT/corasip-cli-linux-x86_64"
