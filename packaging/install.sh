#!/bin/sh
# corasip installer (M6C). Installs the release tarball for the current user, under ~/.local by default.
#   sh install.sh --from <tarball.tar.gz>        install a downloaded tarball (its .sha256 must sit beside it)
#   curl -fsSL <url>/install.sh | sh             installs the latest release from <url> (CORASIP_URL)
# Refuses to run as root unless --allow-root is given. The release signature is checked when the release key is
# pinned (packaging/release-key.fingerprint); --no-signature-check is for test builds only. Records everything it creates in a manifest so the
# uninstaller removes exactly that. The signature check is not in this version: the release signing key is an
# owner decision; the SHA-256 check is.
set -eu
PREFIX="${CORASIP_PREFIX:-$HOME/.local}"
VERSION="${CORASIP_VERSION:-0.1.0}"
FROM=""
ALLOW_ROOT=0
NO_SIG=0
while [ $# -gt 0 ]; do
    case "$1" in
        --from) FROM="$2"; shift 2 ;;
        --prefix) PREFIX="$2"; shift 2 ;;
        --allow-root) ALLOW_ROOT=1; shift ;;
        --no-signature-check) NO_SIG=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
if [ "$(id -u)" -eq 0 ] && [ "$ALLOW_ROOT" -ne 1 ]; then
    echo "refusing to run as root; use --allow-root if this is intended" >&2; exit 1
fi
case "$(uname -m)" in
    x86_64) ARCH=x86_64 ;;
    *) echo "no release for $(uname -m) yet (x86_64 only)" >&2; exit 1 ;;
esac
NAME="corasip-${VERSION}-linux-${ARCH}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if [ -n "$FROM" ]; then
    cp "$FROM" "$WORK/$NAME.tar.gz"; cp "$FROM.sha256" "$WORK/$NAME.tar.gz.sha256"
    [ "$NO_SIG" -eq 1 ] || cp "$FROM.sha256.asc" "$WORK/$NAME.tar.gz.sha256.asc"
else
    : "${CORASIP_URL:?set CORASIP_URL or use --from}"
    curl -fsSL -o "$WORK/$NAME.tar.gz" "$CORASIP_URL/$NAME.tar.gz"
    curl -fsSL -o "$WORK/$NAME.tar.gz.sha256" "$CORASIP_URL/$NAME.tar.gz.sha256"
    [ "$NO_SIG" -eq 1 ] || curl -fsSL -o "$WORK/$NAME.tar.gz.sha256.asc" "$CORASIP_URL/$NAME.tar.gz.sha256.asc"
fi
(cd "$WORK" && sha256sum -c "$NAME.tar.gz.sha256" >/dev/null) || { echo "checksum does not match: not installed" >&2; exit 1; }
echo "checksum OK"
if [ "$NO_SIG" -ne 1 ]; then
    KEYFPR="$(cat "$(dirname "$0")/release-key.fingerprint" 2>/dev/null || true)"
    [ -n "$KEYFPR" ] || { echo "no pinned release key yet (packaging/release-key.fingerprint): not installed" >&2; exit 1; }
    command -v gpg >/dev/null 2>&1 || { echo "gpg is needed to check the release signature" >&2; exit 1; }
    GNUPGHOME="$WORK/gnupg"; export GNUPGHOME; mkdir -p "$GNUPGHOME"; chmod 700 "$GNUPGHOME"
    gpg --batch --import "$(dirname "$0")/release-key.asc" 2>/dev/null
    gpg --batch --verify "$WORK/$NAME.tar.gz.sha256.asc" "$WORK/$NAME.tar.gz.sha256" 2>/dev/null \
        || { echo "release signature does not verify: not installed" >&2; exit 1; }
    gpg --batch --with-colons --fingerprint 2>/dev/null | grep -q "^fpr:::::::::$KEYFPR:" \
        || { echo "signature is not from the pinned release key: not installed" >&2; exit 1; }
    echo "signature OK (release key $KEYFPR)"
fi

APP="$PREFIX/share/corasip/$NAME"
BIN="$PREFIX/bin"; APPS="$PREFIX/share/applications"; ICONS="$PREFIX/share/icons/hicolor/scalable/apps"
MANIFEST="$PREFIX/share/corasip/install-manifest.txt"
mkdir -p "$PREFIX/share/corasip" "$BIN" "$APPS" "$ICONS"
[ -e "$MANIFEST" ] && { echo "already installed (see $MANIFEST); run uninstall.sh first" >&2; exit 1; }
tar -C "$WORK" -xzf "$WORK/$NAME.tar.gz"
mv "$WORK/$NAME" "$APP"
ln -s "$APP/bin/corasip" "$BIN/corasip"
cp "$APP/share/corasip.svg" "$ICONS/corasip.svg"
sed "s|^Exec=.*|Exec=$BIN/corasip %u|" "$(dirname "$0")/corasip.desktop" > "$APPS/corasip.desktop" 2>/dev/null \
    || printf '[Desktop Entry]\nType=Application\nName=corasip\nGenericName=SIP softphone\nExec=%s/corasip %%u\nIcon=corasip\nTerminal=false\nCategories=Network;Telephony;\nMimeType=x-scheme-handler/sip;x-scheme-handler/tel;\n' "$BIN" > "$APPS/corasip.desktop"
{
    echo "$APP"; echo "$BIN/corasip"; echo "$ICONS/corasip.svg"; echo "$APPS/corasip.desktop"; echo "$MANIFEST"
} > "$MANIFEST"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
echo "installed corasip $VERSION to $APP"
echo "launch with: $BIN/corasip   (add $BIN to PATH if it is not there)"
