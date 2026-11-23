#!/usr/bin/env python3
"""Reader for the documented part of original Dungeon Keeper 2 save files.

Source: research/dk2-save-tools/src/header.ts and creature.ts. Only the header
and the room table are reliably documented upstream; the per-creature block
is marked BROKEN there (variable-length records drift after entry 4), so this
tool refuses to invent creature data.

Commands:
    info <save.dk2>            print documented header fields
    convert <save.dk2> <out>   write a reconstructed DK2S v2 state
"""
import argparse
import json
import struct
import sys

MAGIC = b"DK2S"
HEADER_SIZE = 24
ROOM_RECORD = 28


class SaveError(Exception):
    pass


def read_header(data):
    if len(data) < HEADER_SIZE:
        raise SaveError("file too small to be a DK2 save")
    if data[:4] != MAGIC:
        raise SaveError("bad magic: not a DK2 save")
    version, level = struct.unpack_from("<HH", data, 4)
    gold, creatures, rooms = struct.unpack_from("<III", data, 8)
    return {
        "magic": MAGIC.decode(),
        "version": version,
        "level": level,
        "gold": gold,
        "creature_count": creatures,
        "room_count": rooms,
    }


def read_rooms(data, header):
    out = []
    pos = HEADER_SIZE
    for i in range(header["room_count"]):
        if pos + ROOM_RECORD > len(data):
            raise SaveError("room table truncated at entry %d" % i)
        name_len = struct.unpack_from("<H", data, pos)[0]
        if name_len > ROOM_RECORD - 2:
            raise SaveError("implausible room name length %d" % name_len)
        name = data[pos + 2:pos + 2 + name_len].decode("latin-1", "replace")
        out.append({"index": i, "name": name, "offset": pos})
        pos += ROOM_RECORD
    return out, pos


def convert(data, out_path):
    header = read_header(data)
    rooms, creature_offset = read_rooms(data, header)
    result = {
        "source_format": "DK2S original (header + room table)",
        "header": header,
        "rooms": rooms,
        "creature_block_offset": creature_offset,
        "creatures": [],
        "limitations": [
            "Original per-creature records are variable length and NOT decoded: "
            "dk2-save-tools marks this block BROKEN.",
            "No tile grid is present in the documented portion.",
        ],
    }
    with open(out_path, "w") as fh:
        json.dump(result, fh, indent=2)
    return result


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    for cmd in ("info", "convert"):
        p = sub.add_parser(cmd)
        p.add_argument("save")
        if cmd == "convert":
            p.add_argument("out")
    args = ap.parse_args(argv)
    try:
        with open(args.save, "rb") as fh:
            data = fh.read()
        header = read_header(data)
        if args.command == "info":
            rooms, off = read_rooms(data, header)
            print(json.dumps({"header": header, "rooms": rooms,
                              "creature_block_offset": off}, indent=2))
        else:
            r = convert(data, args.out)
            print("wrote %s (%d rooms, creature block at %d)" %
                  (args.out, len(r["rooms"]), r["creature_block_offset"]))
    except (OSError, SaveError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
