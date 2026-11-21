#!/usr/bin/env python3
"""Minimal SIP registrar for transport tests (M1 step 6).

Dev-only test tool, not part of the product. Answers every REGISTER with 200 OK over UDP,
TCP and TLS. Python standard library only.
"""
import argparse
import socket
import ssl
import sys
import threading


def parse_request(head: bytes):
    lines = head.decode("utf-8", "replace").split("\r\n")
    method = lines[0].split(" ", 1)[0]
    headers = {}
    vias = []
    for line in lines[1:]:
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        name = name.strip().lower()
        value = value.strip()
        if name == "via":
            vias.append(value)
        else:
            headers.setdefault(name, value)
    return method, vias, headers


def build_200(vias, headers):
    to = headers.get("to", "")
    if ";tag=" not in to:
        to += ";tag=corasip-test"
    out = ["SIP/2.0 200 OK"]
    out += ["Via: " + v for v in vias]
    out.append("From: " + headers.get("from", ""))
    out.append("To: " + to)
    out.append("Call-ID: " + headers.get("call-id", ""))
    out.append("CSeq: " + headers.get("cseq", ""))
    if "contact" in headers:
        # Echo the request's Contact unchanged so the UA can match it to its own registration.
        contact = headers["contact"]
        if "expires=" not in contact:
            contact += ";expires=60"
        out.append("Contact: " + contact)
    out.append("Content-Length: 0")
    return ("\r\n".join(out) + "\r\n\r\n").encode("utf-8")


def log(transport, method):
    print(f"{transport} {method} -> 200", flush=True)


def serve_udp(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", port))
    while True:
        data, addr = sock.recvfrom(65535)
        head = data.split(b"\r\n\r\n", 1)[0]
        method, vias, headers = parse_request(head)
        if method == "REGISTER":
            sock.sendto(build_200(vias, headers), addr)
            log("udp", method)


def handle_stream(conn, transport):
    buf = b""
    try:
        while True:
            while b"\r\n\r\n" not in buf:
                chunk = conn.recv(4096)
                if not chunk:
                    return
                buf += chunk
            head, rest = buf.split(b"\r\n\r\n", 1)
            method, vias, headers = parse_request(head)
            length = int(headers.get("content-length", "0") or 0)
            while len(rest) < length:
                chunk = conn.recv(4096)
                if not chunk:
                    return
                rest += chunk
            buf = rest[length:]
            if method == "REGISTER":
                conn.sendall(build_200(vias, headers))
                log(transport, method)
    except (OSError, ssl.SSLError) as exc:
        print(f"{transport} connection error: {exc}", flush=True)
    finally:
        conn.close()


def serve_stream(port, transport, tls_ctx=None):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(16)
    while True:
        conn, _ = listener.accept()
        if tls_ctx is not None:
            try:
                conn = tls_ctx.wrap_socket(conn, server_side=True)
            except (OSError, ssl.SSLError) as exc:
                print(f"{transport} handshake failed: {exc}", flush=True)
                conn.close()
                continue
        threading.Thread(target=handle_stream, args=(conn, transport), daemon=True).start()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--udp", type=int, required=True)
    ap.add_argument("--tcp", type=int, required=True)
    ap.add_argument("--tls", type=int, required=True)
    ap.add_argument("--cert", required=True)
    ap.add_argument("--key", required=True)
    args = ap.parse_args()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(args.cert, args.key)

    threads = [
        threading.Thread(target=serve_udp, args=(args.udp,), daemon=True),
        threading.Thread(target=serve_stream, args=(args.tcp, "tcp"), daemon=True),
        threading.Thread(target=serve_stream, args=(args.tls, "tls", ctx), daemon=True),
    ]
    for t in threads:
        t.start()
    print("registrar ready", flush=True)
    for t in threads:
        t.join()


if __name__ == "__main__":
    sys.exit(main())
