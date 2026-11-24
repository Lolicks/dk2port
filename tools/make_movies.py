#!/usr/bin/env python3
"""Decode every original TGQ cutscene to PNG frames, audio and a manifest."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = HERE.parent
TOOL = PORT / "decoder" / "build" / "dk2tool"
INFO_RE = re.compile(r"(\d+)x(\d+) frames=(\d+) audio_blocks=(\d+)")
LEVEL_RE = re.compile(r"^CutSceneLevel(\d+)$", re.IGNORECASE)
POSTER_ROOT = "textures/FrontEnd"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def movie_info(path):
    result = subprocess.run([str(TOOL), "tgq", str(path)],
                            capture_output=True, text=True)
    match = INFO_RE.search(result.stdout)
    if not match:
        return 0, 0, 0, 0
    return (int(match.group(1)), int(match.group(2)),
            int(match.group(3)), int(match.group(4)))


def frame_order(path):
    digits = re.sub(r"\D", "", path.stem)
    return int(digits) if digits else 0


def convert_ppm(ppm, png):
    try:
        from PIL import Image
        with Image.open(ppm) as image:
            image.save(png, format="PNG")
        return True
    except ImportError:
        pass
    try:
        subprocess.run(["sips", "-s", "format", "png", str(ppm),
                        "--out", str(png)], check=True, capture_output=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


def decode_frames(source, dest, limit):
    result = subprocess.run([str(TOOL), "tgqdecode", str(source), str(dest),
                             "--frames", str(limit)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        return None, result.stderr.strip()[:200] or "tgqdecode failed"
    names = []
    for ppm in sorted(dest.glob("frame_*.ppm"), key=frame_order):
        png = ppm.with_suffix(".png")
        if not convert_ppm(ppm, png):
            if png.exists():
                png.unlink()
            return None, "no PPM to PNG converter available"
        ppm.unlink()
        names.append(png.name)
    if not names:
        return None, "decoder produced no frames"
    return names, None


def decode_audio(source, dest):
    wav = dest / "audio.wav"
    if wav.exists():
        wav.unlink()
    result = subprocess.run([str(TOOL), "tgqaudio", str(source), str(wav)],
                            capture_output=True, text=True)
    if result.returncode == 0 and wav.is_file() and wav.stat().st_size > 44:
        return "audio.wav"
    if wav.exists():
        wav.unlink()
    return None


def poster_for(name, poster_dir):
    lowered = name.lower()
    candidates = []
    if "outro" in lowered:
        candidates.append("M-Outro-0.png")
    if lowered == "intro" or "bullfrog" in lowered:
        candidates.append("M-Intro-0.png")
    match = LEVEL_RE.match(name)
    if match:
        candidates.append("M-%03d-0.png" % (int(match.group(1)) - 1))
    if "trailer" in lowered:
        candidates.append("M-Hilight.png")
    for candidate in candidates:
        if (poster_dir / candidate).is_file():
            return POSTER_ROOT + "/" + candidate
    return None


def label_for(name):
    spaced = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name)
    spaced = re.sub(r"(?<=[A-Za-z])(?=\d)", " ", spaced)
    spaced = spaced.replace("_", " ").replace("-", " ")
    return " ".join(word.capitalize() for word in spaced.split())


def load_subtitles(path):
    if not path.is_file():
        return {}
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    if not isinstance(raw, dict):
        return {}
    result = {}
    for key, value in raw.items():
        ids = value.get("speechIDs") if isinstance(value, dict) else value
        if isinstance(ids, list):
            try:
                result[key.lower()] = [int(item) for item in ids]
            except (TypeError, ValueError):
                continue
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--frames", type=int, default=24)
    parser.add_argument("--poster-root", default=None)
    parser.add_argument("--subtitles", default=None)
    args = parser.parse_args(argv)

    if not os.path.isfile(str(TOOL)):
        print("build the C decoder first: cd decoder && ./build.sh",
              file=sys.stderr)
        return 2
    movies_dir = Path(args.game_root) / "Data" / "Movies"
    if not movies_dir.is_dir():
        print("no movies directory: %s" % movies_dir, file=sys.stderr)
        return 2

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    poster_dir = (Path(args.poster_root) if args.poster_root
                  else out.parent / "textures" / "FrontEnd")
    subtitles_path = (Path(args.subtitles) if args.subtitles
                      else movies_dir / "subtitles.json")
    subtitles = load_subtitles(subtitles_path)

    entries = []
    failures = []
    sources = sorted((p for p in movies_dir.glob("*.[Tt][Gg][Qq]") if p.is_file()),
                     key=lambda p: p.name.lower())
    for source in sources:
        name = source.stem
        dest = out / name
        if dest.exists():
            shutil.rmtree(str(dest))
        dest.mkdir(parents=True)
        width, height, total_frames, audio_blocks = movie_info(source)
        frames, error = decode_frames(source, dest, args.frames)
        if error:
            failures.append("%s: %s" % (source.name, error))
            print("%s: FAILED %s" % (source.name, error), file=sys.stderr)
            continue
        audio = decode_audio(source, dest)
        entries.append({
            "name": name,
            "directory": name,
            "source": source.name,
            "sourceSha256": sha256(source),
            "width": width,
            "height": height,
            "totalFrames": total_frames,
            "audioBlocks": audio_blocks,
            "fps": 25.0,
            "frameCount": len(frames),
            "frames": frames,
            "audio": audio,
            "poster": poster_for(name, poster_dir),
            "label": label_for(name),
            "speechIDs": subtitles.get(name.lower(), []),
        })
        print("%s: %d frames%s" % (source.name, len(frames),
                                   " + audio" if audio else ""))

    manifest = {
        "format": "DK2MoviesManifest",
        "version": 1,
        "fps": 25.0,
        "movies": entries,
    }
    manifest_path = out / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2,
                                        sort_keys=True) + "\n",
                             encoding="utf-8")
    print("movies: %d decoded, %d failed; manifest %s"
          % (len(entries), len(failures), manifest_path))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
