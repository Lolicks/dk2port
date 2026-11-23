#!/usr/bin/env python3
"""Reader/extractor for Dungeon Keeper 2 ``SDT`` sound banks and ``.map`` indexes.

Pure Python 3 standard library.  See ``docs/SDT_FORMAT.md`` for the format
specification derived from the shipped files and the explicit unknowns.

Usage::

    sdt_probe.py info <file.sdt> [--json]
    sdt_probe.py list <file.sdt> [--json]
    sdt_probe.py extract <file.sdt> <outdir> [--index N] [--name PAT] [--wav] [--json]
    sdt_probe.py map <file.map> [--json]
    sdt_probe.py scan <dir-or-glob> ... [--json]
"""

from __future__ import annotations

import argparse
import fnmatch
import glob
import json
import os
import re
import struct
import sys

SAMPLE_HEADER_SIZE = 40
GUID_BANK = 0x01
GUID_SFX = 0x00
BANK_RECORD_SIZE = 11
SFX_RECORD_SIZE = 20
SFX_HEADER_SIZE = 0x34

FORMAT_CODECS = {
    0x0010: "empty/blank",
    0x0210: "pcm16le",
    0x2410: "mpeg-layer1",
    0x2510: "mpeg-layer2",
}


class SdtError(Exception):
    pass


# ---------------------------------------------------------------------- MPEG
MP_SYNC = re.compile(b"\xff[\xf0-\xf7]")


def mpeg_header_at(data, pos):
    """Decode an MPEG audio frame header (no frame validation)."""
    if pos + 4 > len(data):
        return None
    b1, b2, b3 = data[pos + 1], data[pos + 2], data[pos + 3]
    if b1 & 0xF0 != 0xF0 or (b2 & 0xE0) != 0xE0:
        return None
    version_bits = (b2 >> 3) & 3
    layer_bits = (b2 >> 1) & 3
    protection = b2 & 1
    bitrate_index = (b3 >> 4) & 0xF
    sr_index = (b3 >> 2) & 3
    padding = (b3 >> 1) & 1
    if version_bits == 1 or layer_bits == 0 or bitrate_index in (0, 15):
        return None
    return {
        "offset": pos,
        "header": data[pos:pos + 4].hex(" "),
        "version_bits": version_bits,
        "layer_bits": layer_bits,
        "protection": protection,
        "bitrate_index": bitrate_index,
        "sample_rate_index": sr_index,
        "padding": padding,
    }


def analyse_frames(blob, limit=64):
    """Find repeated MPEG frame syncs and return a compact observation."""
    positions = [m.start() for m in MP_SYNC.finditer(blob)]
    headers = []
    for p in positions[:8]:
        h = mpeg_header_at(blob, p)
        if h:
            headers.append(h)
    strides = [positions[i] - positions[i - 1] for i in range(1, min(len(positions), limit))]
    from collections import Counter
    return {
        "sync_candidates": len(positions),
        "first_positions": positions[:8],
        "stride_histogram": Counter(strides).most_common(6),
        "headers": headers[:4],
    }


# ------------------------------------------------------------------------ SDT
class SdtFile:
    def __init__(self, path):
        self.path = os.fspath(path)
        with open(self.path, "rb") as fh:
            self.data = fh.read()
        self.size = len(self.data)
        self.warnings = []
        self.ok = False
        self.count = 0
        self.offsets = []
        self.samples = []
        self._parse()

    def _parse(self):
        if self.size < 4:
            raise SdtError("file shorter than 4 bytes")
        self.count = struct.unpack_from("<I", self.data, 0)[0]
        if 4 + 4 * self.count > self.size:
            raise SdtError("offset table (%d entries) exceeds file" % self.count)
        self.offsets = list(struct.unpack_from("<%dI" % self.count, self.data, 4))
        if self.count and self.offsets[0] != 4 + 4 * self.count:
            self.warnings.append(
                "first offset %d != end of offset table %d"
                % (self.offsets[0], 4 + 4 * self.count))
        for i, off in enumerate(self.offsets):
            end = self.offsets[i + 1] if i + 1 < self.count else self.size
            if off + SAMPLE_HEADER_SIZE > end or off + SAMPLE_HEADER_SIZE > self.size:
                self.warnings.append("sample %d at %d has truncated header" % (i, off))
                continue
            hs, ds = struct.unpack_from("<II", self.data, off)
            if hs != SAMPLE_HEADER_SIZE:
                self.warnings.append("sample %d header_size=%d (expected 40)" % (i, hs))
            name_raw = self.data[off + 8:off + 40]
            name = name_raw.split(b"\0", 1)[0].decode("latin1", "replace")
            rate = struct.unpack_from("<H", self.data, off + 0x18)[0]
            fmt = struct.unpack_from("<H", self.data, off + 0x1A)[0]
            f1c = struct.unpack_from("<I", self.data, off + 0x1C)[0]
            f20 = struct.unpack_from("<I", self.data, off + 0x20)[0]
            f24 = struct.unpack_from("<I", self.data, off + 0x24)[0]
            payload_start = off + SAMPLE_HEADER_SIZE
            payload_end = min(end, payload_start + ds)
            if payload_end - payload_start != ds:
                self.warnings.append(
                    "sample %d declares %d data bytes, %d available"
                    % (i, ds, payload_end - payload_start))
            blob = self.data[payload_start:payload_end]
            codec = FORMAT_CODECS.get(fmt, "unknown")
            magic = blob[:4].hex(" ") if blob else ""
            sample = {
                "index": i,
                "offset": off,
                "name": name,
                "header_size": hs,
                "data_size": ds,
                "data_offset": payload_start,
                "sample_rate": rate,
                "format_u16": fmt,
                "field_1C": f1c,
                "field_20": f20,
                "field_24": f24,
                "codec": codec,
                "magic": magic,
            }
            self.samples.append(sample)
        self.ok = not any("truncated header" in w for w in self.warnings)

    def payload(self, sample):
        start = sample["data_offset"]
        return self.data[start:start + sample["data_size"]]

    def summary(self):
        from collections import Counter
        codecs = Counter(s["codec"] for s in self.samples)
        rates = Counter(s["sample_rate"] for s in self.samples)
        fmts = Counter(s["format_u16"] for s in self.samples)
        return {
            "path": self.path,
            "file_size": self.size,
            "count": self.count,
            "samples_parsed": len(self.samples),
            "first_offset_ok": bool(self.offsets) and self.offsets[0] == 4 + 4 * self.count,
            "codecs": dict(codecs),
            "sample_rates": dict(rates),
            "format_u16": {hex(k): v for k, v in fmts.items()},
            "warnings": self.warnings,
        }


def cmd_info(args):
    sdt = SdtFile(args.file)
    s = sdt.summary()
    if args.json:
        print(json.dumps(s, indent=2))
        return
    print("SDT: %s" % s["path"])
    print("file size       : %d bytes" % s["file_size"])
    print("sample count    : %d (offset table %s)" %
          (s["count"], "OK" if s["first_offset_ok"] else "SUSPECT"))
    print("samples parsed  : %d" % s["samples_parsed"])
    print("codecs          : %s" % ", ".join("%s=%d" % kv for kv in sorted(s["codecs"].items())))
    print("sample rates    : %s" % ", ".join("%d=%d" % kv for kv in sorted(s["sample_rates"].items())))
    print("format u16      : %s" % ", ".join("%s=%d" % kv for kv in sorted(s["format_u16"].items())))
    for w in s["warnings"][:10]:
        print("WARNING: %s" % w)
    # frame structure of the first real MPEG sample
    for sample in sdt.samples:
        if sample["codec"].startswith("mpeg") and sample["data_size"] >= 8:
            blob = sdt.payload(sample)
            fa = analyse_frames(blob)
            print("frame probe (%s, %s):" % (sample["name"], sample["codec"]))
            print("  sync candidates: %d, first offsets: %s" %
                  (fa["sync_candidates"], fa["first_positions"]))
            print("  stride histogram: %s" % fa["stride_histogram"])
            for h in fa["headers"][:2]:
                print("  header @%d %s  version_bits=%d layer_bits=%d bitrate_idx=%d sr_idx=%d padding=%d"
                      % (h["offset"], h["header"], h["version_bits"], h["layer_bits"],
                         h["bitrate_index"], h["sample_rate_index"], h["padding"]))
            break


def cmd_list(args):
    sdt = SdtFile(args.file)
    rows = sdt.samples
    if args.json:
        print(json.dumps(rows, indent=2))
        return
    print("%-4s %-9s %-28s %10s %7s %10s %-14s %s" %
          ("#", "offset", "name", "data_size", "rate", "format", "codec", "magic"))
    for s in rows:
        print("%-4d %-9d %-28.28s %10d %7d 0x%04X      %-14s %s" %
              (s["index"], s["offset"], s["name"], s["data_size"], s["sample_rate"],
               s["format_u16"], s["codec"], s["magic"]))


def _wav_header(data_bytes, rate, channels=1):
    return (b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVE" +
            b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate,
                                  rate * channels * 2, channels * 2, 16) +
            b"data" + struct.pack("<I", data_bytes))


def cmd_extract(args):
    sdt = SdtFile(args.file)
    os.makedirs(args.outdir, exist_ok=True)
    chosen = []
    for s in sdt.samples:
        if args.index is not None and s["index"] != args.index:
            continue
        if args.name and not fnmatch.fnmatch(s["name"], args.name):
            continue
        chosen.append(s)
    manifest = {"source": sdt.path, "output_dir": args.outdir,
                "note": ("payload bytes are written verbatim; MPEG streams are complete "
                         "elementary streams (verify with 'file' or mpg123). PCM samples "
                         "are 16-bit little-endian; --wav wraps them in a canonical RIFF header."),
                "files": []}
    for s in chosen:
        payload = sdt.payload(s)
        ext = {"mpeg-layer1": "mp1", "mpeg-layer2": "mp2",
               "pcm16le": "pcm", "empty/blank": "bin",
               "unknown": "bin"}.get(s["codec"], "bin")
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", s["name"])[:48] or "unnamed"
        fname = "s%04d_%s.%s" % (s["index"], safe, ext)
        out = os.path.join(args.outdir, fname)
        with open(out, "wb") as fh:
            fh.write(payload)
        rec = {"index": s["index"], "name": s["name"], "codec": s["codec"],
               "sample_rate": s["sample_rate"], "bytes": len(payload),
               "path": out, "field_20": s["field_20"]}
        if args.wav and s["codec"] == "pcm16le":
            wout = os.path.splitext(out)[0] + ".wav"
            with open(wout, "wb") as fh:
                fh.write(_wav_header(len(payload), s["sample_rate"], 1))
                fh.write(payload)
            rec["wav_path"] = wout
        manifest["files"].append(rec)
    mpath = os.path.join(args.outdir, "sdt_extract.json")
    with open(mpath, "w") as fh:
        json.dump(manifest, fh, indent=2)
    if args.json:
        print(json.dumps(manifest, indent=2))
    else:
        print("extracted %d samples to %s" % (len(chosen), args.outdir))


# ------------------------------------------------------------------------ MAP
def cmd_map(args):
    d = open(args.file, "rb").read()
    if len(d) < 32:
        raise SdtError("map file shorter than 32 bytes")
    guid = d[:16].hex(" ")
    kind = "BANK" if d[0] == GUID_BANK else ("SFX" if d[0] == GUID_SFX else "UNKNOWN")
    out = {"path": args.file, "size": len(d), "guid": guid, "kind": kind,
           "zeros_10_1F": d[0x10:0x20].hex(" ") == "00 " * 16}
    if kind == "BANK":
        n = struct.unpack_from("<I", d, 0x18)[0]
        out["count"] = n
        out["records"] = []
        off = 0x1C
        for i in range(n):
            if off + BANK_RECORD_SIZE > len(d):
                out.setdefault("warnings", []).append("record %d truncated" % i)
                break
            rec = d[off:off + BANK_RECORD_SIZE]
            out["records"].append({
                "index": i,
                "f0": struct.unpack_from("<I", rec, 0)[0],
                "f1": struct.unpack_from("<I", rec, 4)[0],
                "f2": rec[8:11].hex(" "),
            })
            off += BANK_RECORD_SIZE
        names = []
        for i in range(n):
            if off + 4 > len(d):
                out.setdefault("warnings", []).append("string %d truncated" % i)
                break
            L = struct.unpack_from("<I", d, off)[0]
            off += 4
            if L <= 0 or off + L > len(d):
                out.setdefault("warnings", []).append("string %d bad length %d" % (i, L))
                break
            s = d[off:off + L]
            off += L
            names.append(s.rstrip(b"\0").decode("latin1", "replace"))
        out["bank_names"] = names
        out["ends_exactly_at_eof"] = off == len(d)
    else:
        n = struct.unpack_from("<I", d, 0x1C)[0]
        out["marker_18"] = struct.unpack_from("<I", d, 0x18)[0]
        out["count"] = n
        out["field_20"] = struct.unpack_from("<I", d, 0x20)[0]
        out["field_24"] = struct.unpack_from("<HH", d, 0x24)
        out["floats_28_33"] = list(struct.unpack_from("<3f", d, 0x28))
        recs = []
        for i in range(n):
            off = SFX_HEADER_SIZE + SFX_RECORD_SIZE * i
            if off + SFX_RECORD_SIZE > len(d):
                out.setdefault("warnings", []).append("record %d truncated" % i)
                break
            recs.append({
                "index": i,
                "sound_id": struct.unpack_from("<I", d, off)[0],
                "field1": struct.unpack_from("<I", d, off + 4)[0],
                "field2": struct.unpack_from("<I", d, off + 8)[0],
                "field3": struct.unpack_from("<I", d, off + 12)[0],
                "field4": struct.unpack_from("<I", d, off + 16)[0],
            })
        out["records"] = recs
        out["detail_block_offset"] = SFX_HEADER_SIZE + SFX_RECORD_SIZE * n
        out["detail_block_bytes"] = len(d) - out["detail_block_offset"]
    if args.json:
        print(json.dumps(out, indent=2))
        return
    print("MAP: %s (%s)" % (out["path"], out["kind"]))
    print("size            : %d bytes" % out["size"])
    print("guid            : %s" % out["guid"])
    print("count           : %d" % out.get("count", -1))
    if kind == "BANK":
        print("bank names      : %s" % out.get("bank_names"))
        print("record f0/f1/f2 : %s" % (out["records"][0] if out["records"] else None))
        print("ends at EOF     : %s" % out.get("ends_exactly_at_eof"))
    else:
        print("floats @0x28    : %s" % out["floats_28_33"])
        print("detail block    : offset %d, %d bytes (not decoded)" %
              (out["detail_block_offset"], out["detail_block_bytes"]))
        print("%-4s %10s %10s %10s %10s %10s" %
              ("#", "sound_id", "field1", "field2", "field3", "field4"))
        for r in out["records"][:40]:
            print("%-4d %10d %10d %10d %10d %10d" %
                  (r["index"], r["sound_id"], r["field1"], r["field2"],
                   r["field3"], r["field4"]))
    for w in out.get("warnings", []):
        print("WARNING: %s" % w)


def cmd_scan(args):
    paths = []
    for p in args.paths:
        if os.path.isdir(p):
            paths.extend(sorted(glob.glob(os.path.join(p, "**", "*.sdt"), recursive=True)))
        elif os.path.isfile(p):
            paths.append(p)
        else:
            paths.extend(sorted(glob.glob(p)))
    results = []
    for p in paths:
        try:
            results.append(SdtFile(p).summary())
        except (OSError, SdtError) as exc:
            results.append({"path": p, "error": str(exc)})
    if args.json:
        print(json.dumps(results, indent=2))
        return
    print("%-32s %10s %7s %8s %s" % ("file", "bytes", "samples", "codecs", "rates"))
    for r in results:
        if "error" in r:
            print("%-32s ERROR: %s" % (os.path.basename(r["path"]), r["error"]))
        else:
            print("%-32s %10d %7d %8s %s" %
                  (os.path.basename(r["path"]), r["file_size"], r["count"],
                   ",".join(sorted(r["codecs"])), r["sample_rates"]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info", help="summary and MPEG frame probe")
    p.add_argument("file"); p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("list", help="list all samples")
    p.add_argument("file"); p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("extract", help="write sample payloads verbatim")
    p.add_argument("file"); p.add_argument("outdir")
    p.add_argument("--index", type=int)
    p.add_argument("--name", help="fnmatch pattern")
    p.add_argument("--wav", action="store_true", help="wrap PCM16 samples in a WAV header")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_extract)

    p = sub.add_parser("map", help="parse a BANK or SFX .map file")
    p.add_argument("file"); p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_map)

    p = sub.add_parser("scan", help="summarise SDT files")
    p.add_argument("paths", nargs="+")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_scan)

    args = ap.parse_args(argv)
    try:
        args.func(args)
    except (OSError, SdtError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
