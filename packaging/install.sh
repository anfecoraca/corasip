#!/bin/sh
# corasip installer (M6C). Installs the release tarball for the current user, under ~/.local by default.
#   sh install.sh --from <tarball.tar.gz>        install a downloaded tarball (its .sha256 and .sha256.asc sit beside it)
#   curl -fsSL <url>/install.sh | sh             installs the release at <url> (CORASIP_URL), see the README
# Refuses to run as root unless --allow-root is given. The release signature is always checked against the pinned
# release key below; --no-signature-check is for test builds only. Records everything it creates in a manifest so the
# uninstaller removes exactly that.
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
    KEYFPR="E812A201555E3D62A6EDBD99DAB8E159B5794924"
    command -v gpg >/dev/null 2>&1 || { echo "gpg is needed to check the release signature" >&2; exit 1; }
    GNUPGHOME="$WORK/gnupg"; export GNUPGHOME; mkdir -p "$GNUPGHOME"; chmod 700 "$GNUPGHOME"
    cat > "$WORK/release-key.asc" <<'KEY'
-----BEGIN PGP PUBLIC KEY BLOCK-----

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
=0G6t
-----END PGP PUBLIC KEY BLOCK-----
KEY
    gpg --batch --import "$WORK/release-key.asc" 2>/dev/null
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
printf '[Desktop Entry]\nType=Application\nName=corasip\nGenericName=SIP softphone\nExec=%s/corasip %%u\nIcon=corasip\nTerminal=false\nCategories=Network;Telephony;\nMimeType=x-scheme-handler/sip;x-scheme-handler/tel;\n' "$BIN" > "$APPS/corasip.desktop"
{
    echo "$APP"; echo "$BIN/corasip"; echo "$ICONS/corasip.svg"; echo "$APPS/corasip.desktop"; echo "$MANIFEST"
} > "$MANIFEST"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
echo "installed corasip $VERSION to $APP"
echo "launch with: $BIN/corasip   (add $BIN to PATH if it is not there)"
