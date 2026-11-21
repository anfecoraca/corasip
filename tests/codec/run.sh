#!/bin/sh
# M1 step 9 codec test (loopback between two local processes; no lab needed).
#  1. Opus only on both sides: a 440 Hz tone from the caller must arrive at the answerer as Opus
#     (RTP payload type 96), decoded and checked on the wire.
#  2. No common codec (caller G.711 u-law only, answerer Opus only): the call must not connect.
# Usage: run.sh <build-dir>
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
OUT="$BUILD/codec"
mkdir -p "$OUT"
export CORASIP_NULL_AUDIO=1

python3 - "$OUT/tone440-8k.wav" <<'PY'
import math, struct, sys, wave
rate, seconds = 8000, 12
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(b"".join(struct.pack("<h", int(0.3 * 32767 * math.sin(2 * math.pi * 440 * n / rate)))
                           for n in range(rate * seconds)))
PY

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

# Case 1: Opus on both sides, tone must be received as Opus.
pcap="$OUT/opus.pcap"
tcpdump -i lo -U -w "$pcap" udp >/dev/null 2>&1 &
cap=$!
sleep 1
CORASIP_CODECS="opus/48000/2" "$BUILD/engine_call" answer 127.0.0.1:$REG 5181 0 udp - > "$OUT/opus-answer.log" 2>&1 &
ans=$!
sleep 3
set +e
CORASIP_CODECS="opus/48000/2" CORASIP_TONE_TX="$OUT/tone440-8k.wav" CORASIP_HOLD_SECONDS=8 \
    "$BUILD/engine_call" call 127.0.0.1:$REG "sip:answer@127.0.0.1:5181;transport=udp" 0 udp - > "$OUT/opus-caller.log" 2>&1
crc=$?
wait $ans; arc=$?
set -e
sleep 1; kill -INT $cap 2>/dev/null; wait $cap 2>/dev/null || true
if [ $crc -ne 0 ] || [ $arc -ne 0 ]; then
    echo "FAIL: Opus call (caller=$crc answer=$arc), see $OUT/opus-*.log"; fail=1
else
    set +e
    python3 "$ROOT/tests/tools/rtp_tone_check.py" "$pcap" 440 --codec opus --min-fraction 0.2 > "$OUT/opus-check.log" 2>&1
    rc=$?
    set -e
    if [ $rc -eq 0 ]; then echo "ok: Opus call, tone received as Opus"; else echo "FAIL: Opus tone check (see $OUT/opus-check.log)"; fail=1; fi
fi

# Case 2: no common codec. The call must not connect.
CORASIP_CODECS="opus/48000/2" "$BUILD/engine_call" answer 127.0.0.1:$REG 5182 0 udp - > "$OUT/nocommon-answer.log" 2>&1 &
ans=$!
sleep 3
set +e
CORASIP_CODECS="PCMU/8000/1" "$BUILD/engine_call" call 127.0.0.1:$REG "sip:answer@127.0.0.1:5182;transport=udp" 0 udp - > "$OUT/nocommon-caller.log" 2>&1
crc=$?
kill $ans 2>/dev/null; wait $ans 2>/dev/null
set -e
if [ $crc -ne 0 ] && grep -q "not confirmed\|ERROR" "$OUT/nocommon-caller.log" && ! grep -q "^PASS" "$OUT/nocommon-caller.log"; then
    echo "ok: no common codec, call refused"
else
    echo "FAIL: call connected with no common codec (see $OUT/nocommon-caller.log)"; fail=1
fi

[ "$fail" -eq 0 ] && echo "PASS: codec tests (Opus media on the wire, no-common-codec refused)"
exit "$fail"
