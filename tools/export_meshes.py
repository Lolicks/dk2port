#!/usr/bin/env python3
"""Export static MESH-type KMF files to OBJ using the C decoder CLI.

Input:  build/KMF/*.kmf   (produced by tools/kmsh_decompress.py)
Output: build/Assets/models/<name>.obj + models.json
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
TOOL = os.path.join(PORT, "decoder", "build", "dk2tool")


def main():
    kmf_dir = os.path.join(PORT, "build", "KMF")
    out_dir = os.path.join(PORT, "build", "Assets", "models")
    os.makedirs(out_dir, exist_ok=True)
    if not os.path.isfile(TOOL):
        print("build the C decoder first: cd decoder && ./build.sh", file=sys.stderr)
        return 2
    files = []
    for name in sorted(os.listdir(kmf_dir)):
        if not name.lower().endswith(".kmf"):
            continue
        src = os.path.join(kmf_dir, name)
        info = subprocess.run([TOOL, "kmf", src], capture_output=True, text=True)
        if info.returncode != 0 or "type=1 " not in info.stdout:
            continue
        stem = os.path.splitext(name)[0]
        obj = "models/" + stem + ".obj"
        dst = os.path.join(PORT, "build", "Assets", obj)
        r = subprocess.run([TOOL, "kmf", src, "--obj", dst],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.isfile(dst):
            continue
        files.append({"name": stem, "file": obj,
                      "bytes": os.path.getsize(dst)})
    with open(os.path.join(out_dir, "models.json"), "w") as fh:
        json.dump({"format": "OBJ", "count": len(files), "models": files},
                  fh, indent=2)
    print("exported %d OBJ meshes" % len(files))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
