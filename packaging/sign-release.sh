#!/bin/sh
# Signs a release checksum file with the release key (M6, SPEC D34). Run by the owner on the owner's machine,
# never on a build machine. Usage: sign-release.sh <file.sha256> <gpg-key-id-or-name>
# Writes <file.sha256>.asc (ASCII-armoured detached signature). The passphrase is typed by the owner.
set -eu
FILE="$1"; KEY="$2"
gpg --batch --yes --armor --detach-sign --local-user "$KEY" --output "$FILE.asc" "$FILE"
echo "signed: $FILE.asc"
