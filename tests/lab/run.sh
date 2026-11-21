#!/bin/sh
# M1 lab tests against the Asterisk PBX (test extensions 1090 and 1091, UDP, G.711).
# Credentials come from tests/.local/lab-extensions.env (untracked). Never print them.
# Usage: run.sh <build-dir>
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
. "$ROOT/tests/.local/lab-extensions.env"
# Automated runs use a null sound device: they test SIP and media, not the sound hardware.
export CORASIP_NULL_AUDIO=1
HOSTPORT="$LAB_SIP_HOST:$LAB_SIP_PORT"
fail=0

# 1. Registration of 1090 with the PBX.
set +e
CORASIP_USER="$LAB_EXT_A" CORASIP_PASSWORD="$LAB_EXT_A_PASSWORD" CORASIP_DOMAIN="$LAB_SIP_HOST" \
    "$BUILD/engine_transport" udp "$HOSTPORT" - >/dev/null 2>&1
rc=$?; set -e
[ "$rc" -eq 0 ] && echo "ok: 1090 registers with the PBX" || { echo "FAIL: registration (rc=$rc)"; fail=1; }

# 2. Echo test (extension 10): media goes out and comes back.
set +e
CORASIP_USER="$LAB_EXT_A" CORASIP_PASSWORD="$LAB_EXT_A_PASSWORD" CORASIP_DOMAIN="$LAB_SIP_HOST" \
    "$BUILD/engine_call" call "$HOSTPORT" "sip:10@$LAB_SIP_HOST;transport=udp" 0 udp - >/dev/null 2>&1
rc=$?; set -e
[ "$rc" -eq 0 ] && echo "ok: call to echo test (10) completes" || { echo "FAIL: echo call (rc=$rc)"; fail=1; }

# 3. Extension to extension: 1091 answers, 1090 calls.
set +e
CORASIP_USER="$LAB_EXT_B" CORASIP_PASSWORD="$LAB_EXT_B_PASSWORD" CORASIP_DOMAIN="$LAB_SIP_HOST" \
    "$BUILD/engine_call" answer "$HOSTPORT" 5180 0 udp - >"$BUILD/lab-answer.log" 2>&1 &
ANS=$!
sleep 4
CORASIP_USER="$LAB_EXT_A" CORASIP_PASSWORD="$LAB_EXT_A_PASSWORD" CORASIP_DOMAIN="$LAB_SIP_HOST" \
    "$BUILD/engine_call" call "$HOSTPORT" "sip:$LAB_EXT_B@$LAB_SIP_HOST;transport=udp" 0 udp - >/dev/null 2>&1
crc=$?
wait $ANS; arc=$?; set -e
[ "$crc" -eq 0 ] && [ "$arc" -eq 0 ] && echo "ok: 1090 calls 1091 through the PBX" || { echo "FAIL: extension call (caller=$crc answer=$arc)"; fail=1; }

[ "$fail" -eq 0 ] && echo "PASS: lab tests against the PBX (UDP, G.711)"
exit "$fail"
