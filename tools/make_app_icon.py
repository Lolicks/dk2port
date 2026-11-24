#!/usr/bin/env python3
"""Generate the iOS app icon from the original Horned Reaper artwork."""
import json
import os
import shutil

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOURCE = os.path.join(ROOT, "build", "Assets", "textures", "EngineTextures", "reaper.png")
OUT = os.path.join(ROOT, "ios", "DK2Port", "Resources", "Assets.xcassets", "AppIcon.appiconset")
LEGACY = os.path.join(ROOT, "ios", "DK2Port", "Resources", "AppIconLegacy")

SIZES = [
    ("icon-20@2x.png", 40), ("icon-20@3x.png", 60),
    ("icon-29@2x.png", 58), ("icon-29@3x.png", 87),
    ("icon-40@2x.png", 80), ("icon-40@3x.png", 120),
    ("icon-60@2x.png", 120), ("icon-60@3x.png", 180),
    ("icon-76@2x.png", 152), ("icon-83.5@2x.png", 167),
    ("icon-1024.png", 1024),
]

PAIRS = [
    ("icon-20@2x.png", "20", "2x"), ("icon-20@3x.png", "20", "3x"),
    ("icon-29@2x.png", "29", "2x"), ("icon-29@3x.png", "29", "3x"),
    ("icon-40@2x.png", "40", "2x"), ("icon-40@3x.png", "40", "3x"),
    ("icon-60@2x.png", "60", "2x"), ("icon-60@3x.png", "60", "3x"),
    ("icon-76@2x.png", "76", "2x"), ("icon-83.5@2x.png", "83.5", "2x"),
    ("icon-1024.png", None, None),
]

def make_base(size):
    art = Image.open(SOURCE).convert("RGBA")
    pad = max(1, size // 16)
    art = art.resize((size - 2 * pad, size - 2 * pad), Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", (size, size), (16, 11, 9, 255))
    canvas.alpha_composite(art, (pad, pad))
    draw = ImageDraw.Draw(canvas)
    border = max(1, size // 128)
    draw.rectangle([0, 0, size - 1, size - 1],
                   outline=(150, 105, 48, 255), width=border)
    return canvas.convert("RGB")


def main():
    os.makedirs(OUT, exist_ok=True)
    for filename, size in SIZES:
        make_base(size).save(os.path.join(OUT, filename), "PNG")

    images = []
    for filename, idiom, scale in PAIRS:
        entry = {"filename": filename}
        if idiom:
            entry.update({"idiom": "ios", "size": "{}x{}".format(idiom, idiom)})
        else:
            entry.update({"idiom": "ios-marketing", "size": "1024x1024"})
        if scale:
            entry["scale"] = scale
        images.append(entry)
    os.makedirs(LEGACY, exist_ok=True)
    for filename, _size in SIZES:
        shutil.copyfile(os.path.join(OUT, filename),
                        os.path.join(LEGACY, filename.replace("icon-", "Icon-")))

    with open(os.path.join(OUT, "Contents.json"), "w", encoding="utf-8") as fh:
        json.dump({"images": images, "info": {"author": "dk2port", "version": 1}},
                  fh, indent=2)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
