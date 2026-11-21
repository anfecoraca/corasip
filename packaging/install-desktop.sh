#!/bin/sh
# Installs the corasip launcher and icon for the current user (no root needed):
#   - icon:          ~/.local/share/icons/hicolor/scalable/apps/corasip.svg
#   - menu entry:    ~/.local/share/applications/corasip.desktop
#   - desktop icon:  the Desktop folder (if it exists)
# Usage: packaging/install-desktop.sh <path-to-corasip-binary>
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
ICON_DIR="$HOME/.local/share/icons/hicolor/scalable/apps"
APP_DIR="$HOME/.local/share/applications"
mkdir -p "$ICON_DIR" "$APP_DIR"
cp "$ROOT/assets/corasip.svg" "$ICON_DIR/corasip.svg"
sed "s|^Exec=.*|Exec=$BIN %u|" "$ROOT/packaging/corasip.desktop" > "$APP_DIR/corasip.desktop"
chmod 644 "$APP_DIR/corasip.desktop"
DESKTOP="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
if [ -d "$DESKTOP" ]; then
    cp "$APP_DIR/corasip.desktop" "$DESKTOP/corasip.desktop"
    chmod +x "$DESKTOP/corasip.desktop"
fi
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APP_DIR" 2>/dev/null || true
# sip: and tel: links open corasip for this user (M5E).
if command -v xdg-mime >/dev/null 2>&1; then
    xdg-mime default corasip.desktop x-scheme-handler/sip x-scheme-handler/tel 2>/dev/null || true
fi
echo "installed: menu entry, icon, and desktop icon (if the Desktop folder exists)"
