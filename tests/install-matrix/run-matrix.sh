#!/bin/sh
# Runs the install matrix over the release tarball in each listed container.
# Usage: run-matrix.sh <staging dir> <repo dir> [image ...]
# The staging dir holds tarball.tar.gz, its .sha256 and .sha256.asc, and a copy of packaging/ (copy it there first).
# Containers need Docker; the signature check needs gpg, which is installed inside the test container first.
set -u
IN="$1"; REPO="$2"; shift 2
IMAGES="${*:-ubuntu:22.04 ubuntu:24.04 debian:12 fedora:latest archlinux:latest opensuse/tumbleweed rockylinux:9}"
for img in $IMAGES; do
    printf '== %s\n' "$img"
    docker run --rm -v "$IN:/in:ro" -v "$REPO/tests/install-matrix:/t:ro" "$img" sh -c '
        if command -v apt-get >/dev/null 2>&1; then apt-get update -qq >/dev/null 2>&1; apt-get install -y -qq gnupg >/dev/null 2>&1
        elif command -v dnf >/dev/null 2>&1; then dnf install -y -q gnupg2 >/dev/null 2>&1
        elif command -v pacman >/dev/null 2>&1; then pacman -Sy --noconfirm gnupg >/dev/null 2>&1
        elif command -v zypper >/dev/null 2>&1; then zypper -n -q install gpg2 >/dev/null 2>&1; fi
        sh /t/check-in-container.sh && sh /t/roundtrip.sh' && echo "PASS $img" || echo "FAIL $img"
done
