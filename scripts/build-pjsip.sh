#!/bin/sh
# Build PJSIP statically for Linux with the libraries corasip needs (OpenSSL for TLS and SRTP, Opus, ALSA).
# Output goes to third_party/pjproject-install. Run scripts/fetch-pjsip.sh first.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/pjproject"
PREFIX="$ROOT/third_party/pjproject-install"
cd "$SRC"
./configure --prefix="$PREFIX" \
    --disable-shared --enable-static \
    --disable-video --disable-v4l2 --disable-sdl --disable-ffmpeg \
    --disable-libyuv --disable-openh264 --disable-vpx \
    --with-opus=/usr --with-ssl=${OPENSSL_PREFIX:-/usr}
make -j"$(nproc)" dep
make -j"$(nproc)"
make install
