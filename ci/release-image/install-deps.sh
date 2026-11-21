#!/bin/sh
# Installs the release build's packages. The Debian mirror can serve an index that is ahead of its files for a
# while, so the update and install are retried; the image build fails if every attempt fails (M6A).
set -eu
PKGS="build-essential cmake pkg-config git ca-certificates curl python3 python3-dev python3-venv
 libopus-dev libasound2-dev libsqlite3-dev
 libxkbcommon-dev libgl1-mesa-dev libfontconfig1-dev libfreetype-dev libdbus-1-dev
 libx11-dev libx11-xcb-dev libxcb1-dev libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1
 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-dev"
for attempt in 1 2 3 4 5; do
    if apt-get update && apt-get install -y --no-install-recommends $PKGS; then
        rm -rf /var/lib/apt/lists/*
        exit 0
    fi
    echo "package install failed (attempt $attempt), retrying"
    sleep 60
done
exit 1
