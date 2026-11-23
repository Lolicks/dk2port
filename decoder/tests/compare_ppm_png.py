#!/usr/bin/env python3
"""Compare a decoder PPM against an OpenKeeper PNG, byte for byte.

PPM parsing uses only the Python standard library. PNG decoding uses
ImageMagick's `magick <png> ppm:-` (falling back to `convert`), so no Python
image packages are required. Reports exact per-channel and overall mean
absolute differences and the number of exactly equal bytes.

Usage: compare_ppm_png.py <decoded.ppm> <reference.png> [--expect-exact]
Exit status: 0 on success (and on a clean mismatch unless --expect-exact),
1 if --expect-exact was given and the pixels differ, 2 on invalid input.
"""
import subprocess
import sys


def read_ppm(data):
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if pos < len(data) and data[pos:pos + 1] == b"#":
            while pos < len(data) and data[pos:pos + 1] not in (b"\n", b"\r"):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("truncated PPM header")
        tokens.append(data[start:pos])
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError("expected binary PPM with maxval 255")
    width, height = int(tokens[1]), int(tokens[2])
    pos += 1
    pixels = data[pos:]
    if len(pixels) != width * height * 3:
        raise ValueError("PPM payload size %d != %d" % (len(pixels), width * height * 3))
    return width, height, pixels


def png_to_ppm(path):
    for exe in ("magick", "convert"):
        try:
            proc = subprocess.run([exe, path, "ppm:-"], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, check=False)
        except FileNotFoundError:
            continue
        if proc.returncode == 0:
            return proc.stdout
        raise ValueError("%s failed: %s" % (exe, proc.stderr.decode("utf-8", "replace")[:200]))
    raise ValueError("ImageMagick (magick or convert) not found")


def main(argv):
    if len(argv) not in (3, 4) or (len(argv) == 4 and argv[3] != "--expect-exact"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(argv[1], "rb") as f:
        w1, h1, a = read_ppm(f.read())
    w2, h2, b = read_ppm(png_to_ppm(argv[2]))
    if (w1, h1) != (w2, h2):
        print("size mismatch: %dx%d vs %dx%d" % (w1, h1, w2, h2), file=sys.stderr)
        return 2

    total = 0
    chan = [0, 0, 0]
    equal = 0
    maximum = 0
    for i in range(0, len(a)):
        d = abs(a[i] - b[i])
        total += d
        chan[i % 3] += d
        equal += (d == 0)
        if d > maximum:
            maximum = d
    n = len(a)
    print("file=%s reference=%s size=%dx%d bytes=%d" % (argv[1], argv[2], w1, h1, n))
    print("exact_bytes=%d/%d (%.6f%%) max_abs_diff=%d" % (equal, n, 100.0 * equal / n, maximum))
    print("MAD_all=%.9f MAD_R=%.9f MAD_G=%.9f MAD_B=%.9f" %
          (total / n, chan[0] / (n / 3), chan[1] / (n / 3), chan[2] / (n / 3)))
    if len(argv) == 4 and total != 0:
        print("MISMATCH: --expect-exact requested", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
