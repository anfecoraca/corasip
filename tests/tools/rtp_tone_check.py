#!/usr/bin/env python3
"""Check whether a capture contains a test tone in received RTP audio (M1 steps 8 and 9). Dev-only tool.

Usage: rtp_tone_check.py <capture.pcap> <tone-hz> [--far-end IP] [--codec pcmu|opus] [--min-fraction F]

RTP packets are grouped into streams (source IP, source port, destination port). Each stream is decoded
to 8 kHz mono: G.711 u-law directly, Opus through the system libopus (48 kHz, then decimated by 6).
Streams are split into 100 ms windows. A window counts as tone when its RMS is above a floor and the tone
frequency is at least 10 times stronger than every other frequency in the voice band (Hann window).
A stream passes when the tone is in at least F of its windows (default 0.2).

Exit codes: 0 a stream has the tone, 1 no stream has it, 2 the capture or decoder could not be read.
Standard library only (libopus is loaded with ctypes when --codec opus is used).
"""
import argparse
import ctypes
import math
import struct
import sys

RATE = 8000
BAND_HZ = [300 + 20 * i for i in range(155)]
PAYLOAD_TYPE = {"pcmu": 0, "opus": 96}


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(2)


def ulaw_decode(byte):
    byte = ~byte & 0xFF
    sign = byte & 0x80
    exponent = (byte >> 4) & 0x07
    mantissa = byte & 0x0F
    sample = (((mantissa << 3) + 0x84) << exponent) - 0x84
    return -sample if sign else sample


ULAW = [ulaw_decode(b) / 32768.0 for b in range(256)]


def rtp_packets(path, far_ip, payload_type):
    """Yield (stream_key, sequence, payload) for RTP packets of the wanted payload type."""
    with open(path, "rb") as f:
        data = f.read()
    if struct.unpack("<I", data[:4])[0] != 0xA1B2C3D4:
        fail("not a little-endian pcap file")
    linktype = struct.unpack("<I", data[20:24])[0]
    if linktype not in (1, 113, 276):
        fail(f"unsupported link type {linktype}")
    far = bytes(int(x) for x in far_ip.split(".")) if far_ip else None
    pos = 24
    while pos + 16 <= len(data):
        _, _, caplen, _ = struct.unpack("<IIII", data[pos:pos + 16])
        pkt = data[pos + 16:pos + 16 + caplen]
        pos += 16 + caplen
        if linktype == 1:
            if len(pkt) < 14 or struct.unpack("!H", pkt[12:14])[0] != 0x0800:
                continue
            ip = pkt[14:]
        elif linktype == 113:
            if len(pkt) < 16 or struct.unpack("!H", pkt[14:16])[0] != 0x0800:
                continue
            ip = pkt[16:]
        else:
            if len(pkt) < 20 or struct.unpack("!H", pkt[0:2])[0] != 0x0800:
                continue
            ip = pkt[20:]
        if len(ip) < 20 or ip[9] != 17:
            continue
        if far is not None and ip[12:16] != far:
            continue
        ihl = (ip[0] & 0x0F) * 4
        udp = ip[ihl:]
        if len(udp) < 8:
            continue
        sport, dport = struct.unpack("!HH", udp[0:4])
        rtp = udp[8:]
        if len(rtp) < 12 or rtp[0] >> 6 != 2 or (rtp[1] & 0x7F) != payload_type:
            continue
        seq = struct.unpack("!H", rtp[2:4])[0]
        yield (ip[12:16], sport, dport), seq, rtp[12:]


class Opus:
    """Thin ctypes wrapper over the system libopus decoder."""

    def __init__(self):
        try:
            self.lib = ctypes.CDLL("libopus.so.0")
        except OSError as exc:
            fail(f"libopus not available: {exc}")
        self.lib.opus_decoder_create.restype = ctypes.c_void_p
        self.lib.opus_decoder_create.argtypes = [ctypes.c_int32, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
        self.lib.opus_decode.restype = ctypes.c_int
        self.lib.opus_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32,
                                         ctypes.POINTER(ctypes.c_int16), ctypes.c_int, ctypes.c_int]

    def decoder(self):
        err = ctypes.c_int(0)
        dec = self.lib.opus_decoder_create(48000, 2, ctypes.byref(err))
        if not dec or err.value != 0:
            fail(f"opus decoder create failed ({err.value})")
        return dec

    def decode_mono_48k(self, dec, packet):
        pcm = (ctypes.c_int16 * (5760 * 2))()
        n = self.lib.opus_decode(dec, packet, len(packet), pcm, 5760, 0)
        if n < 0:
            return []
        return [(pcm[2 * i] + pcm[2 * i + 1]) / 2.0 / 32768.0 for i in range(n)]


def stream_samples(packets, codec):
    samples = []
    if codec == "pcmu":
        for _, payload in packets:
            samples.extend(ULAW[b] for b in payload)
        return samples
    opus = Opus()
    dec = opus.decoder()
    for _, payload in packets:
        mono = opus.decode_mono_48k(dec, bytes(payload))
        # 48 kHz to 8 kHz: average each group of six samples (a simple low-pass before decimation)
        for i in range(0, len(mono) - 5, 6):
            samples.append(sum(mono[i:i + 6]) / 6.0)
    return samples


def hann(samples):
    n = len(samples)
    return [x * (0.5 - 0.5 * math.cos(2.0 * math.pi * i / (n - 1))) for i, x in enumerate(samples)]


def goertzel_power(samples, rate, freq):
    k = 2.0 * math.cos(2.0 * math.pi * freq / rate)
    s1 = s2 = 0.0
    for x in samples:
        s0 = x + k * s1 - s2
        s2, s1 = s1, s0
    return s1 * s1 + s2 * s2 - k * s1 * s2


def tone_fraction(samples, tone):
    win = RATE // 10
    tone_windows = total = 0
    for start in range(0, len(samples) - win + 1, win):
        chunk = samples[start:start + win]
        total += 1
        rms = math.sqrt(sum(x * x for x in chunk) / len(chunk))
        if rms < 0.005:
            continue
        chunk = hann(chunk)
        p_tone = goertzel_power(chunk, RATE, tone)
        strongest_other = max(goertzel_power(chunk, RATE, f) for f in BAND_HZ if abs(f - tone) > 30)
        if p_tone > 10.0 * (strongest_other + 1e-12):
            tone_windows += 1
    return tone_windows, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcap")
    ap.add_argument("tone", type=float)
    ap.add_argument("--far-end", default=None)
    ap.add_argument("--codec", choices=["pcmu", "opus"], default="pcmu")
    ap.add_argument("--min-fraction", type=float, default=0.2)
    args = ap.parse_args()

    streams = {}
    for key, seq, payload in rtp_packets(args.pcap, args.far_end, PAYLOAD_TYPE[args.codec]):
        streams.setdefault(key, []).append((seq, payload))
    if not streams:
        fail("no RTP packets of the wanted type in the capture")

    any_tone = False
    for key, packets in sorted(streams.items(), key=lambda kv: -len(kv[1])):
        packets.sort(key=lambda p: p[0])
        samples = stream_samples(packets, args.codec)
        hits, total = tone_fraction(samples, args.tone)
        fraction = hits / total if total else 0.0
        src = ".".join(str(b) for b in key[0])
        print(f"stream {src}:{key[1]} -> :{key[2]} codec={args.codec} packets={len(packets)} "
              f"seconds={len(samples) / RATE:.1f} tone={args.tone:.0f}Hz windows={total} "
              f"tone_windows={hits} fraction={fraction:.2f}")
        if total and fraction >= args.min_fraction:
            any_tone = True
    print("PASS (tone received)" if any_tone else "FAIL (no tone received)")
    return 0 if any_tone else 1


if __name__ == "__main__":
    sys.exit(main())
