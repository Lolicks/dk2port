#!/usr/bin/env python3
"""KMSH decompressor for Dungeon Keeper 2 meshes.

The algorithm is ported verbatim from two shipped open-source implementations:

  * OpenKeeper ``toniarts.openkeeper.tools.convert.wad.WadFile#decompressFileData``
    (GPLv3), file ``src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java``;
  * dk2-tools 010 Editor script ``Scripts/wad_extract.1sc``, function
    ``decompressFileData`` (author ufdada, based on earlier community work).

The compressed stream starts at payload offset 5, NOT at the "KMSH" magic:
the magic and the u32 size are themselves the first literal run emitted by the
token decoder.  Output is an uncompressed KMF (IFF-style chunks: HEAD, MATL,
MESH/ANIM/GROP, ...), exactly ``declared_size`` bytes long.

Stdlib only.  Importing never reads a file.

Usage::

    python3 kmsh_decompress.py inspect <Meshes.WAD|payload.bin> [--json] [--limit N]
    python3 kmsh_decompress.py decompress <Meshes.WAD|payload.bin> <outdir> [--limit N]
    python3 kmsh_decompress.py batch <Meshes.WAD> <outdir> [--limit N]
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass
from typing import Iterator, List, Optional

MAGIC = b"KMSH"
WRAPPER_SIZE = 14  # 6-byte wrapper + "KMSH" + u32 size (envelope only)
WAD_MAGIC = b"DWFB"
WAD_HEADER_SIZE = 0x58
WAD_ENTRY_SIZE = 0x28
KNOWN_TAGS = (
    b"HEAD", b"MATL", b"MAT2", b"SPRS", b"SPHD", b"POLY", b"EOM",
    b"CTRL", b"MESH", b"ANIM", b"GEOM", b"VERT", b"VGEO", b"ITAB",
    b"GROP", b"ELEM", b"KMSH",
)


class KmshError(Exception):
    """Bad envelope or failed decompression."""


class CodecUnresolved(Exception):
    """Kept as an alias for backwards compatibility; no longer raised."""


@dataclass(frozen=True)
class Payload:
    index: int
    name: str
    variant: int
    be16_size: int
    trailer: int
    declared_size: int
    stored_size: int
    wrapper: bytes
    body: bytes  # the compressed stream, starting at offset 14

    @property
    def ratio(self) -> float:
        return self.declared_size / len(self.body) if self.body else 0.0


# --------------------------------------------------------------------------- parsing

def parse_payload(data: bytes, index: int = 0, name: str = "") -> Payload:
    """Parse one stored KMSH payload (the complete WAD entry data).

    Envelope observed in 2015/2015 Meshes.WAD entries::

        10 fb <variant:u8> <u16 BE size> <e3|e1> "KMSH" <u32 LE size> <body>

    The u16 at offset 3 is the low 16 bits of the uncompressed size in
    *big-endian* order; the u32 at offset 10 repeats the full size little-endian.
    """
    if len(data) < WRAPPER_SIZE:
        raise KmshError(f"payload {index}: too small ({len(data)} bytes)")
    if data[0] != 0x10 or data[1] != 0xFB:
        raise KmshError(f"payload {index}: bad signature {data[:2].hex()}")
    if data[6:10] != MAGIC:
        raise KmshError(f"payload {index}: missing KMSH magic")
    declared = struct.unpack_from("<I", data, 10)[0]
    be16 = struct.unpack_from(">H", data, 3)[0]
    if declared & 0xFFFF != be16:
        raise KmshError(
            f"payload {index}: size fields disagree (u16BE={be16}, u32={declared})"
        )
    if data[12] != data[2]:
        raise KmshError(f"payload {index}: variant fields disagree")
    if declared <= 0:
        raise KmshError(f"payload {index}: non-positive declared size {declared}")
    return Payload(
        index=index, name=name, variant=data[2], be16_size=be16,
        trailer=data[5], declared_size=declared, stored_size=len(data),
        wrapper=bytes(data[:WRAPPER_SIZE]), body=bytes(data[WRAPPER_SIZE:]),
    )


def iter_wad_payloads(path: str, limit: Optional[int] = None) -> Iterator[Payload]:
    """Yield KMSH payloads from a DWFB WAD, skipping non-KMSH entries."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != WAD_MAGIC:
        raise KmshError(f"{path}: not a DWFB WAD (magic {data[:4]!r})")
    count, pool_off, pool_size, _ = struct.unpack_from("<IIII", data, 0x48)
    emitted = 0
    for i in range(count):
        off = WAD_HEADER_SIZE + i * WAD_ENTRY_SIZE
        if off + WAD_ENTRY_SIZE > len(data):
            raise KmshError(f"{path}: truncated entry table at {i}")
        (_hid, name_off, name_len, data_off, data_size, flags,
         uncomp, _t1, _t2, _t3) = struct.unpack_from("<10I", data, off)
        if data_off + data_size > len(data):
            raise KmshError(f"{path}: entry {i} data out of bounds")
        raw = data[data_off:data_off + data_size]
        if raw[6:10] != MAGIC:
            continue
        if flags == 4 and uncomp and struct.unpack_from("<I", raw, 10)[0] != uncomp:
            raise KmshError(
                f"{path}: entry {i} declared size {uncomp} disagrees with KMSH header"
            )
        name = data[name_off:name_off + max(name_len - 1, 0)].decode("latin-1")
        yield parse_payload(raw, i, name)
        emitted += 1
        if limit is not None and emitted >= limit:
            return


def load_payloads(source: str, limit: Optional[int] = None) -> List[Payload]:
    with open(source, "rb") as fh:
        head = fh.read(4)
    if head == WAD_MAGIC:
        return list(iter_wad_payloads(source, limit))
    with open(source, "rb") as fh:
        return [parse_payload(fh.read(), 0, os.path.basename(source))]


# --------------------------------------------------------------------------- codec

def _ref(out: bytearray, k: int, counter: int, j: int) -> int:
    """C/Java ``do { dest[j] = dest[k++]; j++; } while (counter-- != 0);``.

    The loop runs ``counter + 1`` times because the comparison uses the
    post-decrement value.  Reproducing this detail is essential.
    """
    while True:
        if k < 0 or k >= len(out):
            raise KmshError(f"back-reference out of range: k={k}, out={len(out)}")
        out.append(out[k])
        k += 1
        j += 1
        old = counter
        counter -= 1
        if old == 0:
            break
    return j


def decompress_stream(src: bytes, declared_size: int) -> bytes:
    """Decompress a complete stored KMSH payload to its KMF bytes.

    Ported from OpenKeeper ``WadFile.decompressFileData`` (Java) and
    dk2-tools ``Scripts/wad_extract.1sc``.  ``src`` is the full WAD entry data.
    """
    if len(src) < 8:
        raise KmshError("stream too short")
    i = 0
    if src[i] & 1:
        i += 3
    i += 1            # skip the second byte of the wrapper
    i += 1            # (src[i++] already advanced once above)
    if i + 3 > len(src):
        raise KmshError("truncated 3-byte size field")
    decsize = (src[i] << 16) | (src[i + 1] << 8) | src[i + 2]
    i += 3
    if decsize != declared_size:
        raise KmshError(f"stream declares {decsize}, envelope declares {declared_size}")

    out = bytearray()
    j = 0
    finished = False
    while not finished:
        if i >= len(src):
            raise KmshError("compressed stream ended before terminal token")
        flag = src[i]
        i += 1
        if (flag & 0x80) == 0:
            if i >= len(src):
                raise KmshError("truncated token (branch 0)")
            tmp = src[i]
            i += 1
            count = flag & 3
            while count:
                if i >= len(src):
                    raise KmshError("truncated literal (branch 0)")
                out.append(src[i]); i += 1; j += 1; count -= 1
            k = j - ((flag & 0x60) << 3) - tmp - 1
            j = _ref(out, k, ((flag >> 2) & 7) + 2, j)
        elif (flag & 0x40) == 0:
            if i + 2 > len(src):
                raise KmshError("truncated token (branch 1)")
            tmp = src[i]; i += 1
            tmp2 = src[i]; i += 1
            count = tmp >> 6
            while count:
                if i >= len(src):
                    raise KmshError("truncated literal (branch 1)")
                out.append(src[i]); i += 1; j += 1; count -= 1
            k = j - ((tmp & 0x3F) << 8) - tmp2 - 1
            j = _ref(out, k, (flag & 0x3F) + 3, j)
        elif (flag & 0x20) == 0:
            if i + 3 > len(src):
                raise KmshError("truncated token (branch 2)")
            a = src[i]; i += 1
            b = src[i]; i += 1
            d = src[i]; i += 1
            count = flag & 3
            while count:
                if i >= len(src):
                    raise KmshError("truncated literal (branch 2)")
                out.append(src[i]); i += 1; j += 1; count -= 1
            k = j - ((flag & 0x10) << 12) - (a << 8) - b - 1
            j = _ref(out, k, d + ((flag & 0x0C) << 6) + 4, j)
        else:
            count = (flag & 0x1F) * 4 + 4
            if count > 0x70:
                finished = True
                count = flag & 3
            while count:
                if i >= len(src):
                    raise KmshError("truncated literal (branch 3)")
                out.append(src[i]); i += 1; j += 1; count -= 1

    if len(out) != declared_size:
        raise KmshError(f"decompressed {len(out)} bytes, expected {declared_size}")
    return bytes(out)


def decompress_body(body: bytes, declared_size: int) -> bytes:
    """Backwards-compatible wrapper taking only the stored *body*.

    The token stream actually begins 9 bytes before ``body`` (the wrapper's
    last byte plus "KMSH" and the u32 size are part of the first literal run),
    so callers should prefer :func:`decompress_payload`.
    """
    raise KmshError("use decompress_payload(); the body alone is not the stream start")


def decompress_payload(payload: Payload) -> bytes:
    return decompress_stream(payload.wrapper + payload.body, payload.declared_size)


def visible_tags(body: bytes) -> List[str]:
    found = []
    for tag in KNOWN_TAGS:
        if tag in body:
            found.append(tag.decode("ascii", "replace"))
    return found


# --------------------------------------------------------------------------- validation

def validate(payload: Payload, body: Optional[bytes] = None) -> dict:
    """Validate envelope + (optionally) a decompressed KMF candidate."""
    problems = []
    if payload.wrapper[:2] != b"\x10\xfb":
        problems.append("bad wrapper signature")
    if payload.wrapper[6:10] != MAGIC:
        problems.append("missing KMSH magic")
    if struct.unpack_from("<I", payload.wrapper, 10)[0] != payload.declared_size:
        problems.append("u32 size mismatch")
    if struct.unpack_from(">H", payload.wrapper, 3)[0] != payload.declared_size & 0xFFFF:
        problems.append("u16 BE size mismatch")
    if not visible_tags(payload.body):
        problems.append("no known chunk tags in compressed body")
    result = {
        "index": payload.index,
        "name": payload.name,
        "variant": payload.variant,
        "declared_size": payload.declared_size,
        "stored_size": payload.stored_size,
        "body_size": len(payload.body),
        "tags": visible_tags(payload.body),
        "envelope_ok": not problems,
        "problems": problems,
        "codec": "recovered",
    }
    if body is not None:
        result["candidate_size"] = len(body)
        result["candidate_size_matches"] = len(body) == payload.declared_size
        result["kmf_magic"] = body[:4] == MAGIC
    return result


# --------------------------------------------------------------------------- CLI

def safe_name(name: str, index: int) -> str:
    base = name.replace("\\", "/").split("/")[-1].strip()
    base = "".join(c if c.isalnum() or c in "._-" else "_" for c in base)
    if not base:
        base = f"mesh_{index:05d}"
    return os.path.splitext(base)[0] + ".kmf"


def emit_json(obj) -> None:
    json.dump(obj, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="command", required=True)
    for cmd, help_text in (
        ("inspect", "report envelope facts for each payload"),
        ("decompress", "decompress payloads to real .kmf files"),
        ("batch", "alias for decompress over a whole WAD"),
    ):
        p = sub.add_parser(cmd, help=help_text)
        p.add_argument("source")
        if cmd != "inspect":
            p.add_argument("outdir")
        p.add_argument("--limit", type=int, default=None)
        p.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    try:
        payloads = load_payloads(args.source, args.limit)
    except (OSError, KmshError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.command == "inspect":
        reports = [validate(p) for p in payloads]
        if args.json:
            emit_json({"source": args.source, "payloads": len(payloads),
                       "codec": "recovered", "items": reports})
        else:
            for r in reports:
                print(f"[{r['index']:5d}] {r['name'][:42]:42s} "
                      f"variant={r['variant']} declared={r['declared_size']:7d} "
                      f"stored={r['stored_size']:7d} tags={','.join(r['tags'][:6])} "
                      f"envelope={'OK' if r['envelope_ok'] else 'BAD'}")
            print(f"{len(payloads)} KMSH payload(s); codec recovered from "
                  "OpenKeeper / dk2-tools", file=sys.stderr)
        return 0

    single = (
        args.command == "decompress"
        and len(payloads) == 1
        and open(args.source, "rb").read(4) != WAD_MAGIC
    )
    results = []
    try:
        if single:
            target = args.outdir
            os.makedirs(os.path.dirname(os.path.abspath(target)), exist_ok=True)
            data = decompress_payload(payloads[0])
            with open(target, "wb") as fh:
                fh.write(data)
            results.append({"index": payloads[0].index, "name": payloads[0].name,
                            "output": target, "bytes": len(data), "status": "ok"})
        else:
            os.makedirs(args.outdir, exist_ok=True)
            used = set()
            for p in payloads:
                name = safe_name(p.name, p.index)
                key = name.lower()
                if key in used:
                    name = f"{os.path.splitext(name)[0]}__{p.index}.kmf"
                    key = name.lower()
                used.add(key)
                data = decompress_payload(p)
                with open(os.path.join(args.outdir, name), "wb") as fh:
                    fh.write(data)
                results.append({"index": p.index, "name": p.name, "output": name,
                                "bytes": len(data), "status": "ok"})
    except KmshError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 3

    if args.json:
        emit_json({"source": args.source, "outdir": args.outdir,
                   "codec": "recovered", "extracted": len(results),
                   "entries": results})
    else:
        for r in results:
            print(f"[{r['index']:5d}] {r['name']} -> {r['output']} ({r['bytes']} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
