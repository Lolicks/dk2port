#!/usr/bin/env python3
"""Decode DK2 BFST text files into id=value .properties for the iOS port.

The format and conversion rules follow OpenKeeper's StrFile.java and
MbToUniFile.java.  No Java runtime is required.
"""
import argparse
import json
import os
import struct
import sys


def decode_codepage(path):
    data = open(path, "rb").read()
    if data[:4] != b"BFMU":
        raise ValueError("bad MBToUni magic")
    single_count, _unknown, count1, count2 = data[4:8]
    threshold = 255 - single_count
    count = (((count1 - 1) + 257) * 255) - threshold * 254 + count2
    chars = struct.unpack("<%dH" % (len(data[8:]) // 2), data[8:8 + (len(data[8:]) // 2) * 2])
    return chars, threshold, count


def decode_chunk(chunk, codepage):
    chars, threshold, count = codepage
    out = []
    i = 0
    while i < len(chunk):
        idx = chunk[i] - 1
        i += 1
        if idx >= threshold:
            if i >= len(chunk):
                break
            idx = idx * 255 - threshold * 254 + chunk[i] - 1
            i += 1
        ch = chr(chars[idx]) if 0 <= idx < count and idx < len(chars) else ""
        if ch == "%":
            out.append("%%")
        elif ch in ("\\", "\n", "\t"):
            out.append("\\")
            out.append({"\\": "\\", "\n": "n", "\t": "t"}[ch])
        out.append(ch)
    return "".join(out)


def decode_entry(data, codepage):
    out = []
    pos = 0
    while pos + 4 <= len(data):
        word = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        kind = word & 0xFF
        length = word >> 8
        if kind == 0:
            break
        if kind == 2:
            val = (length + 1) // 10
            if val:
                out.append(str(val))
            out.append(str((length + 1) % 10))
            continue
        if kind != 1 or length > len(data) - pos:
            break
        out.append(decode_chunk(data[pos:pos + length], codepage))
        pos += length
        if pos % 4:
            pos += 4 - pos % 4
    return "".join(out).replace("\r", "\\r")


def decode_str(path, codepage):
    data = open(path, "rb").read()
    if data[:4] != b"BFST":
        raise ValueError("bad BFST magic: %s" % path)
    file_id, count = struct.unpack_from("<II", data, 4)
    offsets = struct.unpack_from("<%dI" % count, data, 12)
    ordered = sorted(offsets)
    entries = {}
    base = 12
    for i, off in enumerate(offsets):
        start = base + off
        if start >= len(data):
            continue
        import bisect
        j = bisect.bisect_left(ordered, off)
        end = base + (ordered[j + 1] if j + 1 < len(ordered) else len(data) - base)
        entries[i] = decode_entry(data[start:end], codepage)
    return file_id, entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--text-dir", default="game/Data/Text/Default")
    ap.add_argument("--out", default="ios/DK2Port/Resources/Assets/interface")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    codepage = decode_codepage(os.path.join(args.text_dir, "MBToUni.dat"))
    texts = {}
    for name, out_name in (("Text.str", "Text.properties"),
                           ("Speech.str", "Speech.properties")):
        path = os.path.join(args.text_dir, name)
        if not os.path.isfile(path):
            continue
        _file_id, entries = decode_str(path, codepage)
        texts[name] = entries
        if args.self_test:
            continue
        os.makedirs(args.out, exist_ok=True)
        with open(os.path.join(args.out, out_name), "w", encoding="utf-8") as fh:
            for key in sorted(entries):
                fh.write("%d=%s\n" % (key, entries[key]))

    if args.self_test:
        expected = {
            71: "Single Player Game",
            1408: "Main Objective",
            1409: "Sub Objective",
            2133: "Mana is the currency of magic and it’s this you spend each time you cast a spell.",
        }
        table = texts.get("Text.str", {})
        for key, want in expected.items():
            got = table.get(key)
            print(key, repr(got))
            if got != want:
                print("EXPECTED", repr(want), file=sys.stderr)
                return 1
        print("STR SELF TEST OK (%d text entries)" % len(table))
        return 0
    print("wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
