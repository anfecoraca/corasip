#!/usr/bin/env python3
"""Malformed-SIP fuzz run (M8). Sends damaged SIP messages to a listening corasip engine and checks it stays up.

Usage: sip_fuzz.py <host> <port> <seconds> [seed]
The target is an engine started as: engine_call answer <registrar> <port> 0 udp -  (null audio, no sound device).
Each message is a valid SIP message with random damage: byte flips, truncation, oversized or repeated headers,
wrong Content-Length, binary bytes, very long lines, unknown methods. A crash or a hang is a failure.
"""
import random
import socket
import sys
import time

BASES = [
    b"INVITE sip:alice@{h} SIP/2.0\r\nVia: SIP/2.0/UDP {h}:5999;branch=z9hG4bK{b}\r\nMax-Forwards: 70\r\n"
    b"From: <sip:bob@{h}>;tag=t{b}\r\nTo: <sip:alice@{h}>\r\nCall-ID: c{b}@{h}\r\nCSeq: 1 INVITE\r\n"
    b"Contact: <sip:bob@{h}:5999>\r\nContent-Type: application/sdp\r\nContent-Length: 0\r\n\r\n",
    b"OPTIONS sip:alice@{h} SIP/2.0\r\nVia: SIP/2.0/UDP {h}:5999;branch=z9hG4bK{b}\r\n"
    b"From: <sip:bob@{h}>;tag=t{b}\r\nTo: <sip:alice@{h}>\r\nCall-ID: o{b}@{h}\r\nCSeq: 2 OPTIONS\r\n"
    b"Content-Length: 0\r\n\r\n",
    b"BYE sip:alice@{h} SIP/2.0\r\nVia: SIP/2.0/UDP {h}:5999;branch=z9hG4bK{b}\r\n"
    b"From: <sip:bob@{h}>;tag=t{b}\r\nTo: <sip:alice@{h}>;tag=x\r\nCall-ID: b{b}@{h}\r\nCSeq: 3 BYE\r\n"
    b"Content-Length: 0\r\n\r\n",
    b"SIP/2.0 200 OK\r\nVia: SIP/2.0/UDP {h}:5999;branch=z9hG4bK{b}\r\n"
    b"From: <sip:bob@{h}>;tag=t{b}\r\nTo: <sip:alice@{h}>;tag=x\r\nCall-ID: r{b}@{h}\r\nCSeq: 1 INVITE\r\n"
    b"Content-Length: 0\r\n\r\n",
    b"MESSAGE sip:alice@{h} SIP/2.0\r\nVia: SIP/2.0/UDP {h}:5999;branch=z9hG4bK{b}\r\n"
    b"From: <sip:bob@{h}>;tag=t{b}\r\nTo: <sip:alice@{h}>\r\nCall-ID: m{b}@{h}\r\nCSeq: 4 MESSAGE\r\n"
    b"Content-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello",
]

def mutate(rng, msg):
    m = bytearray(msg)
    kind = rng.randrange(10)
    if kind == 0:   # flip random bytes
        for _ in range(rng.randint(1, 8)):
            m[rng.randrange(len(m))] = rng.randrange(256)
    elif kind == 1:  # truncate
        m = m[: rng.randrange(1, len(m))]
    elif kind == 2:  # oversized header
        m[len(m) // 2:len(m) // 2] = b"X-Long: " + b"A" * rng.randint(100, 8000) + b"\r\n"
    elif kind == 3:  # repeated headers
        m += b"Route: <sip:x>\r\n" * rng.randint(10, 200)
    elif kind == 4:  # wrong Content-Length
        m = bytearray(bytes(m).replace(b"Content-Length: ", b"Content-Length: 9" + bytes(str(rng.randint(0, 9999)), "ascii")))
    elif kind == 5:  # binary junk
        m[rng.randrange(len(m)):] = bytes(rng.randrange(256) for _ in range(rng.randint(1, 300)))
    elif kind == 6:  # strip line breaks
        m = bytearray(bytes(m).replace(b"\r\n", b"\n" if rng.random() < 0.5 else b""))
    elif kind == 7:  # unknown method and bad version
        m = bytearray(bytes(m).replace(b"SIP/2.0", b"SIP/9.9", 1))
        m[0:0] = b"FOOBAR " + bytes(rng.randrange(33, 126) for _ in range(rng.randint(1, 40))) + b" "
    elif kind == 8:  # deep nesting and long URIs
        m = bytearray(bytes(m).replace(b"sip:alice", b"sip:" + b"a" * rng.randint(200, 3000) + b"@x", 1))
    else:            # a SDP body with bad lines
        m += b"v=0\r\nm=audio 99999 RTP/AVP 999\r\na=rtpmap:999 " + b"Z" * rng.randint(10, 500) + b"\r\n"
    return bytes(m)

def main():
    host, port, seconds = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    rng = random.Random(seed)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sent = 0
    end = time.time() + seconds
    while time.time() < end:
        base = rng.choice(BASES).replace(b"{h}", host.encode()).replace(b"{b}", str(rng.randrange(10**6)).encode())
        payload = mutate(rng, base)
        if rng.random() < 0.05:   # some pure garbage too
            payload = bytes(rng.randrange(256) for _ in range(rng.randint(0, 2000)))
        try:
            sock.sendto(payload[:65000], (host, port))
        except OSError:
            pass
        sent += 1
        if sent % 200 == 0:
            time.sleep(0.01)
    print(f"sent {sent} damaged SIP messages in {seconds:.0f} s (seed {seed})")

if __name__ == "__main__":
    main()
