#!/usr/bin/env python3
"""Container-level reader/extractor for Dungeon Keeper 2 ``TGQ`` movies.

Pure Python 3 standard library.  No codec is decoded: this tool only walks the
IFF-style chunk stream, reports the container layout and writes the embedded
payloads out verbatim.  See ``docs/TGQ_FORMAT.md`` for the format specification
derived from the shipped files and the explicit unknowns.

Usage::

    tgq_probe.py info <file.tgq> [--json]
    tgq_probe.py chunks <file.tgq> [--limit N] [--offset N] [--json]
    tgq_probe.py scan <dir-or-file> ... [--json]
    tgq_probe.py extract <file.tgq> <outdir> [--video] [--audio] [--all]
                      [--frames N] [--json]
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import struct
import sys

# Chunk tags observed in all 24 shipped movies.
TAG_STREAM_HEADER = b"SCHl"
TAG_VIDEO_FRAME = b"pIQT"
TAG_STREAM_CLOSE = b"SCCl"
TAG_AUDIO_BLOCK = b"SCDl"
TAG_STREAM_END = b"SCEl"

KNOWN_TAGS = {
    TAG_STREAM_HEADER: "stream header",
    TAG_VIDEO_FRAME: "video frame",
    TAG_STREAM_CLOSE: "audio/chunk count marker",
    TAG_AUDIO_BLOCK: "audio block",
    TAG_STREAM_END: "stream end marker",
}

HEADER_PAYLOAD_SIZE = 32
# The game reads 8 header bytes then starts the VLC bitstream at payload + 8
# (Ghidra FUN_005b26f0 -> FUN_00659580(param_1 + 8); OpenKeeper TgqFrame.java
# reads u16 width, u16 height, one quantizer/flags byte and skips 3 bytes).
VIDEO_HEADER_SIZE = 8
# The old 16-byte "header" was a preview that included the first 8 VLC bytes.
VIDEO_PREVIEW_SIZE = 16
MIN_CHUNK_SIZE = 8


class TgqError(Exception):
    """Raised when the file does not match the observed TGQ chunk layout."""


def _ascii4(raw):
    return all(32 <= b < 127 for b in raw)


class TgqFile:
    """Chunk walker for one TGQ file.

    Chunk layout (verified on all 24 shipped movies)::

        [4-byte ASCII tag][u32 LE total size][payload of (size - 8) bytes]

    The size INCLUDES the 8-byte chunk header.  The first chunk is always
    ``SCHl`` (40 bytes) and the last one is always ``SCEl`` (8 bytes, empty).
    """

    def __init__(self, path):
        self.path = os.fspath(path)
        self.size = os.path.getsize(self.path)
        self.chunks = []            # list of dicts: index, offset, tag, size, payload_offset, payload_size
        self.warnings = []
        self.ok = False
        with open(self.path, "rb") as fh:
            self._raw = fh  # kept only for lazy reads; never read whole 40 MB file
            self._walk()

    # ------------------------------------------------------------------ walk
    def _walk(self):
        off = 0
        idx = 0
        with open(self.path, "rb") as fh:
            while off + MIN_CHUNK_SIZE <= self.size:
                fh.seek(off)
                raw = fh.read(MIN_CHUNK_SIZE)
                if len(raw) < MIN_CHUNK_SIZE:
                    self.warnings.append("truncated chunk header at %d" % off)
                    return
                tag = raw[:4]
                size = struct.unpack_from("<I", raw, 4)[0]
                if not _ascii4(tag):
                    self.warnings.append(
                        "non-ASCII tag %r at offset %d; stopping walk" % (tag, off))
                    return
                if size < MIN_CHUNK_SIZE:
                    self.warnings.append(
                        "chunk %s at %d declares size %d (< 8); stopping walk"
                        % (tag.decode("latin1"), off, size))
                    return
                end = off + size
                if end > self.size:
                    self.warnings.append(
                        "chunk %s at %d extends past EOF (%d > %d)"
                        % (tag.decode("latin1"), off, end, self.size))
                    return
                self.chunks.append({
                    "index": idx,
                    "offset": off,
                    "tag": tag.decode("latin1"),
                    "size": size,
                    "payload_offset": off + MIN_CHUNK_SIZE,
                    "payload_size": size - MIN_CHUNK_SIZE,
                })
                idx += 1
                off = end
            if off == self.size:
                self.ok = True
            else:
                self.warnings.append(
                    "chunk walk ended at %d, file size is %d" % (off, self.size))

    # --------------------------------------------------------------- helpers
    def of_tag(self, tag):
        return [c for c in self.chunks if c["tag"] == tag.decode("latin1")]

    def read_payload(self, chunk, limit=None):
        n = chunk["payload_size"] if limit is None else min(limit, chunk["payload_size"])
        with open(self.path, "rb") as fh:
            fh.seek(chunk["payload_offset"])
            return fh.read(n)

    def stream_header(self):
        ch = self.of_tag(TAG_STREAM_HEADER)
        if not ch:
            return None
        return self.read_payload(ch[0], HEADER_PAYLOAD_SIZE)

    def video_header(self):
        ch = self.of_tag(TAG_VIDEO_FRAME)
        if not ch:
            return None
        return self.read_payload(ch[0], VIDEO_HEADER_SIZE)

    def video_preview(self):
        ch = self.of_tag(TAG_VIDEO_FRAME)
        if not ch:
            return None
        return self.read_payload(ch[0], VIDEO_PREVIEW_SIZE)

    def summary(self):
        from collections import Counter
        counts = Counter(c["tag"] for c in self.chunks)
        sccl = self.of_tag(TAG_STREAM_CLOSE)
        sccl_value = None
        if sccl and sccl[0]["payload_size"] >= 4:
            sccl_value = struct.unpack("<I", self.read_payload(sccl[0], 4))[0]
        vh = self.video_header()
        vp = self.video_preview()
        video_fields = (struct.unpack("<HHB3s", vh[:8])
                        if vh and len(vh) >= 8 else None)
        preview_fields = (struct.unpack("<8H", vp[:16])
                          if vp and len(vp) >= 16 else None)
        sh = self.stream_header()
        header_words = struct.unpack("<8I", sh[:32]) if sh and len(sh) >= 32 else None
        return {
            "path": self.path,
            "file_size": self.size,
            "walk_exact": self.ok,
            "chunk_count": len(self.chunks),
            "counts": dict(counts),
            "sccl_value": sccl_value,
            "sccl_equals_audio_chunks": (
                sccl_value == counts.get(TAG_AUDIO_BLOCK.decode("latin1"), 0)
                if sccl_value is not None else None),
            "video_header": (
                {"width": video_fields[0], "height": video_fields[1],
                 "quantizer_flags": video_fields[2],
                 "quantizer_flags_hex": "0x%02X" % video_fields[2],
                 "extra_hex": video_fields[3].hex(" ")}
                if video_fields else None),
            "video_header_u16": (list(preview_fields) if preview_fields else None),
            "stream_header_u32": list(header_words) if header_words else None,
            "warnings": self.warnings,
        }


# ------------------------------------------------------------------- commands
def cmd_info(args):
    tgq = TgqFile(args.file)
    s = tgq.summary()
    if args.json:
        print(json.dumps(s, indent=2))
        return
    print("TGQ: %s" % s["path"])
    print("file size       : %d bytes" % s["file_size"])
    print("chunk walk      : %s" % ("OK (lands exactly on EOF)" if s["walk_exact"] else "INCOMPLETE"))
    print("chunk count     : %d" % s["chunk_count"])
    print("tags            : %s" % ", ".join("%s=%d" % (k, v) for k, v in sorted(s["counts"].items())))
    sh = s["stream_header_u32"]
    if sh:
        print("SCHl u32[0..7]  : %s" % " ".join("0x%08X" % w for w in sh))
    vh = s["video_header"]
    if vh:
        print("pIQT header     : %dx%d q/flags=%s extra=%s" %
              (vh["width"], vh["height"], vh["quantizer_flags_hex"], vh["extra_hex"]))
    vp = s["video_header_u16"]
    if vp:
        print("pIQT first 16 B : %s (legacy preview; VLC starts at byte 8)" %
              " ".join(str(w) for w in vp))
    print("SCCl value      : %s (audio blocks: %s)" %
          (s["sccl_value"], s["sccl_equals_audio_chunks"]))
    for w in s["warnings"]:
        print("WARNING: %s" % w)


def cmd_chunks(args):
    tgq = TgqFile(args.file)
    start = args.offset
    end = min(len(tgq.chunks), start + args.limit)
    rows = []
    for c in tgq.chunks[start:end]:
        preview = tgq.read_payload(c, 16)
        tag = c["tag"]
        name = KNOWN_TAGS.get(tag.encode("latin1"), "unknown")
        rows.append({
            "index": c["index"],
            "offset": c["offset"],
            "tag": tag,
            "name": name,
            "declared_size": c["size"],
            "payload_offset": c["payload_offset"],
            "payload_size": c["payload_size"],
            "preview_hex": preview.hex(" "),
        })
    if args.json:
        print(json.dumps(rows, indent=2))
        return
    print("%-5s %-10s %-6s %-26s %-9s %-9s %s" %
          ("#", "offset", "tag", "name", "size", "pay_size", "payload[0:16]"))
    for r in rows:
        print("%-5d %-10d %-6s %-26s %-9d %-9d %s" %
              (r["index"], r["offset"], r["tag"], r["name"],
               r["declared_size"], r["payload_size"], r["preview_hex"]))


def cmd_scan(args):
    paths = []
    for p in args.paths:
        if os.path.isdir(p):
            paths.extend(sorted(glob.glob(os.path.join(p, "**", "*.[Tt][Gg][Qq]"), recursive=True)))
        elif os.path.isfile(p):
            paths.append(p)
        else:
            paths.extend(sorted(glob.glob(p)))
    results = [TgqFile(p).summary() for p in paths]
    if args.json:
        print(json.dumps(results, indent=2))
        return
    print("%-26s %10s %7s %7s %7s %7s %s" %
          ("file", "bytes", "chunks", "video", "audio", "SCCl", "walk"))
    for r in results:
        print("%-26s %10d %7d %7d %7d %7s %s" %
              (os.path.basename(r["path"]), r["file_size"], r["chunk_count"],
               r["counts"].get("pIQT", 0), r["counts"].get("SCDl", 0),
               r["sccl_value"], "OK" if r["walk_exact"] else "BAD"))


def cmd_extract(args):
    tgq = TgqFile(args.file)
    if not tgq.ok:
        print("WARNING: chunk walk did not reach EOF; extraction may be partial", file=sys.stderr)
    os.makedirs(args.outdir, exist_ok=True)
    want_video = args.all or args.video
    want_audio = args.all or args.audio
    manifest = {
        "source": tgq.path,
        "output_dir": args.outdir,
        "note": ("payloads are written verbatim; no codec is decoded. "
                 "Video frames retain their 8-byte TQI frame header unless "
                 "--strip-video-header is used. Audio blocks retain their leading u32 field."),
        "files": [],
    }
    vchunks = tgq.of_tag(TAG_VIDEO_FRAME)
    achunks = tgq.of_tag(TAG_AUDIO_BLOCK)
    if want_video:
        for i, c in enumerate(vchunks[:args.frames] if args.frames else vchunks):
            payload = tgq.read_payload(c)
            body = payload[VIDEO_HEADER_SIZE:] if args.strip_video_header else payload
            name = "video_%05d_%08X.bin" % (i, c["offset"])
            out = os.path.join(args.outdir, name)
            with open(out, "wb") as fh:
                fh.write(body)
            manifest["files"].append({
                "kind": "video_frame", "index": i, "source_offset": c["offset"],
                "path": out,                 "bytes": len(body),
                "header": struct.unpack("<HHB3s", payload[:8])[:3]
                          + (payload[5:8].hex(" "),),
                "legacy_preview_u16": list(struct.unpack("<8H", payload[:16])),
            })
    if want_audio:
        for i, c in enumerate(achunks):
            payload = tgq.read_payload(c)
            name = "audio_%05d_%08X.bin" % (i, c["offset"])
            out = os.path.join(args.outdir, name)
            with open(out, "wb") as fh:
                fh.write(payload)
            manifest["files"].append({
                "kind": "audio_block", "index": i, "source_offset": c["offset"],
                "path": out, "bytes": len(payload),
                "u32_field": struct.unpack("<I", payload[:4])[0] if len(payload) >= 4 else None,
            })
    sh = tgq.stream_header()
    if sh:
        out = os.path.join(args.outdir, "SCHl_payload.bin")
        with open(out, "wb") as fh:
            fh.write(sh)
        manifest["files"].append({"kind": "stream_header", "path": out, "bytes": len(sh)})
    mpath = os.path.join(args.outdir, "tgq_extract.json")
    with open(mpath, "w") as fh:
        json.dump(manifest, fh, indent=2)
    if args.json:
        print(json.dumps(manifest, indent=2))
    else:
        print("wrote %d files to %s" % (len(manifest["files"]), args.outdir))
        print("manifest: %s" % mpath)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info", help="print container summary")
    p.add_argument("file")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("chunks", help="print first N chunk headers")
    p.add_argument("file")
    p.add_argument("--limit", type=int, default=16)
    p.add_argument("--offset", type=int, default=0)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_chunks)

    p = sub.add_parser("scan", help="summarise every TGQ in given paths")
    p.add_argument("paths", nargs="+")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_scan)

    p = sub.add_parser("extract", help="write payload bytes verbatim")
    p.add_argument("file")
    p.add_argument("outdir")
    p.add_argument("--video", action="store_true")
    p.add_argument("--audio", action="store_true")
    p.add_argument("--all", action="store_true")
    p.add_argument("--frames", type=int, default=0, help="limit video frames (0 = all)")
    p.add_argument("--strip-video-header", action="store_true",
                   help="omit the 8-byte TQI frame header from extracted frames")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_extract)

    args = ap.parse_args(argv)
    try:
        args.func(args)
    except (OSError, TgqError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
