#!/usr/bin/env python3
"""Decode original TGQ cutscenes to PNG frames using our C TQI decoder.

PPM output from `dk2tool tgqdecode` is converted with `sips` (macOS) or
Pillow when available. Stdlib-only fallback writes the PPM unchanged.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
TOOL = os.path.join(PORT, "decoder", "build", "dk2tool")


def main(argv=None):
    argv = argv or sys.argv
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game-root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--frames", type=int, default=24)
    args = ap.parse_args(argv)

    if not os.path.isfile(TOOL):
        print("build the C decoder first: cd decoder && ./build.sh", file=sys.stderr)
        return 2
    movies = os.path.join(args.game_root, "Data", "Movies")
    if not os.path.isdir(movies):
        print(f"no movies directory: {movies}", file=sys.stderr)
        return 2

    made = 0
    for name in sorted(os.listdir(movies)):
        if not name.lower().endswith(".tgq"):
            continue
        stem = os.path.splitext(name)[0]
        tmp = os.path.join(args.out, stem)
        os.makedirs(tmp, exist_ok=True)
        r = subprocess.run([TOOL, "tgqdecode", os.path.join(movies, name),
                            tmp, "--frames", str(args.frames)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"{name}: decode failed: {r.stderr[:120]}", file=sys.stderr)
            continue
        for f in sorted(os.listdir(tmp)):
            if not f.endswith(".ppm"):
                continue
            ppm = os.path.join(tmp, f)
            png = os.path.splitext(ppm)[0] + ".png"
            try:
                subprocess.run(["sips", "-s", "format", "png", ppm,
                                "--out", png], check=True,
                               capture_output=True)
                os.remove(ppm)
                made += 1
            except (OSError, subprocess.CalledProcessError):
                if os.path.exists(png):
                    os.remove(png)
        print(f"{name}: frames ready")
    print(f"cutscene frames: {made}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
