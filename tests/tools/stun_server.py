#!/usr/bin/env python3
"""Minimal STUN server (RFC 5389 Binding only) for the M5C NAT test path.

It answers Binding Requests with the sender's transport address in XOR-MAPPED-ADDRESS. Nothing else.
Usage: stun_server.py <bind-ip> [port]   (default port 3478)
"""
import socket
import struct
import sys

MAGIC = 0x2112A442
BINDING_REQUEST = 0x0001
BINDING_SUCCESS = 0x0101


def build_response(txid: bytes, ip: str, port: int) -> bytes:
    xport = port ^ (MAGIC >> 16)
    xaddr = struct.unpack("!I", socket.inet_aton(ip))[0] ^ MAGIC
    attr = struct.pack("!HHBBH", 0x0020, 8, 0, 0x01, xport) + struct.pack("!I", xaddr)
    header = struct.pack("!HHI", BINDING_SUCCESS, len(attr), MAGIC) + txid
    return header + attr


def main() -> None:
    bind_ip = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 3478
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind_ip, port))
    print(f"stun: listening on {bind_ip}:{port}", flush=True)
    while True:
        data, (ip, sport) = sock.recvfrom(2048)
        if len(data) < 20:
            continue
        msg_type, _length, magic = struct.unpack("!HHI", data[:8])
        if msg_type != BINDING_REQUEST or magic != MAGIC:
            continue
        sock.sendto(build_response(data[8:20], ip, sport), (ip, sport))
        print(f"stun: mapped {ip}:{sport}", flush=True)


if __name__ == "__main__":
    main()
