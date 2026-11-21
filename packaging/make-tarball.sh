#!/bin/sh
# Builds the release tarball (M6B): corasip binary, the Qt 6.4.2 libraries and plugins it needs, a launcher, and a SHA-256.
# Run inside the release image, where Qt is at $QTDIR. Usage: make-tarball.sh <binary> <outdir> <version> <arch>
set -eu
BIN="$1"; OUT="$2"; VER="$3"; ARCH="$4"
QTDIR="${QTDIR:-/opt/Qt/6.4.2/gcc_64}"
NAME="corasip-${VER}-linux-${ARCH}"
STAGE="$(mktemp -d)/${NAME}"
mkdir -p "$STAGE/bin" "$STAGE/lib/qt-plugins/platforms" "$STAGE/lib/qt-plugins/iconengines" \
         "$STAGE/lib/qt-plugins/imageformats" "$STAGE/share"

cp "$BIN" "$STAGE/bin/corasip.bin"
# Stripped: symbols are not needed to run; the size budget is set from the stripped size (SPEC §5.1).
strip --strip-unneeded "$STAGE/bin/corasip.bin"
for plugin in platforms/libqxcb.so platforms/libqoffscreen.so platforms/libqwayland-generic.so platforms/libqwayland-egl.so iconengines/libqsvgicon.so imageformats/libqsvg.so; do
    cp "$QTDIR/plugins/$plugin" "$STAGE/lib/qt-plugins/$plugin"
done
# The Wayland platform plugin loads these helper plugins from its own folders.
for d in wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    cp -r "$QTDIR/plugins/$d" "$STAGE/lib/qt-plugins/"
done

# Every library the binary and the plugins load that comes from the Qt install, plus the few system libraries
# that differ between distributions, is bundled, recursively.
collect() {
    ldd "$1" 2>/dev/null | awk '/=> \//{print $3}' | while read -r lib; do
        case "$lib" in "$QTDIR"/*) if [ ! -e "$STAGE/lib/$(basename "$lib")" ]; then
            cp -L "$lib" "$STAGE/lib/"; strip --strip-unneeded "$STAGE/lib/$(basename "$lib")" 2>/dev/null || true; collect "$lib"; fi ;;
            # Libraries the distributions do not all ship at these versions (OpenSSL 3, Opus, SQLite) are bundled too.
            */libssl.so.3|*/libcrypto.so.3|*/libopus.so.0|*/libsqlite3.so.0) if [ ! -e "$STAGE/lib/$(basename "$lib")" ]; then
            cp -L "$lib" "$STAGE/lib/"; strip --strip-unneeded "$STAGE/lib/$(basename "$lib")" 2>/dev/null || true; collect "$lib"; fi ;;
        esac
    done
}
collect "$STAGE/bin/corasip.bin"
for p in "$STAGE"/lib/qt-plugins/*/*.so; do collect "$p"; done

cp "$(dirname "$0")/../assets/corasip.svg" "$STAGE/share/corasip.svg"
# Licence texts travel with the binary (GPL-3.0 sections 4 and 6): corasip's own licence and notices, the SBOM,
# and the texts of the bundled components (PJSIP is GPL-2.0-or-later, Qt is LGPL-3.0, OpenSSL is Apache-2.0).
LIC="$STAGE/share/licenses"; mkdir -p "$LIC"
cp "$(dirname "$0")/../LICENSE" "$(dirname "$0")/../NOTICE" "$(dirname "$0")/../README.md" "$STAGE/"
cp "$(dirname "$0")/../docs/SBOM.md" "$LIC/SBOM.md"
cp /usr/share/common-licenses/GPL-2 "$LIC/PJSIP-GPL-2.0.txt"
cp /usr/share/common-licenses/LGPL-3 "$LIC/Qt-LGPL-3.0.txt"
cp /usr/share/common-licenses/Apache-2.0 "$LIC/OpenSSL-Apache-2.0.txt"
cp "$(dirname "$0")/../LICENSE" "$LIC/corasip-GPL-3.0.txt"
cat > "$STAGE/bin/corasip" <<'WRAP'
#!/bin/sh
# Starts corasip with the bundled Qt libraries and plugins.
DIR="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"  # the launcher may be a symlink (installer)
LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
QT_PLUGIN_PATH="$DIR/lib/qt-plugins"
export LD_LIBRARY_PATH QT_PLUGIN_PATH
exec "$DIR/bin/corasip.bin" "$@"
WRAP
chmod +x "$STAGE/bin/corasip" "$STAGE/bin/corasip.bin"

mkdir -p "$OUT"
tar -C "$(dirname "$STAGE")" -czf "$OUT/$NAME.tar.gz" "$NAME"
(cd "$OUT" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
echo "built $OUT/$NAME.tar.gz"
