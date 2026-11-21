#!/bin/sh
# M1 step 8 automated audio check, through the lab PBX.
#  1. Echo test (10): a 440 Hz tone goes out, the PBX echoes it back. The tone must be in the RTP we receive.
#  2. Negative control (22, no echo): the same tone goes out. The tone must NOT be in the RTP we receive.
# Runs with a null sound device, so it checks the media path, not the sound hardware. The hardware check
# is manual. Received audio is read from a capture of the RTP packets.
# Usage: run.sh <build-dir>
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
OUT="$BUILD/audio"
mkdir -p "$OUT"
. "$ROOT/tests/.local/lab-extensions.env"

python3 - "$OUT/tone440.wav" <<'PY'
import math, struct, sys, wave
rate, seconds = 8000, 15
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(b"".join(struct.pack("<h", int(0.3 * 32767 * math.sin(2 * math.pi * 440 * n / rate)))
                           for n in range(rate * seconds)))
PY

run_case() {  # run_case <name> <extension> <expect: pass|fail>
    name="$1"; ext="$2"; expect="$3"
    pcap="$OUT/$name.pcap"
    tcpdump -i any -U -w "$pcap" "udp and host $LAB_SIP_HOST" >/dev/null 2>&1 &
    cap=$!
    sleep 1
    set +e
    CORASIP_USER="$LAB_EXT_A" CORASIP_PASSWORD="$LAB_EXT_A_PASSWORD" CORASIP_DOMAIN="$LAB_SIP_HOST" \
    CORASIP_NULL_AUDIO=1 CORASIP_TONE_TX="$OUT/tone440.wav" CORASIP_HOLD_SECONDS=15 \
        "$BUILD/engine_call" call "$LAB_SIP_HOST:$LAB_SIP_PORT" "sip:$ext@$LAB_SIP_HOST;transport=udp" 0 udp - \
        > "$OUT/$name.log" 2>&1
    crc=$?
    sleep 1
    kill -INT $cap 2>/dev/null; wait $cap 2>/dev/null
    set -e
    result=0
    python3 "$ROOT/tests/tools/rtp_tone_check.py" "$pcap" 440 --far-end "$LAB_SIP_HOST" --min-fraction 0.2 >/dev/null || result=$?
    if [ "$expect" = pass ]; then
        if [ "$crc" -eq 0 ] && [ "$result" -eq 0 ]; then echo "ok: $name (tone received)"; return 0; fi
        echo "FAIL: $name (call rc=$crc, tone check rc=$result), see $OUT/$name.log"; return 1
    fi
    if [ "$result" -eq 1 ]; then echo "ok: $name (no tone received, as expected)"; return 0; fi
    if [ "$result" -eq 0 ]; then echo "FAIL: $name received a tone it should not have"; return 1; fi
    echo "FAIL: $name capture could not be checked (rc=$result)"; return 1
}

fail=0
run_case echo-10 10 pass || fail=1
run_case control-22 22 fail || fail=1
[ "$fail" -eq 0 ] && echo "PASS: audio path through the PBX (tone echoed back, negative control clean)"
exit "$fail"
