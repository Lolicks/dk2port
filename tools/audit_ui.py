#!/usr/bin/env python3
"""Audit original UI XML references against decoded texts and bundled assets."""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OPENKEEPER = os.path.join(ROOT, "tools", "uigen", "ui_xml")
ASSETS = os.path.join(ROOT, "build", "Assets")
TEXT = os.path.join(ROOT, "ios", "DK2Port", "Resources", "Assets", "interface", "Text.properties")

IGNORE_PREFIXES = ("textfield/", "scrollbar/", "slider/")

MENU = re.compile(r"\$\{menu\.(\d+)\}")
IMAGE = re.compile(r'filename="([^"]+)"')


def load_ids(path):
    ids = set()
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if "=" in line:
                try:
                    ids.add(int(line.split("=", 1)[0]))
                except ValueError:
                    pass
    return ids


def resolve_image(name):
    name = name.replace("\\", "/")
    if name.lower().startswith("textures/"):
        rel = name[len("textures/"):]
    else:
        rel = name
    bundle_textures = os.path.join(ROOT, "ios", "DK2Port", "Resources", "Assets", "textures")
    candidates = [
        os.path.join(ASSETS, "textures", "FrontEnd", rel),
        os.path.join(bundle_textures, "FrontEnd", rel),
        os.path.join(ASSETS, "textures", "EngineTextures", rel),
        os.path.join(ASSETS, "textures", rel),
        os.path.join(bundle_textures, rel),
        os.path.join(bundle_textures, "EngineTextures", rel),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=os.path.join(ROOT, "docs", "ui_audit.json"))
    args = ap.parse_args()

    if not os.path.isdir(OPENKEEPER):
        print("OpenKeeper UI sources missing", file=sys.stderr)
        return 2
    known = load_ids(TEXT)
    menu_ids = set()
    images = set()
    missing_images = set()
    screens = []
    for dirpath, _dirs, files in os.walk(OPENKEEPER):
        for name in sorted(files):
            if not name.endswith(".xml"):
                continue
            path = os.path.join(dirpath, name)
            text = open(path, encoding="utf-8", errors="replace").read()
            text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
            screens.extend(re.findall(r'<screen[^>]+id="([^"]+)"', text))
            ids = {int(v) for v in MENU.findall(text)}
            menu_ids |= ids
            for image in IMAGE.findall(text):
                if "$" in image or not re.search(r"\.(png|bmp|444|gif)$", image, re.I):
                    continue
                if image.lower().startswith(IGNORE_PREFIXES):
                    continue
                images.add(image)
                if not resolve_image(image):
                    missing_images.add(image)

    missing_text = sorted(menu_ids - known)
    report = {
        "screens": sorted(screens),
        "screen_count": len(screens),
        "menu_ids": len(menu_ids),
        "menu_ids_missing": missing_text,
        "images": len(images),
        "images_missing": sorted(missing_images),
    }
    with open(args.json, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2, ensure_ascii=False)
    print("screens=%d menu_ids=%d missing_text=%d images=%d missing_images=%d" %
          (len(screens), len(menu_ids), len(missing_text), len(images), len(missing_images)))
    print("wrote", args.json)
    return 1 if missing_text else 0


if __name__ == "__main__":
    raise SystemExit(main())
