#!/bin/sh
# M1 step 7: loopback call with SRTP off, then on, with a capture of each.
# Usage: run.sh <build-dir>
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
CERTS="$BUILD/transport-certs"
mkdir -p "$CERTS"
[ -f "$CERTS/cert.pem" ] || openssl req -x509 -newkey rsa:2048 -nodes -keyout "$CERTS/key.pem" \
    -out "$CERTS/cert.pem" -days 1 -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1

# Automated runs use a null sound device: they test SIP and media, not the sound hardware.
export CORASIP_NULL_AUDIO=1

REG=5170
python3 "$ROOT/tests/tools/sip_registrar.py" --udp $REG --tcp 5171 --tls 5172 \
    --cert "$CERTS/cert.pem" --key "$CERTS/key.pem" > "$BUILD/call-registrar.log" 2>&1 &
REG_PID=$!
trap 'kill $REG_PID 2>/dev/null || true' EXIT
sleep 1

fail=0
for mode in off on; do
    srtp=0; tp=udp
    if [ "$mode" = on ]; then srtp=1; tp=tls; fi   # SRTP needs TLS signalling (engine default)
    pcap="$BUILD/call-$mode.pcap"
    tcpdump -i lo -w "$pcap" -U udp >/dev/null 2>&1 &
    CAP_PID=$!
    sleep 1

    "$BUILD/engine_call" answer 127.0.0.1:$REG 5180 $srtp $tp "$CERTS/cert.pem" "$CERTS/key.pem" \
        > "$BUILD/call-answer-$mode.log" 2>&1 &
    ANS_PID=$!
    sleep 3

    set +e
    "$BUILD/engine_call" call 127.0.0.1:$REG "sip:answer@127.0.0.1:5180;transport=$tp" $srtp $tp \
        "$CERTS/cert.pem" > "$BUILD/call-caller-$mode.log" 2>&1
    caller_rc=$?
    wait $ANS_PID
    answer_rc=$?
    set -e

    kill -INT $CAP_PID 2>/dev/null || true
    wait $CAP_PID 2>/dev/null || true

    if [ $caller_rc -ne 0 ] || [ $answer_rc -ne 0 ]; then
        echo "FAIL: call srtp=$srtp (caller=$caller_rc answer=$answer_rc), see $BUILD/call-*-$mode.log"
        fail=1
        continue
    fi
    python3 "$ROOT/tests/tools/rtp_check.py" "$pcap" "$mode" || fail=1
done
# SRTP on over UDP must be refused: SRTP keys would travel in plain SIP (engine default).
set +e
"$BUILD/engine_call" call 127.0.0.1:$REG "sip:answer@127.0.0.1:5180;transport=udp" 1 udp "$CERTS/cert.pem" \
    > "$BUILD/call-caller-refused.log" 2>&1
refused_rc=$?
set -e
if [ "$refused_rc" -ne 1 ] || ! grep -q "PJSIP_ESESSIONINSECURE" "$BUILD/call-caller-refused.log"; then
    echo "FAIL: SRTP over UDP was not refused (rc=$refused_rc)"
    fail=1
else
    echo "ok: SRTP over UDP refused (PJSIP_ESESSIONINSECURE)"
fi
[ "$fail" -eq 0 ] && echo "PASS: loopback call with SRTP off and on, capture checks passed, SRTP over UDP refused"
exit "$fail"
