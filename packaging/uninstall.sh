#!/bin/sh
# corasip uninstaller (M6D). Removes exactly the paths the installer recorded in its manifest, then the manifest.
# Usage: uninstall.sh [--prefix <dir>]   (default ~/.local, or $CORASIP_PREFIX)
set -eu
PREFIX="${CORASIP_PREFIX:-$HOME/.local}"
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
MANIFEST="$PREFIX/share/corasip/install-manifest.txt"
[ -f "$MANIFEST" ] || { echo "nothing to remove: no manifest at $MANIFEST" >&2; exit 1; }
while read -r path; do
    [ -n "$path" ] || continue
    case "$path" in
        "$PREFIX"/*) ;;
        *) echo "skipping a path outside $PREFIX: $path" >&2; continue ;;
    esac
    if [ -d "$path" ] && [ ! -L "$path" ]; then rm -rf "$path"; else rm -f "$path"; fi
done < "$MANIFEST"
rmdir "$PREFIX/share/corasip" 2>/dev/null || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
echo "corasip removed"
