#!/bin/sh
# Install-matrix check (M6E), run inside a distribution container with the release tarball at /in/tarball.
# 1. installs the desktop libraries a normal install has (what corasip needs beyond the bundled Qt),
# 2. unpacks the tarball, checks that every library resolves,
# 3. starts the app with Qt's offscreen display for a few seconds: it must still be running (exit 124 from timeout).
set -eu
. /etc/os-release
case "$ID" in
    ubuntu|debian|linuxmint) apt-get update -qq >/dev/null 2>&1 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        libxkbcommon0 libxkbcommon-x11-0 libfontconfig1 libfreetype6 libdbus-1-3 libgl1 libglib2.0-0 \
        libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-image0 libxcb-render-util0 libxcb-shape0 \
        libxcb-xinerama0 libxcb-xkb1 libopengl0 libegl1 >/dev/null
        for a in libasound2t64 libasound2; do apt-get install -y -qq $a >/dev/null 2>&1 && break; done ;;
    fedora) dnf install -y -q libxkbcommon libxkbcommon-x11 fontconfig freetype dbus-libs mesa-libGL glib2 \
        xcb-util-cursor xcb-util-image xcb-util-keysyms xcb-util-renderutil libglvnd-opengl libglvnd-egl libX11 xcb-util-wm alsa-lib >/dev/null ;;
    rocky|almalinux|rhel|centos) dnf install -y -q libxkbcommon libxkbcommon-x11 fontconfig freetype dbus-libs \
        mesa-libGL glib2 xcb-util-image xcb-util-keysyms xcb-util-renderutil xcb-util-wm libglvnd-opengl libglvnd-egl libX11 alsa-lib >/dev/null ;;
    arch) pacman -Sy --noconfirm -q libxkbcommon libxkbcommon-x11 fontconfig freetype2 dbus mesa glib2 \
        xcb-util-cursor xcb-util-image xcb-util-keysyms xcb-util-renderutil xcb-util-wm libglvnd libx11 alsa-lib >/dev/null ;;
    opensuse*|sles) zypper -n -q install libxkbcommon0 libxkbcommon-x11-0 fontconfig freetype2 libdbus-1-3 \
        Mesa-libGL1 libglib-2_0-0 libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-image0 \
        libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libglvnd libX11-6 libgthread-2_0-0 libasound2 >/dev/null ;;
    *) echo "unknown distribution: $ID"; exit 2 ;;
esac

mkdir -p /opt && tar -C /opt -xzf /in/tarball.tar.gz
APP=$(ls -d /opt/corasip-*-linux-*/)
echo "== $PRETTY_NAME: unpacked $APP"
export LD_LIBRARY_PATH="$APP/lib"
MISSING=$(ldd "$APP/bin/corasip.bin" "$APP"/lib/qt-plugins/platforms/libqxcb.so 2>/dev/null | grep "not found" | sort -u || true)
if [ -n "$MISSING" ]; then
    echo "FAIL: missing libraries:"; echo "$MISSING"; exit 1
fi
echo "libraries: all resolve"
set +e
QT_QPA_PLATFORM=offscreen timeout 6 "$APP/bin/corasip" >/tmp/run.log 2>&1
RC=$?
set -e
if [ "$RC" -eq 124 ]; then echo "PASS: launched and stayed running for 6 s"; exit 0; fi
echo "FAIL: exit $RC"; tail -5 /tmp/run.log; exit 1
