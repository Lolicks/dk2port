#!/usr/bin/env python3
"""Standalone reader/extractor for Dungeon Keeper 2 ``DWFB`` WAD archives.

Pure Python 3 standard library.  Usage::

    wad_tool.py list <file.wad> [--json]
    wad_tool.py extract <file.wad> <outdir> [filters ...] [--json]

The container format was derived from the bytes of the shipped WADs; see
``docs/WAD_FORMAT.md`` for the full specification and the list of unknowns.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import struct
import sys

MAGIC = b"DWFB"
HEADER_SIZE = 0x58
ENTRY_SIZE = 0x28
EXPECTED_VERSION = 2

PNG_SIG = b"\x89PNG\r\n\x1a\n"


class WadError(Exception):
    """Raised when the container does not match the expected DWFB layout."""


class Entry:
    __slots__ = (
        "index", "hash_id", "name_offset", "name_length", "data_offset",
        "data_size", "flags", "uncompressed_size", "tail", "name",
    )

    def __init__(self, index, hash_id, name_offset, name_length, data_offset,
                 data_size, flags, uncompressed_size, tail, name):
        self.index = index
        self.hash_id = hash_id
        self.name_offset = name_offset
        self.name_length = name_length
        self.data_offset = data_offset
        self.data_size = data_size
        self.flags = flags
        self.uncompressed_size = uncompressed_size
        self.tail = tail
        self.name = name

    @property
    def is_compressed(self):
        return self.flags != 0

    def as_dict(self, type_guess=None):
        out = {
            "index": self.index,
            "name": self.name,
            "hash_id": self.hash_id,
            "name_offset": self.name_offset,
            "name_length": self.name_length,
            "data_offset": self.data_offset,
            "data_size": self.data_size,
            "flags": self.flags,
            "uncompressed_size": self.uncompressed_size,
        }
        if type_guess is not None:
            out["type"] = type_guess
        return out


class WadFile:
    def __init__(self, path):
        self.path = os.fspath(path)
        with open(self.path, "rb") as fh:
            self.data = fh.read()
        self.entries = []
        self.warnings = []
        self._parse()

    # ------------------------------------------------------------------ parse
    def _parse(self):
        d = self.data
        size = len(d)
        if size < HEADER_SIZE:
            raise WadError(f"file too small ({size} bytes); need >= {HEADER_SIZE}")
        if d[:4] != MAGIC:
            raise WadError(f"bad magic {d[:4]!r}, expected {MAGIC!r}")
        self.version = struct.unpack_from("<I", d, 4)[0]
        if self.version != EXPECTED_VERSION:
            self.warnings.append(
                f"unexpected version {self.version} (expected {EXPECTED_VERSION}); "
                "continuing with the version-2 layout"
            )

        (self.count, self.pool_offset, self.pool_size,
         self.header_reserved) = struct.unpack_from("<IIII", d, 0x48)

        index_end = HEADER_SIZE + self.count * ENTRY_SIZE
        if index_end > size:
            raise WadError(
                f"index of {self.count} entries ends at {index_end:#x}, "
                f"past EOF ({size:#x})"
            )

        for i in range(self.count):
            off = HEADER_SIZE + i * ENTRY_SIZE
            (hash_id, name_off, name_len, data_off, data_size, flags,
             uncomp, t1, t2, t3) = struct.unpack_from("<10I", d, off)

            if name_off + name_len > size or name_len == 0:
                raise WadError(f"entry {i}: name range out of bounds")
            raw = d[name_off:name_off + name_len]
            if raw.endswith(b"\x00"):
                name = raw[:-1].decode("latin-1")
            else:
                name = raw.decode("latin-1")
                self.warnings.append(f"entry {i}: name is not NUL-terminated")
            if data_off + data_size > size:
                raise WadError(f"entry {i}: data range out of bounds")

            self.entries.append(Entry(
                i, hash_id, name_off, name_len, data_off, data_size,
                flags, uncomp, (t1, t2, t3), name,
            ))

        self._verify_layout()

    def _verify_layout(self):
        d = self.data
        size = len(d)
        expected_pool = self.pool_offset + self.pool_size
        if expected_pool != size:
            self.warnings.append(
                f"name pool ends at {expected_pool:#x} but file size is {size:#x}"
            )
        if self.entries:
            first = self.entries[0].data_offset
            expected_first = HEADER_SIZE + self.count * ENTRY_SIZE
            if first != expected_first:
                self.warnings.append(
                    f"first data offset {first:#x} != index end {expected_first:#x}"
                )
            for prev, cur in zip(self.entries, self.entries[1:]):
                if prev.data_offset + prev.data_size != cur.data_offset:
                    self.warnings.append(
                        f"data gap/overlap between entries {prev.index} and {cur.index}"
                    )
            last_end = self.entries[-1].data_offset + self.entries[-1].data_size
            if last_end != self.pool_offset:
                self.warnings.append(
                    f"last data end {last_end:#x} != name pool start {self.pool_offset:#x}"
                )
        if sum(e.name_length for e in self.entries) != self.pool_size:
            self.warnings.append("sum of name lengths != name pool size")

    # ------------------------------------------------------------------ types
    def payload(self, entry):
        return self.data[entry.data_offset:entry.data_offset + entry.data_size]

    def guess_type(self, entry):
        b = self.payload(entry)
        if b.startswith(PNG_SIG):
            return "png"
        if b.startswith(b"BM"):
            return "bmp"
        if b.startswith(b"PSFB"):
            return "spr (PSFB sprite atlas)"
        if b[6:10] == b"KMSH" and entry.flags == 4:
            return "kmf (compressed KMSH)"
        ext = os.path.splitext(entry.name)[1].lower()
        return {
            ".kcs": "kcs (path data)",
            ".444": "data (444)",
            ".psd": "psd",
            ".gif": "gif",
            ".dat": "data",
            ".txt": "text",
        }.get(ext, "unknown")

    # ---------------------------------------------------------------- listing
    def listing(self):
        return [e.as_dict(self.guess_type(e)) for e in self.entries]

    def print_list(self):
        print(f"{self.path}: DWFB v{self.version}, {self.count} entries, "
              f"{len(self.data)} bytes")
        print(f"{'#':>5}  {'offset':>10} {'size':>10} {'flags':>5} "
              f"{'unc.size':>10}  {'type':<24} name")
        for e in self.entries:
            print(f"{e.index:5d}  {e.data_offset:10d} {e.data_size:10d} "
                  f"{e.flags:5d} {e.uncompressed_size:10d}  "
                  f"{self.guess_type(e):<24} {e.name}")
        for w in self.warnings:
            print(f"WARNING: {w}", file=sys.stderr)

    # -------------------------------------------------------------- extraction
    @staticmethod
    def _safe_components(name):
        parts = []
        for part in name.replace("\\", "/").split("/"):
            part = part.strip()
            if not part or part in (".", ".."):
                continue
            clean = "".join(c if c not in '<>:"|?*\x00' and ord(c) >= 32 else "_"
                            for c in part)
            if clean:
                parts.append(clean)
        return parts or ["unnamed"]

    def extract(self, outdir, filters=None):
        os.makedirs(outdir, exist_ok=True)
        outdir_real = os.path.realpath(outdir)
        results = []
        used = set()

        for e in self.entries:
            if filters and not any(
                fnmatch.fnmatchcase(e.name.lower(), f.lower()) for f in filters
            ):
                continue
            parts = self._safe_components(e.name)
            rel = os.path.join(*parts)
            target = os.path.realpath(os.path.join(outdir, *parts))
            if os.path.commonpath([target, outdir_real]) != outdir_real:
                raise WadError(f"unsafe output path for entry {e.index}")
            if rel.lower() in used:
                stem, ext = os.path.splitext(rel)
                rel = f"{stem}__{e.index}{ext}"
                target = os.path.realpath(os.path.join(outdir, rel))
            used.add(rel.lower())
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as fh:
                fh.write(self.payload(e))
            results.append({
                "index": e.index,
                "name": e.name,
                "output": rel,
                "bytes": e.data_size,
                "type": self.guess_type(e),
                "compressed": e.is_compressed,
            })
        return results


def _emit_json(obj):
    json.dump(obj, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="List/extract Dungeon Keeper 2 DWFB WAD archives"
    )
    parser.add_argument("--json", action="store_true",
                        help="emit machine-readable JSON")
    sub = parser.add_subparsers(dest="command", required=True)

    # Accept --json both before and after the subcommand.
    p_list = sub.add_parser("list", help="list entries")
    p_list.add_argument("file")
    p_list.add_argument("--json", action="store_true",
                        default=argparse.SUPPRESS, help="emit machine-readable JSON")

    p_ext = sub.add_parser("extract", help="extract entries")
    p_ext.add_argument("file")
    p_ext.add_argument("outdir")
    p_ext.add_argument("filters", nargs="*", help="name globs (default: all)")
    p_ext.add_argument("--json", action="store_true",
                       default=argparse.SUPPRESS, help="emit machine-readable JSON")

    args = parser.parse_args(argv)
    try:
        wad = WadFile(args.file)
    except (OSError, WadError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.command == "list":
        if args.json:
            _emit_json({
                "file": wad.path,
                "magic": MAGIC.decode(),
                "version": wad.version,
                "entry_count": wad.count,
                "file_size": len(wad.data),
                "name_pool_offset": wad.pool_offset,
                "name_pool_size": wad.pool_size,
                "warnings": wad.warnings,
                "entries": wad.listing(),
            })
        else:
            wad.print_list()
        return 0

    results = wad.extract(args.outdir, args.filters or None)
    if args.json:
        _emit_json({
            "file": wad.path,
            "outdir": args.outdir,
            "extracted": len(results),
            "entries": results,
            "warnings": wad.warnings,
        })
    else:
        for r in results:
            print(f"[{r['index']:5d}] {r['name']} -> {r['output']} "
                  f"({r['bytes']} bytes, {r['type']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
