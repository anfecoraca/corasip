#!/bin/sh
# Fetch PJSIP at the pinned commit into third_party/pjproject and verify the commit.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/third_party/PJSIP.pin"
DEST="$ROOT/third_party/pjproject"
if [ ! -d "$DEST/.git" ]; then
    git clone --quiet "$repo" "$DEST"
fi
git -C "$DEST" fetch --quiet --tags origin
git -C "$DEST" checkout --quiet "$commit"
got="$(git -C "$DEST" rev-parse HEAD)"
[ "$got" = "$commit" ] || { echo "commit mismatch: $got" >&2; exit 1; }
echo "PJSIP $tag at $commit"
