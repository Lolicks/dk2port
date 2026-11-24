#!/usr/bin/env python3
"""Replace the DK2 magenta transparency key with a real alpha channel."""
import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("Pillow is required: python3 -m pip install --user pillow", file=sys.stderr)
    raise

KEY = (255, 0, 255)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default="build/Assets")
    args = ap.parse_args()
    changed = 0
    scanned = 0
    for dirpath, _dirs, files in os.walk(args.root):
        for name in files:
            if not name.lower().endswith(".png"):
                continue
            path = os.path.join(dirpath, name)
            scanned += 1
            try:
                image = Image.open(path)
                if image.mode not in ("RGB", "RGBA", "P"):
                    continue
                rgba = image.convert("RGBA")
                pixels = rgba.load()
                hit = False
                for y in range(rgba.height):
                    for x in range(rgba.width):
                        r, g, b, a = pixels[x, y]
                        if (r, g, b) == KEY:
                            pixels[x, y] = (r, g, b, 0)
                            hit = True
                if hit:
                    rgba.save(path)
                    changed += 1
            except Exception as exc:
                print("SKIP", path, exc, file=sys.stderr)
    print("scanned=%d changed=%d" % (scanned, changed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
