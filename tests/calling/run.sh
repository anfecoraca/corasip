#!/bin/sh
# M3 calling-core test (loopback between two local processes, no lab needed).
#  1. A live call: hold, resume, mute, unmute and DTMF each succeed and the call stays up.
#  2. Reject: the answering side declines (603); the caller must see the call end before it is confirmed.
# Usage: run.sh <build-dir>
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
OUT="$BUILD/calling"
mkdir -p "$OUT"
export CORASIP_NULL_AUDIO=1
REG=5170
CERTS="$BUILD/transport-certs"
mkdir -p "$CERTS"
[ -f "$CERTS/cert.pem" ] || openssl req -x509 -newkey rsa:2048 -nodes -keyout "$CERTS/key.pem" \
    -out "$CERTS/cert.pem" -days 1 -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1
python3 "$ROOT/tests/tools/sip_registrar.py" --udp $REG --tcp 5171 --tls 5172 \
    --cert "$CERTS/cert.pem" --key "$CERTS/key.pem" > "$OUT/registrar.log" 2>&1 &
REG_PID=$!
trap 'kill $REG_PID 2>/dev/null || true' EXIT
sleep 1
fail=0

# Case 1: features on a live call.
CORASIP_FEATURES=1 CORASIP_HOLD_SECONDS=2 "$BUILD/engine_call" answer 127.0.0.1:$REG 5191 0 udp - \
    > "$OUT/feat-answer.log" 2>&1 &
ans=$!
sleep 3
set +e
CORASIP_FEATURES=1 CORASIP_HOLD_SECONDS=2 "$BUILD/engine_call" call 127.0.0.1:$REG "sip:answer@127.0.0.1:5191;transport=udp" 0 udp - \
    > "$OUT/feat-caller.log" 2>&1
crc=$?
wait $ans; arc=$?
set -e
if [ $crc -eq 0 ] && [ $arc -eq 0 ] && grep -q "^hold: ok" "$OUT/feat-caller.log" && grep -q "^dtmf: ok" "$OUT/feat-caller.log"; then
    echo "ok: hold, resume, mute, unmute and DTMF on a live call"
else
    echo "FAIL: live-call features (caller=$crc answer=$arc), see $OUT/feat-caller.log"; fail=1
fi

# Case 2: the answering side rejects. The caller must not reach confirmed.
CORASIP_REJECT=1 "$BUILD/engine_call" answer 127.0.0.1:$REG 5192 0 udp - > "$OUT/rej-answer.log" 2>&1 &
ans=$!
sleep 3
set +e
"$BUILD/engine_call" call 127.0.0.1:$REG "sip:answer@127.0.0.1:5192;transport=udp" 0 udp - > "$OUT/rej-caller.log" 2>&1
crc=$?
wait $ans; arc=$?
set -e
if [ $crc -ne 0 ] && [ $arc -eq 0 ] && grep -q "ended before it was confirmed" "$OUT/rej-caller.log" && grep -q "rejected incoming call" "$OUT/rej-answer.log"; then
    echo "ok: rejected call ends before confirmation"
else
    echo "FAIL: reject (caller=$crc answer=$arc), see $OUT/rej-caller.log"; fail=1
fi

[ "$fail" -eq 0 ] && echo "PASS: calling core (hold, mute, DTMF, reject)"
exit "$fail"
