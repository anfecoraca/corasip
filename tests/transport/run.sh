#!/bin/sh
# M1 step 6 transport test. Usage: run.sh <build-dir>
# Starts the dev registrar on loopback, then checks:
#   UDP registers, TCP registers, TLS registers when the CA is trusted,
#   TLS does NOT register when the CA is not trusted (verification on, SPEC §5.3).
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"
CERTS="$BUILD/transport-certs"
mkdir -p "$CERTS"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$CERTS/key.pem" -out "$CERTS/cert.pem" \
    -days 1 -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1

UDP=5170 TCP=5171 TLS=5172
python3 "$ROOT/tests/tools/sip_registrar.py" --udp $UDP --tcp $TCP --tls $TLS \
    --cert "$CERTS/cert.pem" --key "$CERTS/key.pem" > "$BUILD/registrar.log" 2>&1 &
REG_PID=$!
trap 'kill $REG_PID 2>/dev/null || true' EXIT
sleep 1

fail=0
run_expect() {  # run_expect <expected-rc> <args...>
    want="$1"; shift
    set +e
    "$BUILD/engine_transport" "$@"
    rc=$?
    set -e
    if [ "$rc" -ne "$want" ]; then
        echo "FAIL: expected exit $want, got $rc: $*"
        fail=1
    fi
}
run_expect 0 udp "127.0.0.1:$UDP" -
run_expect 0 tcp "127.0.0.1:$TCP" -
run_expect 0 tls "127.0.0.1:$TLS" "$CERTS/cert.pem"
run_expect 2 tls "127.0.0.1:$TLS" -
[ "$fail" -eq 0 ] && echo "PASS: transport tests (udp, tcp, tls trusted, tls untrusted rejected)"
exit "$fail"
