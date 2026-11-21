#!/usr/bin/env python3
"""Check a loopback call capture for SRTP on or off (M1 step 7). Dev-only test tool.

Usage: rtp_check.py <capture.pcap> <on|off>
  off: signalling is plain UDP, SDP offers plain RTP (RTP/AVP), RTP payloads are plain PCMU silence.
  on:  signalling is TLS (no plaintext SIP in the capture), RTP payloads are encrypted (not silence).
       The SRTP offer itself is inside TLS, so the capture cannot show RTP/SAVP. The call only
       completes because SRTP is mandatory, and the payload check confirms the media is encrypted.
Exit 0 when the capture matches the mode, 1 otherwise. Reads classic pcap files with Ethernet
link type, which is what tcpdump -i lo writes. Standard library only.
"""
import struct
import sys


def udp_payloads(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, _, _, _, _, _, linktype = struct.unpack("<IHHiIII", data[:24])
    if magic != 0xA1B2C3D4:
        raise SystemExit("not a little-endian pcap file")
    if linktype != 1:
        raise SystemExit(f"unsupported link type {linktype}, expected Ethernet (1)")
    pos = 24
    while pos + 16 <= len(data):
        _, _, caplen, _ = struct.unpack("<IIII", data[pos:pos + 16])
        pkt = data[pos + 16:pos + 16 + caplen]
        pos += 16 + caplen
        if len(pkt) < 14 + 20 + 8 or struct.unpack("!H", pkt[12:14])[0] != 0x0800:
            continue
        ihl = (pkt[14] & 0x0F) * 4
        if pkt[14 + 9] != 17:  # not UDP
            continue
        udp = 14 + ihl
        payload = pkt[udp + 8:]
        yield payload


def is_rtp_audio(payload):
    # RTP version 2, 12-byte header, payload type 0 (PCMU). 20 ms frames are 160 bytes of audio.
    return len(payload) >= 12 + 160 and payload[0] >> 6 == 2 and (payload[1] & 0x7F) == 0


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ("on", "off"):
        print(__doc__)
        return 1
    path, mode = sys.argv[1], sys.argv[2]
    sdp_avp = sdp_savp = plain_sip = False
    rtp_total = rtp_silence = rtp_other = 0
    for payload in udp_payloads(path):
        if payload.startswith(b"SIP/2.0") or payload.startswith(b"INVITE") or payload.startswith(b"ACK"):
            plain_sip = True
            if b"RTP/SAVP" in payload:
                sdp_savp = True
            if b"RTP/AVP" in payload:
                sdp_avp = True
            continue
        if is_rtp_audio(payload):
            rtp_total += 1
            body = payload[12:]
            if all(b == 0xFF for b in body):
                rtp_silence += 1
            else:
                rtp_other += 1

    print(f"mode={mode} plain_sip={plain_sip} sdp_RTP_AVP={sdp_avp} sdp_RTP_SAVP={sdp_savp} "
          f"rtp_frames={rtp_total} plain_silence={rtp_silence} non_silence={rtp_other}")
    if rtp_total < 50:
        print("FAIL: too few RTP audio frames captured")
        return 1
    if mode == "off":
        ok = plain_sip and sdp_avp and not sdp_savp and rtp_silence == rtp_total
    else:
        ok = not plain_sip and rtp_other > rtp_total * 0.9
    print("PASS" if ok else "FAIL", f"({mode})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
