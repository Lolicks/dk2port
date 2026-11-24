#!/usr/bin/env python3
"""Extract original GUI icons from DK2TextureCache/EngineTextures.dat.

OpenKeeper stores GUI textures in the EngineTextures container with MM0/1/2
mipmap suffixes.  XML refers to the unsuffixed names, so we extract only MM0
and rename it to the path GameHUD.xml expects.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CACHE = os.path.normpath(os.path.join(ROOT, "..", "DK2TextureCache", "EngineTextures.dat"))
AUDIT = os.path.join(ROOT, "docs", "ui_audit.json")
OUT = os.path.join(ROOT, "ios", "DK2Port", "Resources", "Assets", "textures", "EngineTextures")
JAVA_SRC = os.path.join(ROOT, "tools", "uigen")


def main():
    if not os.path.isfile(CACHE):
        print("missing", CACHE, file=sys.stderr)
        return 2
    audit = json.load(open(AUDIT, encoding="utf-8"))
    wanted = []
    for path in audit.get("images_missing", []):
        low = path.lower()
        if low.startswith("textures/gui/") and low.endswith(".png"):
            wanted.append(path[len("textures/"):])
    if not wanted:
        print("no GUI icons needed")
        return 0

    with tempfile.TemporaryDirectory(prefix="dk2tex_") as tmp:
        classes = os.path.join(tmp, "classes")
        raw = os.path.join(tmp, "raw")
        os.makedirs(classes)
        os.makedirs(raw)
        subprocess.run([
            "javac", "-nowarn", "-d", classes,
            "-sourcepath", os.path.join(ROOT, "research", "OpenKeeper", "src", "main", "java") + ":" + JAVA_SRC,
            os.path.join(JAVA_SRC, "TextureMain.java"),
            os.path.join(JAVA_SRC, "com", "jme3", "math", "FastMath.java"),
            os.path.join(JAVA_SRC, "com", "jme3", "math", "Vector3f.java"),
            os.path.join(JAVA_SRC, "com", "jme3", "system", "AppSettings.java"),
            os.path.join(JAVA_SRC, "javax", "vecmath", "Vector3f.java"),
        ], check=True)
        env = dict(os.environ, JAVA_TOOL_OPTIONS="-Djava.awt.headless=true")
        subprocess.run(["java", "-cp", classes, "TextureMain", CACHE, raw],
                       check=True, env=env)

        copied = 0
        missing = []
        for rel in wanted:
            base, ext = os.path.splitext(rel)
            src = os.path.join(raw, base + "MM0" + ext)
            if not os.path.isfile(src):
                missing.append(rel)
                continue
            dst = os.path.join(OUT, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            copied += 1
        print("copied", copied, "GUI icons; missing", len(missing))
        for rel in missing:
            print("  MISSING", rel)
        return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
