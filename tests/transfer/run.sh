#!/bin/sh
# M4 transfer test (loopback, no lab needed). Three local parties: transferor, B and C.
#  1. Blind transfer: the transferor sends B to C and leaves the call. C must be confirmed with B.
#  2. Attended transfer: the transferor holds B, calls C, then connects B to C and leaves both calls.
# Usage: run.sh <build-dir>
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
OUT="$BUILD/transfer"
mkdir -p "$OUT"
export CORASIP_NULL_AUDIO=1
REG=5175
CERTS="$BUILD/transport-certs"
mkdir -p "$CERTS"
[ -f "$CERTS/cert.pem" ] || openssl req -x509 -newkey rsa:2048 -nodes -keyout "$CERTS/key.pem" \
    -out "$CERTS/cert.pem" -days 1 -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1
python3 "$ROOT/tests/tools/sip_registrar.py" --udp $REG --tcp 5176 --tls 5177 \
    --cert "$CERTS/cert.pem" --key "$CERTS/key.pem" > "$OUT/registrar.log" 2>&1 &
REG_PID=$!
trap 'kill $REG_PID 2>/dev/null || true' EXIT
sleep 1
fail=0

run_case() {
    kind="$1"
    "$BUILD/engine_call" answer 127.0.0.1:$REG 5193 0 udp - > "$OUT/$kind-b.log" 2>&1 &
    bpid=$!
    "$BUILD/engine_call" answer 127.0.0.1:$REG 5194 0 udp - > "$OUT/$kind-c.log" 2>&1 &
    cpid=$!
    sleep 3
    set +e
    CORASIP_HOLD_SECONDS=2 "$BUILD/transfer_call" 127.0.0.1:$REG "$kind" \
        "sip:answer@127.0.0.1:5193;transport=udp" "sip:answer@127.0.0.1:5194;transport=udp" > "$OUT/$kind-transferor.log" 2>&1
    trc=$?
    wait $bpid; brc=$?
    wait $cpid; crc=$?
    set -e
    if [ $trc -eq 0 ] && grep -q "PASS transfer $kind" "$OUT/$kind-transferor.log" && grep -q "confirmed" "$OUT/$kind-c.log"; then
        echo "ok: $kind transfer, C confirmed with B"
    else
        echo "FAIL: $kind transfer (transferor=$trc b=$brc c=$crc), see $OUT/$kind-transferor.log"; fail=1
    fi
}

run_case blind
run_case attended
[ "$fail" -eq 0 ] && echo "PASS: transfer (blind and attended)"
exit "$fail"
