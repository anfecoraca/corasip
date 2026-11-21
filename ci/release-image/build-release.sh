#!/bin/sh
# Builds corasip inside the release image and checks the glibc it needs (M6A).
# Usage: ci/release-image/build-release.sh <repo-dir> <out-dir>   (run where docker works)
set -eu
REPO="$1"; OUT="$2"; IMG=corasip-release:m6a
mkdir -p "$OUT"
docker build -q -t "$IMG" -f "$REPO/ci/release-image/Dockerfile" "$REPO/ci/release-image" >/dev/null
docker run --rm \
    -e GIT_CONFIG_COUNT=2 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/work \
    -e GIT_CONFIG_KEY_1=safe.directory -e GIT_CONFIG_VALUE_1=/work/third_party/pjproject \
    -v "$REPO:/work" -v "$OUT:/out" -w /work "$IMG" sh -c '
    git config --global --add safe.directory "*"
    set -eu
    sh scripts/fetch-pjsip.sh
    sh scripts/build-pjsip.sh >/tmp/pjsip-build.log 2>&1 || { tail -n 50 /tmp/pjsip-build.log; exit 1; }
    cmake -S . -B /tmp/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/Qt/6.4.2/gcc_64 >/dev/null
    LIBRARY_PATH=/opt/openssl3/lib cmake --build /tmp/build --target corasip
    cp /tmp/build/corasip /out/corasip
    echo "glibc needed:"; objdump -T /out/corasip | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1
'
