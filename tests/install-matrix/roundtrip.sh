#!/bin/sh
# Install round trip (M6C, M6D), run after check-in-container.sh in the same distribution container.
# Expects /in/tarball.tar.gz (+ .sha256) and the packaging scripts under /in/packaging.
set -eu
export HOME=/tmp/rt-home; mkdir -p "$HOME"
cp /in/tarball.tar.gz /tmp/corasip-0.1.0-linux-x86_64.tar.gz
cp /in/tarball.tar.gz.sha256 /tmp/corasip-0.1.0-linux-x86_64.tar.gz.sha256
# The containers run as root: the installer must refuse without --allow-root, then install with it.
if sh /in/packaging/install.sh --from /tmp/corasip-0.1.0-linux-x86_64.tar.gz >/dev/null 2>&1; then
    echo "FAIL: installer ran as root without --allow-root"; exit 1
fi
echo "root refused without --allow-root"
# No release key is pinned yet: without the test flag the installer must refuse.
if sh /in/packaging/install.sh --allow-root --from /tmp/corasip-0.1.0-linux-x86_64.tar.gz >/dev/null 2>&1; then
    echo "FAIL: installed without a pinned release key"; exit 1
fi
echo "refused without a pinned release key"
sh /in/packaging/install.sh --allow-root --no-signature-check --from /tmp/corasip-0.1.0-linux-x86_64.tar.gz
test -x "$HOME/.local/bin/corasip" || { echo "FAIL: launcher not installed"; exit 1; }
set +e
QT_QPA_PLATFORM=offscreen timeout 6 "$HOME/.local/bin/corasip" >/tmp/rt-run.log 2>&1; RC=$?
set -e
[ "$RC" -eq 124 ] || { echo "FAIL: installed app exit $RC"; tail -3 /tmp/rt-run.log; exit 1; }
echo "installed app launches (6 s)"
sh /in/packaging/uninstall.sh
# Only what the installer recorded must be gone. The app's own data (call history, contacts) is kept on purpose.
if [ -e "$HOME/.local/bin/corasip" ] || [ -e "$HOME/.local/share/corasip/install-manifest.txt" ] \
   || [ -d "$HOME/.local/share/corasip/corasip-0.1.0-linux-x86_64" ] \
   || [ -e "$HOME/.local/share/applications/corasip.desktop" ]; then
    echo "FAIL: installer files left after uninstall"; find "$HOME/.local" -type f | head; exit 1
fi
[ -f "$HOME/.local/share/corasip/history.db" ] && echo "kept user data: ~/.local/share/corasip/history.db (not removed by design)"
echo "PASS: install, launch and uninstall (installer files removed)"
