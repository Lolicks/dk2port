#!/usr/bin/env python3
"""Import original Dungeon Keeper 2 resources into a platform-neutral bundle.

The importer never invents content: every texture, sound and tile grid comes
straight from the shipped game files.  Output layout:

    <out>/manifest.json
    <out>/textures/<wad>/<relative name>
    <out>/audio/<stem>/sNNNN_<sample>.wav     (decoded) or .mp1/.pcm (raw)
    <out>/maps/<name>.bin                     DK2M tile grid
    <out>/maps/maps.json
    <out>/data/game_data.json
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from wad_tool import WadFile
from kwd_tool import KldMap, KwdError
from sdt_probe import SdtFile

MAP_MAGIC = b"DK2M"
MAP_VERSION = 1


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def safe_rel(name):
    name = name.replace("\\", "/").lstrip("/")
    parts = []
    for part in name.split("/"):
        if part in ("", ".", ".."):
            continue
        parts.append(re.sub(r"[^A-Za-z0-9._+-]", "_", part))
    return os.path.join(*parts) if parts else "unnamed.bin"


def unique_path(path, used):
    if path not in used:
        used.add(path)
        return path
    root, ext = os.path.splitext(path)
    i = 2
    while root + "__%d" % i + ext in used:
        i += 1
    out = root + "__%d" % i + ext
    used.add(out)
    return out


def import_wads(game_root, out):
    result = {}
    used = set()
    tex_root = os.path.join(out, "textures")
    for wad_name in ("EngineTextures.WAD", "FrontEnd.WAD", "Sprite.WAD",
                     "Paths.WAD", "Meshes.WAD"):
        src = os.path.join(game_root, "Data", wad_name)
        if not os.path.isfile(src):
            continue
        wad = WadFile(src)
        dest_dir = os.path.join(tex_root, wad_name.replace(".WAD", ""))
        os.makedirs(dest_dir, exist_ok=True)
        files = []
        for entry in wad.entries:
            ext = os.path.splitext(entry.name)[1].lower()
            if wad_name == "Meshes.WAD":
                ext = ext or ".kmf"
            rel = safe_rel(entry.name)
            if not os.path.splitext(rel)[1]:
                rel += ext
            dest = unique_path(os.path.join(dest_dir, rel), used)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            blob = wad.data[entry.data_offset:entry.data_offset + entry.data_size]
            with open(dest, "wb") as fh:
                fh.write(blob)
            files.append({
                "name": entry.name,
                "path": os.path.relpath(dest, out).replace(os.sep, "/"),
                "bytes": len(blob),
                "flags": entry.flags,
                "uncompressed_size": entry.uncompressed_size,
                "sha256": hashlib.sha256(blob).hexdigest(),
            })
        result[wad_name] = {
            "source_sha256": sha256(src),
            "entry_count": len(files),
            "files": files,
        }
        print("  WAD %-22s %5d entries" % (wad_name, len(files)))
    return result


def import_maps(game_root, out):
    src_dir = os.path.join(game_root, "Data", "editor", "maps")
    dest_dir = os.path.join(out, "maps")
    os.makedirs(dest_dir, exist_ok=True)
    maps = []
    skipped = []
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith(".kld"):
            continue
        if "map" not in name.lower():
            skipped.append(name)
            continue
        src = os.path.join(src_dir, name)
        try:
            kld = KldMap(path=src)
        except (KwdError, Exception) as exc:
            skipped.append("%s: %s" % (name, exc))
            continue
        if not kld.dimensions_valid:
            skipped.append("%s: dimension equation failed" % name)
            continue
        stem = os.path.splitext(name)[0]
        stem = re.sub(r"Map$", "", stem)
        dest = os.path.join(dest_dir, stem + ".bin")
        payload = struct.pack("<4sIII", MAP_MAGIC, MAP_VERSION,
                              kld.width, kld.height)
        payload += struct.pack("<%dI" % len(kld.tiles), *kld.tiles)
        with open(dest, "wb") as fh:
            fh.write(payload)
        maps.append({
            "name": stem,
            "source": name,
            "file": os.path.relpath(dest, out).replace(os.sep, "/"),
            "width": kld.width,
            "height": kld.height,
            "bytes": len(payload),
            "sha256": sha256(dest),
        })
    with open(os.path.join(dest_dir, "maps.json"), "w") as fh:
        json.dump({"format": MAP_MAGIC.decode(), "version": MAP_VERSION,
                   "tile_bytes": 4, "maps": maps}, fh, indent=2)
    print("  MAPS %d imported, %d skipped" % (len(maps), len(skipped)))
    return maps, skipped


def import_game_data(game_root, out):
    src = os.path.join(HERE, "..", "docs", "game_data.json")
    dest_dir = os.path.join(out, "data")
    os.makedirs(dest_dir, exist_ok=True)
    dest = os.path.join(dest_dir, "game_data.json")
    shutil.copyfile(src, dest)
    with open(dest, encoding="utf-8") as fh:
        data = json.load(fh)
    print("  DATA %d creatures, %d rooms, %d maps" % (
        len(data.get("creature_names", [])), len(data.get("room_names", [])),
        len(data.get("maps", []))))
    return {"sha256": sha256(dest), "counts": data.get("counts", {})}


def import_research_data(out):
    """Copy recovered stats/triggers into the runtime bundle."""
    dest_dir = os.path.join(out, "data")
    os.makedirs(dest_dir, exist_ok=True)
    copied = []
    for name in ("stats.json", "triggers.json"):
        src = os.path.join(HERE, "..", "docs", name)
        if os.path.isfile(src):
            shutil.copyfile(src, os.path.join(dest_dir, name))
            copied.append(name)
    print("  RESEARCH DATA %s" % ", ".join(copied))
    return copied


def import_missions(out):
    src = os.path.join(HERE, "..", "docs", "missions.json")
    if not os.path.isfile(src):
        return 0
    dest_dir = os.path.join(out, "data")
    os.makedirs(dest_dir, exist_ok=True)
    shutil.copyfile(src, os.path.join(dest_dir, "missions.json"))
    with open(src, encoding="utf-8") as fh:
        missions = json.load(fh)
    print("  MISSIONS %d records" % len(missions))
    return len(missions)


def decode_mpeg(src, dest):
    mpg = shutil.which("mpg123")
    if not mpg:
        return None
    try:
        r = subprocess.run(
            [mpg, "-q", "-w", dest, src],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=30, check=False)
        return dest if r.returncode == 0 and os.path.isfile(dest) else None
    except (subprocess.TimeoutExpired, OSError):
        return None


def import_audio(game_root, out, limit=None):
    src_dir = os.path.join(game_root, "Data", "Sound", "Sfx", "Global")
    if not os.path.isdir(src_dir):
        return {"banks": 0, "samples": 0, "wav": 0}
    dest_dir = os.path.join(out, "audio")
    os.makedirs(dest_dir, exist_ok=True)
    banks = samples = wav_count = 0
    catalog = []
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith(".sdt") or limit and banks >= limit:
            continue
        src = os.path.join(src_dir, name)
        sdt = SdtFile(src)
        stem = os.path.splitext(name)[0]
        bdir = os.path.join(dest_dir, stem)
        os.makedirs(bdir, exist_ok=True)
        for sample in sdt.samples:
            ext = {"mpeg-layer1": "mp1", "mpeg-layer2": "mp2",
                   "pcm16le": "pcm"}.get(sample["codec"], "bin")
            safe = re.sub(r"[^A-Za-z0-9_.-]", "_", sample["name"])[:40]
            raw = os.path.join(bdir, "s%04d_%s.%s" % (sample["index"], safe, ext))
            blob = sdt.payload(sample)
            with open(raw, "wb") as fh:
                fh.write(blob)
            wav = None
            if sample["codec"] in ("mpeg-layer1", "mpeg-layer2"):
                wav = decode_mpeg(raw, os.path.splitext(raw)[0] + ".wav")
                if wav:
                    wav_count += 1
            elif sample["codec"] == "pcm16le" and blob:
                from sdt_probe import _wav_header
                wav = os.path.splitext(raw)[0] + ".wav"
                with open(wav, "wb") as fh:
                    fh.write(_wav_header(len(blob), sample["sample_rate"], 1))
                    fh.write(blob)
                wav_count += 1
            catalog.append({
                "bank": stem,
                "index": sample["index"],
                "name": sample["name"],
                "codec": sample["codec"],
                "rate": sample["sample_rate"],
                "raw": os.path.relpath(raw, out).replace(os.sep, "/"),
                "wav": os.path.relpath(wav, out).replace(os.sep, "/") if wav else None,
                "bytes": len(blob),
            })
            samples += 1
        banks += 1
    with open(os.path.join(dest_dir, "audio.json"), "w") as fh:
        json.dump({"banks": banks, "samples": samples, "decoded": wav_count,
                   "files": catalog}, fh, indent=2)
    print("  AUDIO %d banks, %d samples, %d decoded" % (banks, samples, wav_count))
    return {"banks": banks, "samples": samples, "wav": wav_count}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game-root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--skip-audio", action="store_true")
    ap.add_argument("--audio-bank-limit", type=int, default=None)
    args = ap.parse_args(argv)

    game_root = os.path.abspath(args.game_root)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)

    print("Importing from", game_root)
    wads = import_wads(game_root, out)
    maps, skipped = import_maps(game_root, out)
    data = import_game_data(game_root, out)
    missions = import_missions(out)
    research = import_research_data(out)
    audio = {"banks": 0, "samples": 0, "wav": 0}
    if not args.skip_audio:
        audio = import_audio(game_root, out, args.audio_bank_limit)

    manifest = {
        "format": "DK2AssetBundle",
        "version": 1,
        "source": "original Dungeon Keeper 2 (GOG) installation",
        "note": "Contains copyrighted game resources. Never redistribute.",
        "wads": {k: {"source_sha256": v["source_sha256"],
                     "entry_count": v["entry_count"]} for k, v in wads.items()},
        "maps": len(maps),
        "missions": missions,
        "research_data": research,
        "movies": sorted(os.listdir(os.path.join(out, "movies")))
                  if os.path.isdir(os.path.join(out, "movies")) else [],
        "game_data": data,
        "audio": audio,
        "skipped_maps": skipped,
    }
    with open(os.path.join(out, "manifest.json"), "w") as fh:
        json.dump(manifest, fh, indent=2)
    print("Bundle written to", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
