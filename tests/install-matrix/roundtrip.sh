#!/bin/sh
# Install round trip, run after check-in-container.sh in the same distribution container.
# Expects /in/tarball.tar.gz with its .sha256 and .sha256.asc, and the packaging scripts under /in/packaging.
set -eu
export HOME=/tmp/rt-home; mkdir -p "$HOME"
NAME=corasip-0.1.1-linux-x86_64.tar.gz
cp /in/tarball.tar.gz "/tmp/$NAME"
cp /in/tarball.tar.gz.sha256 "/tmp/$NAME.sha256"
cp /in/tarball.tar.gz.sha256.asc "/tmp/$NAME.sha256.asc"
# The containers run as root: the installer must refuse without --allow-root.
if sh /in/packaging/install.sh --from "/tmp/$NAME" >/dev/null 2>&1; then
    echo "FAIL: installer ran as root without --allow-root"; exit 1
fi
echo "root refused without --allow-root"
# A damaged tarball must be refused by the checksum and the signature.
cp "/tmp/$NAME" "/tmp/tampered.tar.gz"; cp "/tmp/$NAME.sha256" "/tmp/tampered.tar.gz.sha256"; cp "/tmp/$NAME.sha256.asc" "/tmp/tampered.tar.gz.sha256.asc"
printf 'x' >> /tmp/tampered.tar.gz
if sh /in/packaging/install.sh --allow-root --from /tmp/tampered.tar.gz >/dev/null 2>&1; then
    echo "FAIL: installed a damaged tarball"; exit 1
fi
echo "damaged tarball refused"
# The signed release installs with the checksum and signature checks on.
sh /in/packaging/install.sh --allow-root --from "/tmp/$NAME"
test -x "$HOME/.local/bin/corasip" || { echo "FAIL: launcher not installed"; exit 1; }
set +e
QT_QPA_PLATFORM=offscreen timeout 6 "$HOME/.local/bin/corasip" >/tmp/rt-run.log 2>&1; RC=$?
set -e
[ "$RC" -eq 124 ] || { echo "FAIL: installed app exit $RC"; tail -3 /tmp/rt-run.log; exit 1; }
echo "installed app launches (6 s)"
sh /in/packaging/uninstall.sh
echo "PASS: round trip"
