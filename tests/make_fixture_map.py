#!/usr/bin/env python3
"""Create a tiny synthetic DK2M map for CI (no copyrighted assets required)."""
import struct
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "build/fixture.bin"
w = h = 16
tiles = []
for y in range(h):
    for x in range(w):
        border = x == 0 or y == 0 or x == w - 1 or y == h - 1
        terrain = 1 if border else (9 if (x + y) % 7 == 0 else 2)
        tiles.append(terrain | (1 << 8) | (1 << 16))
with open(out, "wb") as fh:
    fh.write(struct.pack("<4sIII", b"DK2M", 1, w, h))
    fh.write(struct.pack("<%dI" % len(tiles), *tiles))
print(out)
