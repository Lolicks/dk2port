#!/usr/bin/env python3
"""Write an integrity manifest for the resources embedded into the app.

Only cheap checks are stored: file size and SHA-256 for a small set of critical
archives. Full 466 MB hashing on every launch would be too slow on iPhone.
"""
import hashlib
import json
import os
import sys

CRITICAL = [
    "Original/Data/Meshes.WAD",
    "Original/Data/EngineTextures.WAD",
    "Original/Data/FrontEnd.WAD",
    "Original/Data/Paths.WAD",
    "Original/Data/Sprite.WAD",
    "Original/Data/Movies/BullfrogIntro.tgq",
    "Original/Data/Movies/INTRO.TGQ",
    "Assets/data/game_data.json",
    "Assets/data/stats.json",
    "Assets/data/triggers.json",
    "Assets/data/missions.json",
    "Assets/maps/maps.json",
]


def main(argv=None):
    if len(argv) < 3:
        print("usage: make_integrity.py <resources-root> <out.json>", file=sys.stderr)
        return 2
    root, out = argv[1], argv[2]
    entries = []
    for rel in CRITICAL:
        path = os.path.join(root, rel)
        item = {"path": rel, "present": os.path.isfile(path)}
        if item["present"]:
            item["size"] = os.path.getsize(path)
            if item["size"] < 16 * 1024 * 1024:
                h = hashlib.sha256()
                with open(path, "rb") as fh:
                    for chunk in iter(lambda: fh.read(1 << 20), b""):
                        h.update(chunk)
                item["sha256"] = h.hexdigest()
        entries.append(item)
    manifest = {"format": "DK2Integrity", "version": 1, "files": entries,
                "required_count": len(CRITICAL)}
    with open(out, "w") as fh:
        json.dump(manifest, fh, indent=2)
    ok = sum(1 for e in entries if e["present"])
    print(f"integrity manifest: {ok}/{len(entries)} critical files present")
    return 0 if ok == len(entries) else 3


if __name__ == "__main__":
    raise SystemExit(main())
