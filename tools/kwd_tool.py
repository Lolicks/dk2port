#!/usr/bin/env python3
"""Standalone reader for Dungeon Keeper 2 ``KWD``/``KLD`` chunked data files.

Pure Python 3 standard library.  See ``docs/KWD_FORMAT.md`` for the format
specification derived from the shipped game files.

Usage::

    kwd_tool.py tree <file>                 list the chunk tree with offsets
    kwd_tool.py dump <file> [--strings]     dump chunk payloads / strings
    kwd_tool.py map <file.kld> [--json]     parse a KLD map tile grid
    kwd_tool.py json <file>                 emit a structured JSON summary
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys

ROOT_HEADER_SIZE = 12
CHUNK_HEADER_SIZE = 8
STRING_MIN_LEN = 3
PRINTABLE = re.compile(rb"[ -~]{%d,}\x00" % STRING_MIN_LEN)

ROOT_NAMES = {
    0x64: "map", 0x6E: "terrain", 0x78: "rooms", 0x82: "traps",
    0x8C: "doors", 0x96: "keeper_spells", 0xA0: "creature_spells",
    0xAA: "creatures", 0xB4: "map_players", 0xBE: "map_things",
    0xD2: "map_triggers", 0xDC: "map_settings", 0xE6: "variables",
    0xF0: "objects", 0xFA: "effect_elements", 0x104: "shots",
    0x10E: "effects",
}

CHUNK_NAMES = {
    0x65: "map_dimensions", 0x66: "map_tiles",
    0x6F: "terrain_info", 0x70: "terrain_data",
    0x79: "rooms_info", 0x7A: "rooms_data",
    0x83: "traps_info", 0x84: "traps_data",
    0x8D: "doors_info", 0x8E: "doors_data",
    0x97: "keeper_spells_info", 0x98: "keeper_spells_data",
    0xA1: "creature_spells_info", 0xA2: "creature_spells_data",
    0xAB: "creatures_info", 0xAC: "creatures_data",
    0xB5: "players_info", 0xB6: "players_data",
    0xBF: "things_info", 0xC0: "things_data",
    0xD3: "triggers_info", 0xD4: "triggers_data",
    0xD5: "trigger_condition", 0xD6: "trigger_action",
    0xDD: "map_settings_info", 0xDE: "map_settings_extra",
    0xDF: "map_settings_data",
    0xE7: "variables_info", 0xE8: "variables_data",
    0xF1: "objects_info", 0xF2: "objects_data",
    0xFB: "effect_elements_info", 0xFC: "effect_elements_data",
    0x105: "shots_info", 0x106: "shots_data",
    0x10F: "effects_info", 0x110: "effects_data",
}


class KwdError(Exception):
    """Raised when a file does not follow the expected KWD/KLD layout."""


def tag_name(tag: int) -> str:
    return CHUNK_NAMES.get(tag, ROOT_NAMES.get(tag, "unknown"))


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


class Chunk:
    """One 8-byte-header chunk: tag u32, payload byte length u32, payload."""

    __slots__ = ("tag", "size", "header_offset", "payload_offset",
                 "payload", "raw", "children", "anomaly")

    def __init__(self, data, header_offset, size, payload_offset):
        self.tag = u32(data, header_offset)
        self.size = size
        self.header_offset = header_offset
        self.payload_offset = payload_offset
        self.payload = data[payload_offset:payload_offset + size]
        self.raw = data[header_offset:payload_offset + size]
        self.children = []
        self.anomaly = None

    @property
    def end_offset(self):
        return self.payload_offset + self.size

    @property
    def name(self):
        return tag_name(self.tag)

    def as_dict(self):
        return {
            "tag": hex(self.tag),
            "tag_decimal": self.tag,
            "name": self.name,
            "size": self.size,
            "header_offset": self.header_offset,
            "payload_offset": self.payload_offset,
            "end_offset": self.end_offset,
            "children": [c.as_dict() for c in self.children],
            "anomaly": self.anomaly,
        }


class Record:
    """One fixed-size record inside the main data chunk."""

    __slots__ = ("index", "offset", "size", "data")

    def __init__(self, index, offset, size, data):
        self.index = index
        self.offset = offset
        self.size = size
        self.data = data

    @property
    def name(self):
        raw = self.data[:28].split(b"\0", 1)[0]
        return raw.decode("latin-1")

    def strings(self, min_len=STRING_MIN_LEN):
        return extract_strings(self.data, self.offset, min_len)

    def as_dict(self, include_strings=False, min_len=STRING_MIN_LEN):
        out = {
            "index": self.index,
            "offset": self.offset,
            "size": self.size,
            "name": self.name,
        }
        if include_strings:
            out["strings"] = [s.as_dict() for s in self.strings(min_len)]
        return out


class StringHit:
    __slots__ = ("offset", "text", "record")

    def __init__(self, offset, text, record=None):
        self.offset = offset
        self.text = text
        self.record = record

    def as_dict(self):
        out = {"offset": self.offset, "text": self.text}
        if self.record is not None:
            out["record"] = self.record
        return out


def extract_strings(blob, base_offset=0, min_len=STRING_MIN_LEN):
    hits = []
    pattern = re.compile(rb"[ -~]{%d,}\x00" % min_len)
    for match in pattern.finditer(blob):
        text = match.group()[:-1].decode("latin-1")
        hits.append(StringHit(base_offset + match.start(), text))
    return hits


class KwdFile:
    """Parsed KWD/KLD file: root header plus a flat list of chunks."""

    def __init__(self, path=None, data=None):
        if data is None:
            if path is None:
                raise KwdError("path or data is required")
            with open(path, "rb") as handle:
                data = handle.read()
        self.path = os.fspath(path) if path is not None else None
        self.data = data
        self.warnings = []
        if len(data) < ROOT_HEADER_SIZE:
            raise KwdError("file is shorter than the 12-byte root header")
        self.root_tag = u32(data, 0)
        self.version = u32(data, 4)
        self.declared_size = u32(data, 8)
        if self.declared_size != len(data):
            self.warnings.append(
                "root declared size %d != file size %d"
                % (self.declared_size, len(data))
            )
        self.chunks = []
        self._parse_chunks(ROOT_HEADER_SIZE, len(data))
        self.info_chunk = None
        self.data_chunk = None
        self.records = []
        self.record_size = None
        self._find_records()
        for chunk in self.chunks:
            if chunk.tag in (0xD4,):
                self._try_nested(chunk)

    def _parse_chunks(self, start, end):
        pos = start
        while pos + CHUNK_HEADER_SIZE <= end:
            tag = u32(self.data, pos)
            size = u32(self.data, pos + 4)
            if tag < 0x40:
                self.warnings.append(
                    "implausible tag %#x at offset %d; %d trailing bytes "
                    "were not parsed as top-level chunks" % (tag, pos, end - pos)
                )
                break
            if pos + CHUNK_HEADER_SIZE + size > end:
                self.warnings.append(
                    "chunk %#x at %d declares %d bytes, only %d remain"
                    % (tag, pos, size, end - pos - CHUNK_HEADER_SIZE)
                )
                break
            self.chunks.append(Chunk(self.data, pos, size,
                                     pos + CHUNK_HEADER_SIZE))
            pos += CHUNK_HEADER_SIZE + size
        if pos != end and not self.warnings:
            self.warnings.append("chunk walk ended at %d, file ends at %d"
                                 % (pos, end))

    def _try_nested(self, chunk):
        pos = chunk.payload_offset
        found = []
        while pos + CHUNK_HEADER_SIZE <= len(self.data):
            tag = u32(self.data, pos)
            size = u32(self.data, pos + 4)
            if tag < 0x40 or size <= 0 or size > 4096:
                break
            if pos + CHUNK_HEADER_SIZE + size > len(self.data):
                break
            found.append(Chunk(self.data, pos, size, pos + CHUNK_HEADER_SIZE))
            pos += CHUNK_HEADER_SIZE + size
        if len(found) >= 2 and pos == len(self.data):
            chunk.children = found
            chunk.anomaly = (
                "declared size %d does not cover the %d nested chunks that "
                "extend to EOF" % (chunk.size, len(found))
            )

    def _find_records(self):
        for chunk in self.chunks:
            if chunk.size == 28 and chunk.tag == (self.root_tag + 1) & 0xFFFFFFFF:
                self.info_chunk = chunk
                self.record_count = u32(chunk.payload, 0)
                break
        else:
            self.record_count = None
        if self.info_chunk is None:
            if self.chunks and self.chunks[0].size == 28:
                self.info_chunk = self.chunks[0]
                self.record_count = u32(self.info_chunk.payload, 0)
        for chunk in self.chunks:
            if chunk.tag == (self.root_tag + 2) & 0xFFFFFFFF:
                self.data_chunk = chunk
                break
        if self.data_chunk is None or not self.record_count:
            return
        if self.data_chunk.size % self.record_count:
            self.warnings.append(
                "data chunk size %d is not a multiple of record count %d"
                % (self.data_chunk.size, self.record_count)
            )
            return
        self.record_size = self.data_chunk.size // self.record_count
        for index in range(self.record_count):
            offset = self.data_chunk.payload_offset + index * self.record_size
            self.records.append(
                Record(index, offset, self.record_size,
                       self.data[offset:offset + self.record_size])
            )

    @property
    def kind(self):
        return ROOT_NAMES.get(self.root_tag, "unknown")

    def strings(self, min_len=STRING_MIN_LEN):
        hits = extract_strings(self.data, 0, min_len)
        record_ranges = [(r.offset, r.offset + r.size, r.index)
                         for r in self.records]
        for hit in hits:
            for start, end, index in record_ranges:
                if start <= hit.offset < end:
                    hit.record = index
                    break
        return hits

    def record_names(self):
        return [r.name for r in self.records]

    def as_dict(self, include_strings=True, include_records=True,
                max_strings=2000, min_len=STRING_MIN_LEN):
        out = {
            "path": self.path,
            "file_size": len(self.data),
            "root_tag": hex(self.root_tag),
            "root_tag_decimal": self.root_tag,
            "kind": self.kind,
            "version": self.version,
            "declared_size": self.declared_size,
            "record_count": self.record_count,
            "record_size": self.record_size,
            "chunks": [c.as_dict() for c in self.chunks],
            "warnings": self.warnings,
        }
        if include_records:
            out["records"] = [r.as_dict() for r in self.records]
        if include_strings:
            hits = self.strings(min_len)
            truncated = len(hits) > max_strings
            out["strings"] = [s.as_dict() for s in hits[:max_strings]]
            out["string_total"] = len(hits)
            out["strings_truncated"] = truncated
        return out

    def to_bytes(self):
        if self.warnings:
            raise KwdError("cannot round-trip a file with parse warnings")
        return (struct.pack("<III", self.root_tag, self.version,
                            self.declared_size)
                + b"".join(c.raw for c in self.chunks))


class KldMap(KwdFile):
    """KLD map variant: 0x64 root, 0x65 dimensions, 0x66 tile array."""

    def __init__(self, path=None, data=None):
        super().__init__(path=path, data=data)
        self.width = None
        self.height = None
        self.tile_chunk = None
        self.tiles = []
        dim_chunk = None
        for chunk in self.chunks:
            if chunk.tag == 0x65 and chunk.size >= 8:
                dim_chunk = chunk
            elif chunk.tag == 0x66:
                self.tile_chunk = chunk
        if dim_chunk is None or self.tile_chunk is None:
            raise KwdError("not a KLD map file (no 0x65/0x66 chunk pair)")
        self.width, self.height = struct.unpack_from("<II", dim_chunk.payload, 0)
        expected = self.width * self.height * 4
        self.dimensions_valid = (self.tile_chunk.size == expected)
        count = len(self.tile_chunk.payload) // 4
        self.tiles = list(struct.unpack_from("<%dI" % count,
                                             self.tile_chunk.payload, 0))

    def as_dict(self, include_tiles=True):
        out = {
            "path": self.path,
            "file_size": len(self.data),
            "root_tag": hex(self.root_tag),
            "version": self.version,
            "width": self.width,
            "height": self.height,
            "tile_count": self.width * self.height,
            "tile_size_bytes": 4,
            "tile_chunk_size": self.tile_chunk.size,
            "expected_tile_chunk_size": self.width * self.height * 4,
            "dimensions_valid": self.dimensions_valid,
            "warnings": self.warnings,
        }
        if include_tiles:
            out["tiles"] = self.tiles
        return out


def hexdump(blob, base=0, length=96):
    lines = []
    for offset in range(0, min(len(blob), length), 16):
        row = blob[offset:offset + 16]
        hx = " ".join("%02x" % b for b in row)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        lines.append("  %08X  %-47s  %s" % (base + offset, hx, asc))
    return "\n".join(lines)


def cmd_tree(args):
    kwd = KwdFile(args.file)
    print("file:            %s" % (kwd.path or "<buffer>"))
    print("root tag:        0x%X (%s)" % (kwd.root_tag, kwd.kind))
    print("version:         %d" % kwd.version)
    print("declared size:   %d" % kwd.declared_size)
    print("file size:       %d" % len(kwd.data))
    print("chunks:          %d" % len(kwd.chunks))
    print("")
    print("  #  tag      name                      size      header     payload    end")
    for index, chunk in enumerate(kwd.chunks):
        print("%3d  0x%-6X %-24s %9d  %9d  %9d  %9d"
              % (index, chunk.tag, chunk.name, chunk.size,
                 chunk.header_offset, chunk.payload_offset,
                 chunk.end_offset))
        for child in chunk.children:
            print("       -> child 0x%-6X %-19s %9d  %9d  %9d  %9d"
                  % (child.tag, child.name, child.size,
                     child.header_offset, child.payload_offset,
                     child.end_offset))
        if chunk.anomaly:
            print("       ! %s" % chunk.anomaly)
    print("")
    print("record count:    %s" % kwd.record_count)
    print("record size:     %s" % kwd.record_size)
    if kwd.records:
        print("first records:   %s" % ", ".join(kwd.record_names()[:8]))
    if kwd.info_chunk:
        print("")
        print("info chunk payload (first 28 bytes):")
        print(hexdump(kwd.info_chunk.payload, kwd.info_chunk.payload_offset, 28))
    for warning in kwd.warnings:
        print("WARNING: %s" % warning)
    return 0


def cmd_dump(args):
    kwd = KwdFile(args.file)
    print("file: %s  root=0x%X  version=%d  chunks=%d"
          % (kwd.path or "<buffer>", kwd.root_tag, kwd.version,
             len(kwd.chunks)))
    for index, chunk in enumerate(kwd.chunks):
        print("")
        print("[chunk %d] tag=0x%X (%s) size=%d @0x%X"
              % (index, chunk.tag, chunk.name, chunk.size,
                 chunk.header_offset))
        print(hexdump(chunk.payload, chunk.payload_offset, args.bytes))
        for child in chunk.children:
            print("  [nested] tag=0x%X (%s) size=%d @0x%X"
                  % (child.tag, child.name, child.size,
                     child.header_offset))
            print(hexdump(child.payload, child.payload_offset, args.bytes))
    if kwd.records:
        print("")
        print("[records] count=%d size=%d" %
              (len(kwd.records), kwd.record_size))
        for record in kwd.records:
            print("  #%02d @0x%X name=%r" %
                  (record.index, record.offset, record.name))
            if args.strings:
                for hit in record.strings(args.min_len):
                    print("      0x%06X  %r" % (hit.offset, hit.text))
    if args.strings and not kwd.records:
        print("")
        print("[strings]")
        for hit in kwd.strings(args.min_len):
            print("  0x%06X  %r" % (hit.offset, hit.text))
    for warning in kwd.warnings:
        print("WARNING: %s" % warning)
    return 0


def cmd_map(args):
    kld = KldMap(args.file)
    info = kld.as_dict(include_tiles=args.json)
    if args.json:
        print(json.dumps(info, indent=2, ensure_ascii=False))
        return 0
    print("file:            %s" % (kld.path or "<buffer>"))
    print("root tag:        0x%X" % kld.root_tag)
    print("width x height:  %d x %d" % (kld.width, kld.height))
    print("tile count:      %d" % (kld.width * kld.height))
    print("tile size:       4 bytes")
    print("tile bytes:      %d" % kld.tile_chunk.size)
    print("expected bytes:  %d" % (kld.width * kld.height * 4))
    print("consistent:      %s" % kld.dimensions_valid)
    print("first 12 tiles:  %s"
          % ", ".join("0x%08X" % t for t in kld.tiles[:12]))
    for warning in kld.warnings:
        print("WARNING: %s" % warning)
    return 0 if kld.dimensions_valid else 1


def cmd_json(args):
    kwd = KwdFile(args.file)
    print(json.dumps(kwd.as_dict(max_strings=args.max_strings,
                                 min_len=args.min_len),
                     indent=2, ensure_ascii=False))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_tree = sub.add_parser("tree", help="list chunk tree with offsets")
    p_tree.add_argument("file")
    p_tree.set_defaults(func=cmd_tree)

    p_dump = sub.add_parser("dump", help="dump chunk payloads")
    p_dump.add_argument("file")
    p_dump.add_argument("--strings", action="store_true")
    p_dump.add_argument("--min-len", type=int, default=STRING_MIN_LEN)
    p_dump.add_argument("--bytes", type=int, default=96)
    p_dump.set_defaults(func=cmd_dump)

    p_map = sub.add_parser("map", help="parse a KLD tile grid")
    p_map.add_argument("file")
    p_map.add_argument("--json", action="store_true")
    p_map.set_defaults(func=cmd_map)

    p_json = sub.add_parser("json", help="structured JSON summary")
    p_json.add_argument("file")
    p_json.add_argument("--max-strings", type=int, default=2000)
    p_json.add_argument("--min-len", type=int, default=STRING_MIN_LEN)
    p_json.set_defaults(func=cmd_json)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (KwdError, OSError, struct.error) as exc:
        print("kwd_tool: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
