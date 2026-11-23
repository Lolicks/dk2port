#!/usr/bin/env python3
"""TQI frame-structure probe for Dungeon Keeper 2 ``.tgq`` movies.

This is a **container/header probe, not a pixel decoder**.  It was written
after the codec was identified as Electronic Arts TQI (see
``docs/TGQ_CODEC_ANALYSIS.md``).  The game's TQI decoder uses run-time built
VLC tables, a custom IDCT and motion compensation; reproducing it faithfully
was judged out of scope for this repository, so this tool deliberately stops
at the bitstream boundary and never claims to produce an image.

What it does verify, byte for byte:

* the EA IFF-style chunk stream (delegated to ``tgq_probe.TgqFile``);
* the corrected 8-byte TQI frame header at the start of every ``pIQT``
  payload: ``u16 width, u16 height, u8 quantizer/flags, u8[3] extra``;
* that the TQI bitstream starts at payload offset 8 (confirmed by the
  decompiled game function ``FUN_005b26f0`` at ``0x005B26F0``, which calls
  ``FUN_00659580(param_1 + 8)``);
* that no raw MPEG-1 start codes occur in the frame bitstream.

Usage::

    tgq_decode.py info <file.tgq> [--json]
    tgq_decode.py headers <file.tgq> [--limit N] [--offset N] [--json]
    tgq_decode.py scan <dir-or-file> ... [--json]
    tgq_decode.py bitstream <file.tgq> <out.bin> [--frame N]
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tgq_probe import TgqFile, TAG_VIDEO_FRAME  # noqa: E402

# Corrected frame header size: the game reads 8 bytes, then starts the VLC
# bitstream at param_1 + 8 (Ghidra FUN_005b26f0 / FUN_00659580).
TQI_HEADER_SIZE = 8
MPEG_START_CODE_PREFIX = b"\x00\x00\x01"


class TqiProbeError(Exception):
    """Raised when a frame does not match the observed TQI header layout."""


def frame_header(payload):
    """Return the 8-byte TQI frame header as a dict.

    ``payload`` is the full ``pIQT`` chunk payload (header + bitstream).
    """
    if len(payload) < TQI_HEADER_SIZE:
        raise TqiProbeError("TQI frame payload shorter than 8 bytes")
    width, height = struct.unpack_from("<HH", payload, 0)
    qflags = payload[4]
    extra = payload[5:8]
    return {
        "width": width,
        "height": height,
        "quantizer_flags": qflags,
        "quantizer_flags_hex": "0x%02X" % qflags,
        "flags_bit0": qflags & 1,
        "flags_bit4": (qflags >> 4) & 1,
        "flags_bit7": (qflags >> 7) & 1,
        "extra": extra.hex(" "),
        "bitstream_offset": TQI_HEADER_SIZE,
        "bitstream_size": len(payload) - TQI_HEADER_SIZE,
        "bitstream_prefix": payload[TQI_HEADER_SIZE:TQI_HEADER_SIZE + 16].hex(" "),
    }


def video_payloads(tgq):
    """Yield ``(index, chunk, payload)`` for every ``pIQT`` frame."""
    for i, chunk in enumerate(tgq.of_tag(TAG_VIDEO_FRAME)):
        yield i, chunk, tgq.read_payload(chunk)


def count_mpeg_start_codes(payload):
    """Count ``00 00 01 xx`` occurrences in a bitstream (should be zero)."""
    body = payload[TQI_HEADER_SIZE:]
    n = 0
    pos = body.find(MPEG_START_CODE_PREFIX)
    while pos != -1:
        n += 1
        pos = body.find(MPEG_START_CODE_PREFIX, pos + 1)
    return n


def probe_file(path):
    """Probe one TGQ file without decoding any pixels."""
    tgq = TgqFile(path)
    frames = []
    sc_total = 0
    qf = collections.Counter()
    widths = collections.Counter()
    heights = collections.Counter()
    extras = collections.Counter()
    for i, chunk, payload in video_payloads(tgq):
        h = frame_header(payload)
        sc = count_mpeg_start_codes(payload)
        sc_total += sc
        qf[h["quantizer_flags"]] += 1
        widths[h["width"]] += 1
        heights[h["height"]] += 1
        extras[h["extra"]] += 1
        frames.append({
            "index": i,
            "source_offset": chunk["offset"],
            "chunk_size": chunk["size"],
            "header": h,
            "mpeg_start_codes": sc,
        })
    return {
        "path": tgq.path,
        "file_size": tgq.size,
        "walk_exact": tgq.ok,
        "chunk_count": len(tgq.chunks),
        "frame_count": len(frames),
        "widths": dict(widths),
        "heights": dict(heights),
        "quantizer_flags_histogram": dict(qf),
        "extra_field_histogram": dict(extras),
        "mpeg_start_codes_total": sc_total,
        "decoder_status": (
            "NOT_IMPLEMENTED: EA TQI VLC+IDCT+motion-compensation decoder "
            "is out of scope; bitstream is exposed verbatim only"
        ),
        "frames": frames,
    }


# ------------------------------------------------------------------- commands
def cmd_info(args):
    r = probe_file(args.file)
    if args.json:
        print(json.dumps(r, indent=2))
        return
    print("TGQ/TQI: %s" % r["path"])
    print("file size           : %d" % r["file_size"])
    print("chunk walk          : %s" % ("OK" if r["walk_exact"] else "INCOMPLETE"))
    print("chunks / frames     : %d / %d" % (r["chunk_count"], r["frame_count"]))
    print("frame dimensions    : %s" % ", ".join("%dx%d" % kv for kv in
          zip(r["widths"], r["heights"])))
    print("quantizer/flags     : %s" % ", ".join("0x%02X=%d" % kv for kv in
          r["quantizer_flags_histogram"].items()))
    print("extra bytes[5..7]   : %s" % r["extra_field_histogram"])
    print("MPEG-like triples   : %d (coincidental in VLC data; no valid start-code suffix)" %
          r["mpeg_start_codes_total"])
    print("decoder             : %s" % r["decoder_status"])
    for w in r.get("warnings", []):
        print("WARNING: %s" % w)


def cmd_headers(args):
    r = probe_file(args.file)
    start = args.offset
    end = min(len(r["frames"]), start + args.limit)
    rows = r["frames"][start:end]
    if args.json:
        print(json.dumps(rows, indent=2))
        return
    print("%-5s %-10s %-8s %-6s %-6s %-9s %s" %
          ("#", "offset", "chunk", "W", "H", "Q/flags", "bitstream[0:16]"))
    for f in rows:
        h = f["header"]
        print("%-5d %-10d %-8d %-6d %-6d %-9s %s" %
              (f["index"], f["source_offset"], f["chunk_size"],
               h["width"], h["height"], h["quantizer_flags_hex"],
               h["bitstream_prefix"]))


def cmd_scan(args):
    paths = []
    for p in args.paths:
        if os.path.isdir(p):
            import glob
            paths.extend(sorted(glob.glob(os.path.join(p, "**", "*.[Tt][Gg][Qq]"),
                                          recursive=True)))
        elif os.path.isfile(p):
            paths.append(p)
        else:
            import glob
            paths.extend(sorted(glob.glob(p)))
    results = [probe_file(p) for p in paths]
    if args.json:
        print(json.dumps(results, indent=2))
        return
    print("%-26s %10s %7s %7s %6s %8s %s" %
          ("file", "bytes", "chunks", "frames", "WxH", "qflags", "walk"))
    for r in results:
        wh = ",".join("%dx%d" % kv for kv in zip(r["widths"], r["heights"]))
        qf = ",".join("0x%02X" % k for k in r["quantizer_flags_histogram"])
        print("%-26s %10d %7d %7d %6s %8s %s" %
              (os.path.basename(r["path"]), r["file_size"], r["chunk_count"],
               r["frame_count"], wh, qf, "OK" if r["walk_exact"] else "BAD"))


def cmd_bitstream(args):
    tgq = TgqFile(args.file)
    frames = list(video_payloads(tgq))
    if args.frame >= len(frames):
        raise TqiProbeError("frame %d out of range (0..%d)" %
                            (args.frame, len(frames) - 1))
    payload = frames[args.frame][2]
    h = frame_header(payload)
    body = payload[TQI_HEADER_SIZE:]
    with open(args.out, "wb") as fh:
        fh.write(body)
    print("wrote %d raw TQI bitstream bytes from frame %d to %s" %
          (len(body), args.frame, args.out))
    print("header: W=%d H=%d q/flags=%s extra=%s" %
          (h["width"], h["height"], h["quantizer_flags_hex"], h["extra"]))
    print("NOTE: this is the compressed VLC bitstream, NOT decoded pixels.")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info", help="probe frame structure of one TGQ")
    p.add_argument("file")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("headers", help="print per-frame TQI headers")
    p.add_argument("file")
    p.add_argument("--limit", type=int, default=16)
    p.add_argument("--offset", type=int, default=0)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_headers)

    p = sub.add_parser("scan", help="probe every TGQ in given paths")
    p.add_argument("paths", nargs="+")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_scan)

    p = sub.add_parser("bitstream", help="dump raw VLC bitstream bytes")
    p.add_argument("file")
    p.add_argument("out")
    p.add_argument("--frame", type=int, default=0)
    p.set_defaults(func=cmd_bitstream)

    args = ap.parse_args(argv)
    try:
        args.func(args)
    except (OSError, TqiProbeError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
