#!/usr/bin/env python3
"""Compare C WAV output against the OpenKeeper Java raw PCM dump.

Usage: compare_audio.py <c.wav> <java.raw> [window_samples]
Prints full-stream and first-window exact-match/MAD statistics.
"""
import struct
import sys
import wave


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    window = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
    with wave.open(sys.argv[1], "rb") as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 2
        c_bytes = w.readframes(w.getnframes())
    with open(sys.argv[2], "rb") as f:
        j_bytes = f.read()
    c = struct.unpack("<%dh" % (len(c_bytes) // 2), c_bytes)
    j = struct.unpack("<%dh" % (len(j_bytes) // 2), j_bytes)
    n = min(len(c), len(j))
    print("c_samples=%d java_samples=%d compared=%d" % (len(c), len(j), n))
    diff = [abs(a - b) for a, b in zip(c[:n], j[:n])]
    exact = sum(1 for d in diff if d == 0)
    print("full: exact=%d/%d (%.6f%%) MAD=%.6f max_abs=%d" % (
        exact, n, 100.0 * exact / n if n else 0.0,
        sum(diff) / n if n else 0.0, max(diff) if diff else 0))
    w = min(window, n)
    wd = diff[:w]
    we = sum(1 for d in wd if d == 0)
    print("first_%d: exact=%d/%d (%.6f%%) MAD=%.6f max_abs=%d" % (
        w, we, w, 100.0 * we / w if w else 0.0,
        sum(wd) / w if w else 0.0, max(wd) if wd else 0))
    print("first_16_c =", list(c[:16]))
    print("first_16_java =", list(j[:16]))
    if diff:
        first = next((i for i, d in enumerate(diff) if d != 0), None)
        print("first_mismatch_index =", first)
    return 0 if n == len(c) == len(j) and exact == n else 1


if __name__ == "__main__":
    raise SystemExit(main())
