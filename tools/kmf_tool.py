#!/usr/bin/env python3
"""KMF (Dungeon Keeper 2 mesh) reader and cataloguer, stdlib only.

The layout implemented here comes from two independent references, both read in
full before writing this file:

  * ``research/dk2-tools/Formats/Model/kmf_struct.bt`` (010 Editor template,
    author ufdada, "Original Code by Hoenir"), lines 1-331;
  * ``research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/kmf/
    KmfFile.java`` (GPLv3, Toni Helenius), lines 1-650.

All numbers are LITTLE ENDIAN (KmfFile.java:33, BufferedResourceReader.java:67).
Structures are packed: there is no alignment padding between strings and the
following numeric fields (verified against real decompressed .kmf bytes).
Every chunk's declared ``size`` includes its 4-byte signature and 4-byte size
field (kmf_struct.bt:226, :290; confirmed on MAT2/HEAD/MATL/ANIM).

No file outside ``port/`` is read or written by this tool unless the caller
explicitly passes a path on the command line.

Usage::

    python3 kmf_tool.py info <file.kmf> [--json]
    python3 kmf_tool.py chunks <file.kmf>
    python3 kmf_tool.py validate <dir|file> [--json]
    python3 kmf_tool.py catalog <dir> --out <json>
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Constants recovered from the two references
# ---------------------------------------------------------------------------

KMSH_MAGIC = b"KMSH"
KNOWN_TAGS = (
    b"HEAD", b"MATL", b"MAT2", b"MESH", b"ANIM", b"GROP", b"ELEM",
    b"CTRL", b"SPRS", b"SPHD", b"POLY", b"VERT", b"GEOM", b"VGEO",
    b"ITAB",
)
TYPE_NAMES = {1: "MESH", 2: "ANIM", 3: "GROP"}
MATERIAL_FLAGS = {
    0x0001: "HAS_ALPHA",
    0x0002: "DOUBLE_SIDED",
    0x0004: "ALPHA_ADDITIVE",
    0x0008: "UNKNOWN8 / TRANSLUCENT",
    0x0010: "UNKNOWN10",
    0x0020: "UNKNOWN20",
    0x0040: "IS_SHININESS_SET / HAS_SPECULAR",
    0x0080: "IS_BRIGHTNESS_SET / HAS_EMISSIVE",
    0x0100: "INVISIBLE",
}
FRAME_FACTOR_NAMES = {0: "CLAMP", 1: "WRAP"}

MAX_STRING = 4096
MAX_COUNT = 4_000_000

# Minimum bytes consumed by one item, used to reject impossible counts before
# allocating/looping (defensive parsing of untrusted files).
MIN_ITEM = {
    "material": 4 + 4 + 1 + 4 + 1 + 4 + 4 + 4 + 1,
    "sprite": 4 + 4 + 4 + 4 + 4,
    "vertex": 12,
    "triangle": 3,
    "anim_geom": 5,
    "control": 4,
    "element": 4 + 4 + 1 + 12,
    "itab": 4,
    "vgeo": 1,
}


class KmfError(Exception):
    """Raised when the bytes cannot be interpreted as a valid KMF file."""


# ---------------------------------------------------------------------------
# Small containers
# ---------------------------------------------------------------------------


@dataclass
class Chunk:
    offset: int
    tag: str
    size: int
    depth: int
    end: int
    size_ok: bool

    def as_dict(self) -> Dict[str, Any]:
        return {
            "offset": self.offset,
            "tag": self.tag,
            "size": self.size,
            "depth": self.depth,
            "end": self.end,
            "size_ok": self.size_ok,
        }


@dataclass
class Reader:
    data: bytes
    pos: int = 0

    @property
    def remaining(self) -> int:
        return len(self.data) - self.pos

    def need(self, n: int, what: str) -> None:
        if n < 0 or self.pos + n > len(self.data):
            raise KmfError(
                f"{what}: need {n} byte(s) at offset {self.pos}, "
                f"only {self.remaining} left (file size {len(self.data)})"
            )

    def count(self, n: int, what: str, min_item_bytes: int) -> int:
        if n > MAX_COUNT or n * min_item_bytes > self.remaining:
            raise KmfError(
                f"{what}: implausible count {n} at offset {self.pos} "
                f"({self.remaining} bytes left)"
            )
        return n

    def u8(self, what: str = "u8") -> int:
        self.need(1, what)
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u16(self, what: str = "u16") -> int:
        self.need(2, what)
        v = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def i16(self, what: str = "i16") -> int:
        self.need(2, what)
        v = struct.unpack_from("<h", self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self, what: str = "u32") -> int:
        self.need(4, what)
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def f32(self, what: str = "f32") -> float:
        self.need(4, what)
        v = struct.unpack_from("<f", self.data, self.pos)[0]
        self.pos += 4
        return v

    def vec3f(self, what: str = "Vec3f") -> Tuple[float, float, float]:
        return (self.f32(what + ".x"), self.f32(what + ".y"), self.f32(what + ".z"))

    def raw(self, n: int, what: str) -> bytes:
        self.need(n, what)
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def cstring(self, what: str = "string") -> str:
        start = self.pos
        end = self.data.find(b"\0", start, min(len(self.data), start + MAX_STRING))
        if end < 0:
            raise KmfError(
                f"{what}: unterminated string at offset {start} "
                f"(searched {min(MAX_STRING, self.remaining)} bytes)"
            )
        self.pos = end + 1
        return self.data[start:end].decode("cp1252", errors="replace")

    def magic(self, expected: bytes, what: str = "signature") -> None:
        got = self.raw(4, what)
        if got != expected:
            raise KmfError(
                f"{what}: expected {expected.decode('ascii')!r}, "
                f"got {got!r} at offset {self.pos - 4}"
            )


# ---------------------------------------------------------------------------
# Model representation
# ---------------------------------------------------------------------------


@dataclass
class KmfModel:
    path: str
    file_size: int
    version: int
    head_unknown: int
    model_type: str
    head: Dict[str, Any] = field(default_factory=dict)
    materials: List[Dict[str, Any]] = field(default_factory=list)
    sprites: List[Dict[str, Any]] = field(default_factory=list)
    geometry: Dict[str, Any] = field(default_factory=dict)
    controls: List[Dict[str, Any]] = field(default_factory=list)
    grops: List[Dict[str, Any]] = field(default_factory=list)
    chunks: List[Chunk] = field(default_factory=list)
    issues: List[str] = field(default_factory=list)
    trailing_bytes: int = 0

    @property
    def texture_names(self) -> List[str]:
        return [t for m in self.materials for t in m["textures"]]

    @property
    def total_triangles(self) -> int:
        return sum(sum(s["triangle_counts"]) for s in self.sprites)

    @property
    def lod0_triangles(self) -> int:
        return sum(s["triangle_counts"][0] for s in self.sprites
                   if s["triangle_counts"])

    @property
    def total_vertices(self) -> int:
        # MESH: unique positions in GEOM. ANIM: packed animated GEOM samples +
        # per-sprite extended vertices. Kept separate in the JSON as well.
        g = self.geometry
        if self.model_type == "MESH":
            return g.get("geom_count", 0)
        if self.model_type == "ANIM":
            return g.get("anim_geom_count", 0) + sum(
                s.get("vertex_count", 0) for s in self.sprites)
        return 0

    def summary(self) -> Dict[str, Any]:
        return {
            "file": os.path.basename(self.path),
            "file_size": self.file_size,
            "type": self.model_type,
            "version": self.version,
            "name": self.head.get("name"),
            "sprs_count": self.head.get("sprs_count", 0),
            "lod_count": self.head.get("lod_count", 0),
            "materials": len(self.materials),
            "material_references": len(self.materials),
            "texture_references": len(self.texture_names),
            "unique_textures": len(set(self.texture_names)),
            "sprites": len(self.sprites),
            "vertices": self.total_vertices,
            "geom_vertices": self.geometry.get("geom_count", 0),
            "anim_geom_samples": self.geometry.get("anim_geom_count", 0),
            "extended_vertices": sum(s.get("vertex_count", 0)
                                     for s in self.sprites),
            "triangles_all_lods": self.total_triangles,
            "triangles_lod0": self.lod0_triangles,
            "elements": len(self.grops),
            "chunks": len(self.chunks),
            "issues": self.issues,
            "trailing_bytes": self.trailing_bytes,
        }


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------


class KmfParser:
    """Structural, bounds-checked KMF parser."""

    def __init__(self, data: bytes, path: str = "<memory>") -> None:
        self.r = Reader(data)
        self.path = path
        self.model: Optional[KmfModel] = None

    def parse(self) -> KmfModel:
        r = self.r
        r.magic(KMSH_MAGIC, "KMSH")
        kmsh_size = r.u32("KMSH.size")
        version = r.u32("KMSH.version")
        model = KmfModel(path=self.path, file_size=len(r.data),
                         version=version, head_unknown=0,
                         model_type="UNKNOWN")
        self.model = model
        self._record(0, b"KMSH", kmsh_size, 0)
        if kmsh_size != len(r.data):
            model.issues.append(
                f"KMSH.size={kmsh_size} vs file size {len(r.data)}")

        r.magic(b"HEAD", "KMSH.HEAD")
        head_size = r.u32("HEAD.size")
        start = r.pos - 8
        self._record(start, b"HEAD", head_size, 1)
        fmt = r.u32("HEAD.format")
        if fmt not in TYPE_NAMES:
            raise KmfError(f"HEAD.format: unknown type {fmt} (expected 1..3)")
        model.model_type = TYPE_NAMES[fmt]
        model.head_unknown = r.u32("HEAD.unknown")
        if head_size < 16:
            model.issues.append(f"HEAD.size={head_size} < 16")

        if fmt != 3:  # GROP has no MATL (kmf_struct.bt:122-124, KmfFile.java:138)
            self._parse_matl()

        tag = r.raw(4, "model chunk signature")
        if tag == b"MESH":
            if fmt != 1:
                model.issues.append("MESH chunk in non-MESH file")
            self._parse_mesh()
        elif tag == b"ANIM":
            if fmt != 2:
                model.issues.append("ANIM chunk in non-ANIM file")
            self._parse_anim()
        elif tag == b"GROP":
            if fmt != 3:
                model.issues.append("GROP chunk in non-GROP file")
            self._parse_grop()
        else:
            raise KmfError(f"unknown model chunk {tag!r} at offset {r.pos - 4}")

        model.trailing_bytes = r.remaining
        if r.remaining:
            model.issues.append(
                f"{r.remaining} unparsed trailing byte(s) at offset {r.pos}")
        return model

    def _record(self, offset: int, tag: bytes, size: int,
                depth: int) -> Chunk:
        assert self.model is not None
        ok = size >= 8 and offset + size <= len(self.r.data)
        c = Chunk(offset, tag.decode("ascii", errors="replace"), size,
                  depth, offset + size, ok)
        self.model.chunks.append(c)
        if not ok:
            self.model.issues.append(
                f"{c.tag} at {offset}: declared size {size} is invalid "
                f"(end {offset + size} > file size {len(self.r.data)})")
        return c

    def _chunk(self, expected: bytes, depth: int) -> Chunk:
        start = self.r.pos
        self.r.magic(expected, expected.decode("ascii") + " signature")
        size = self.r.u32(expected.decode("ascii") + ".size")
        return self._record(start, expected, size, depth)

    def _flag_names(self, flags: int) -> List[str]:
        return [name for bit, name in MATERIAL_FLAGS.items() if flags & bit]

    # -- MATL / MAT2 -------------------------------------------------------

    def _parse_matl(self) -> None:
        assert self.model is not None
        r = self.r
        self._chunk(b"MATL", 1)
        count = r.count(r.u32("MATL.numMaterials"), "MATL.numMaterials", 1)
        self.model.head["material_count"] = count
        for i in range(count):
            self._chunk(b"MAT2", 2)
            name = r.cstring(f"MAT2[{i}].name")
            tex_count = r.count(r.u32(f"MAT2[{i}].numTextures"),
                                f"MAT2[{i}].numTextures", 1)
            textures = [r.cstring(f"MAT2[{i}].texture[{j}]")
                        for j in range(tex_count)]
            flags = r.u32(f"MAT2[{i}].flags")
            brightness = r.f32(f"MAT2[{i}].brightness")
            gamma = r.f32(f"MAT2[{i}].gamma/shininess")
            envmap = r.cstring(f"MAT2[{i}].envmap")
            self.model.materials.append({
                "name": name,
                "textures": textures,
                "flags": flags,
                "flag_names": self._flag_names(flags),
                "brightness": brightness,
                "gamma": gamma,
                "envmap": envmap,
            })

    # -- CTRL --------------------------------------------------------------

    def _parse_ctrl(self, depth: int, anim: bool) -> Dict[str, Any]:
        r = self.r
        self._chunk(b"CTRL", depth)
        count = r.count(r.u32("CTRL.numCTRL"), "CTRL.numCTRL",
                        MIN_ITEM["control"])
        sample: List[Dict[str, int]] = []
        for i in range(count):
            if anim:
                a, b, c = r.u16(), r.u16(), r.u32()  # KmfFile.java:495-497
            else:
                a, b, c = r.u32(), r.u32(), 0        # KmfFile.java:276-277
            if i < 8:
                sample.append({"a": a, "b": b, "c": c})
        return {"count": count, "sample": sample}

    # -- shared SPHD / sprite headers -------------------------------------

    def _parse_sphd_headers(self, sprs_count: int, lod_count: int,
                            depth: int) -> List[Dict[str, Any]]:
        r = self.r
        headers: List[Dict[str, Any]] = []
        for i in range(sprs_count):
            self._chunk(b"SPHD", depth)
            tris = [r.u32(f"SPHD[{i}].lod[{j}]") for j in range(lod_count)]
            verts = r.count(r.u32(f"SPHD[{i}].numVertsEx"),
                            f"SPHD[{i}].numVertsEx", MIN_ITEM["vertex"])
            mm = r.f32(f"SPHD[{i}].mmFactor")
            headers.append({
                "triangle_counts": tris,
                "vertex_count": verts,
                "mm_factor": mm,
            })
        return headers

    @staticmethod
    def _triangles(r: Reader, count: int, what: str) -> List[List[int]]:
        r.count(count, what, MIN_ITEM["triangle"])
        data = r.raw(count * 3, what)
        return [[data[i], data[i + 1], data[i + 2]]
                for i in range(0, len(data), 3)]

    # -- MESH --------------------------------------------------------------

    def _parse_mesh(self) -> None:
        assert self.model is not None
        r = self.r
        model = self.model
        c = self._record(r.pos - 4, b"MESH",
                         struct.unpack_from("<I", r.data, r.pos)[0], 1)
        r.pos += 4  # size already read by _record (no signature re-read)

        r.magic(b"HEAD", "MESH.HEAD")
        hs = r.u32("MESH.HEAD.size")
        self._record(r.pos - 8, b"HEAD", hs, 2)
        name = r.cstring("MESH.HEAD.meshname")
        sprs = r.count(r.u32("MESH.HEAD.sprsCount"), "sprsCount", 1)
        verts = r.count(r.u32("MESH.HEAD.vertsCount"), "vertsCount", 12)
        pos = r.vec3f("MESH.HEAD.pos")
        scale = r.f32("MESH.HEAD.scale")
        lod = r.count(r.u32("MESH.HEAD.lodCount"), "lodCount", 1)
        model.head.update({
            "name": name, "sprs_count": sprs, "verts_count": verts,
            "pos": pos, "scale": scale, "lod_count": lod,
        })

        model.controls.append(self._parse_ctrl(2, anim=False))
        self._chunk(b"SPRS", 2)
        headers = self._parse_sphd_headers(sprs, lod, 3)
        self._parse_mesh_sprs(headers, lod)
        self._parse_mesh_geom(verts)

    def _parse_mesh_sprs(self, headers: List[Dict[str, Any]],
                         lod_count: int) -> None:
        assert self.model is not None
        r = self.r
        for i, h in enumerate(headers):
            self._chunk(b"SPRS", 3)
            mat_index = r.u32(f"SPRS[{i}].index")
            tris: List[List[List[int]]] = []
            for j in range(lod_count):
                tris.append(self._triangles(
                    r, h["triangle_counts"][j], f"SPRS[{i}].lod[{j}]"))
            vcount = h["vertex_count"]
            r.count(vcount, f"SPRS[{i}].verts", 18)
            v_samples: List[Dict[str, Any]] = []
            for k in range(vcount):
                geom_index = r.i16()          # template: short (signed)
                uv = (r.i16(), r.i16())       # template: short; Java: unsigned
                normal = r.vec3f("MeshVertEx.normal")
                if k < 5:
                    v_samples.append({
                        "geom_index": geom_index,
                        "uv_signed": list(uv),
                        "uv_unsigned": [uv[0] & 0xFFFF, uv[1] & 0xFFFF],
                        "normal": list(normal),
                    })
            self.model.sprites.append({
                "index": i,
                "material_index": mat_index,
                "triangle_counts": list(h["triangle_counts"]),
                "vertex_count": vcount,
                "mm_factor": h["mm_factor"],
                "triangles_sample": [lod[:4] for lod in tris],
                "vertices_sample": v_samples,
            })

    def _parse_mesh_geom(self, verts: int) -> None:
        assert self.model is not None
        r = self.r
        self._chunk(b"GEOM", 2)
        r.count(verts, "GEOM.verts", MIN_ITEM["vertex"])
        lo = [float("inf")] * 3
        hi = [float("-inf")] * 3
        sample: List[List[float]] = []
        for i in range(verts):
            v = r.vec3f("GEOM.vert")
            for k in range(3):
                lo[k] = min(lo[k], v[k])
                hi[k] = max(hi[k], v[k])
            if i < 5:
                sample.append(list(v))
        self.model.geometry = {
            "geom_count": verts,
            "bbox_min": None if verts == 0 else lo,
            "bbox_max": None if verts == 0 else hi,
            "sample": sample,
        }

    # -- ANIM --------------------------------------------------------------

    def _parse_anim(self) -> None:
        assert self.model is not None
        r = self.r
        model = self.model
        self._record(r.pos - 4, b"ANIM",
                     struct.unpack_from("<I", r.data, r.pos)[0], 1)
        r.pos += 4

        r.magic(b"HEAD", "ANIM.HEAD")
        hs = r.u32("ANIM.HEAD.size")
        self._record(r.pos - 8, b"HEAD", hs, 2)
        name = r.cstring("ANIM.HEAD.meshname")
        sprs = r.count(r.u32("ANIM.HEAD.sprsCount"), "sprsCount", 1)
        frames = r.count(r.u32("ANIM.HEAD.frameCount"), "frameCount", 1)
        indexes = r.count(r.u32("ANIM.HEAD.indexCount"), "indexCount", 1)
        geoms = r.count(r.u32("ANIM.HEAD.geomCount"), "geomCount", 1)
        fff = r.u32("ANIM.HEAD.frameFactorFunction")
        pos = r.vec3f("ANIM.HEAD.pos")
        cube_scale = r.f32("ANIM.HEAD.cubeScale")
        scale = r.f32("ANIM.HEAD.scale")
        lod = r.count(r.u32("ANIM.HEAD.lodCount"), "lodCount", 1)
        model.head.update({
            "name": name, "sprs_count": sprs, "frame_count": frames,
            "index_count": indexes, "geom_count": geoms,
            "frame_factor_function": fff,
            "frame_factor_name": FRAME_FACTOR_NAMES.get(fff, "UNKNOWN"),
            "pos": pos, "cube_scale": cube_scale, "scale": scale,
            "lod_count": lod,
        })
        if fff not in FRAME_FACTOR_NAMES:
            model.issues.append(f"unknown frameFactorFunction {fff}")

        model.controls.append(self._parse_ctrl(2, anim=True))
        self._chunk(b"SPRS", 2)
        headers = self._parse_sphd_headers(sprs, lod, 3)
        self._parse_anim_sprs(headers, lod)
        self._parse_itab(frames, indexes)
        self._parse_anim_geom(geoms, scale)
        self._parse_vgeo(indexes, frames)

    def _parse_anim_sprs(self, headers: List[Dict[str, Any]],
                         lod_count: int) -> None:
        assert self.model is not None
        r = self.r
        for i, h in enumerate(headers):
            self._chunk(b"SPRS", 3)
            mat_index = r.u32(f"SPRS[{i}].index")
            self._chunk(b"POLY", 4)
            tris: List[List[List[int]]] = []
            for j in range(lod_count):
                tris.append(self._triangles(
                    r, h["triangle_counts"][j], f"SPRS[{i}].POLY.lod[{j}]"))
            self._chunk(b"VERT", 4)
            vcount = h["vertex_count"]
            r.count(vcount, f"SPRS[{i}].verts", 18)
            v_samples: List[Dict[str, Any]] = []
            for k in range(vcount):
                uv = (r.u16(), r.u16())           # Java: unsigned shorts
                normal = r.vec3f("AnimVertEx.normal")
                itab_index = r.u16()
                if k < 5:
                    v_samples.append({
                        "uv": list(uv), "normal": list(normal),
                        "itab_index": itab_index,
                    })
            self.model.sprites.append({
                "index": i,
                "material_index": mat_index,
                "triangle_counts": list(h["triangle_counts"]),
                "vertex_count": vcount,
                "mm_factor": h["mm_factor"],
                "triangles_sample": [lod[:4] for lod in tris],
                "vertices_sample": v_samples,
            })

    def _parse_itab(self, frames: int, indexes: int) -> None:
        assert self.model is not None
        r = self.r
        self._chunk(b"ITAB", 2)
        # KmfFile.java:426 / kmf_struct.bt:198:
        #   chunks = floor((frameCount - 1) / 128.0 + 1)
        chunks = (frames - 1) // 128 + 1 if frames >= 1 else 0
        total = r.count(chunks * indexes, "ITAB entries", MIN_ITEM["itab"])
        sample = [r.u32() for _ in range(min(8, total))]
        r.pos += (total - len(sample)) * 4
        self.model.geometry.update({
            "itab_chunks": chunks, "itab_entries": total,
            "itab_sample": sample,
        })

    def _parse_anim_geom(self, geoms: int, scale: float) -> None:
        assert self.model is not None
        r = self.r
        self._chunk(b"GEOM", 2)
        r.count(geoms, "ANIM.GEOM entries", MIN_ITEM["anim_geom"])
        sample: List[Dict[str, Any]] = []
        frames: List[int] = []
        for i in range(geoms):
            packed = r.u32("AnimGeom.coordinates")
            frame_base = r.u8("AnimGeom.frameBase")
            # 10 bits per coordinate, bias 0x200, divide by 511.
            # Template getAnimGeom() (bt:316-324) uses (v-0x200)/511.0f.
            # Java (KmfFile.java:447-449) uses (v%1024 - 512)/511.0f,
            # then multiplies by the ANIM HEAD scale (Java:452).
            raw = [((packed >> 20) & 0x3FF) - 0x200,
                   ((packed >> 10) & 0x3FF) - 0x200,
                   (packed & 0x3FF) - 0x200]
            xyz = [v / 511.0 for v in raw]
            if i < 5:
                sample.append({
                    "packed": packed, "raw10": raw,
                    "xyz": xyz, "xyz_scaled": [v * scale for v in xyz],
                    "frame_base": frame_base,
                })
            if len(frames) < 5:
                frames.append(frame_base)
        self.model.geometry.update({
            "anim_geom_count": geoms, "anim_geom_sample": sample,
            "frame_base_sample": frames,
        })

    def _parse_vgeo(self, indexes: int, frames: int) -> None:
        assert self.model is not None
        r = self.r
        self._chunk(b"VGEO", 2)
        total = r.count(indexes * frames, "VGEO entries", MIN_ITEM["vgeo"])
        data = r.raw(min(total, 16), "VGEO sample")
        r.pos += total - len(data)
        self.model.geometry.update({
            "vgeo_entries": total, "vgeo_sample": list(data),
        })

    # -- GROP --------------------------------------------------------------

    def _parse_grop(self) -> None:
        assert self.model is not None
        r = self.r
        model = self.model
        self._record(r.pos - 4, b"GROP",
                     struct.unpack_from("<I", r.data, r.pos)[0], 1)
        r.pos += 4
        r.magic(b"HEAD", "GROP.HEAD")
        hs = r.u32("GROP.HEAD.size")
        self._record(r.pos - 8, b"HEAD", hs, 2)
        count = r.count(r.u32("GROP.HEAD.elementCount"), "elementCount",
                        MIN_ITEM["element"])
        model.head["element_count"] = count
        for i in range(count):
            self._chunk(b"ELEM", 2)
            name = r.cstring(f"ELEM[{i}].name")
            pos = r.vec3f(f"ELEM[{i}].pos")
            model.grops.append({"name": name, "pos": list(pos)})


def parse_kmf(data: bytes, path: str = "<memory>") -> KmfModel:
    return KmfParser(data, path).parse()


def parse_file(path: str) -> KmfModel:
    with open(path, "rb") as fh:
        return parse_kmf(fh.read(), path)


# ---------------------------------------------------------------------------
# Generic chunk scan (independent of the structural parser)
# ---------------------------------------------------------------------------


def scan_chunks(data: bytes) -> List[Dict[str, Any]]:
    """Find plausible 4CC + u32-size chunk headers anywhere in *data*.

    This is a heuristic cross-check for `chunks`; a match needs a known tag and
    a size of at least 8 that stays inside the file.  It is not a replacement
    for the structural parser (false positives in vertex data are possible).
    """
    found: List[Dict[str, Any]] = []
    for i in range(0, len(data) - 8):
        tag = data[i:i + 4]
        if tag not in KNOWN_TAGS:
            continue
        size = struct.unpack_from("<I", data, i + 4)[0]
        if 8 <= size <= len(data) - i:
            found.append({"offset": i, "tag": tag.decode("ascii"),
                          "size": size, "end": i + size})
    return found


# ---------------------------------------------------------------------------
# Catalog
# ---------------------------------------------------------------------------


def catalog(directory: str) -> Dict[str, Any]:
    files = sorted(
        os.path.join(directory, n)
        for n in os.listdir(directory)
        if n.lower().endswith(".kmf")
    )
    totals = {
        "materials": 0,
        "material_references": 0,
        "texture_references": 0,
        "sprites": 0,
        "vertices": 0,
        "geom_vertices": 0,
        "anim_geom_samples": 0,
        "extended_vertices": 0,
        "triangles_all_lods": 0,
        "triangles_lod0": 0,
        "elements": 0,
        "chunks": 0,
    }
    all_textures = set()
    type_dist: Dict[str, int] = {k: 0 for k in TYPE_NAMES.values()}
    version_dist: Dict[str, int] = {}
    issue_counts: Dict[str, int] = {}
    per_file: List[Dict[str, Any]] = []
    errors: List[Dict[str, str]] = []
    ok = 0

    for path in files:
        try:
            m = parse_file(path)
        except Exception as exc:  # noqa: BLE001 - catalog must survive bad files
            errors.append({"file": os.path.basename(path),
                           "error": f"{type(exc).__name__}: {exc}"})
            continue
        ok += 1
        s = m.summary()
        per_file.append(s)
        type_dist[m.model_type] = type_dist.get(m.model_type, 0) + 1
        version_dist[str(m.version)] = version_dist.get(str(m.version), 0) + 1
        for key in totals:
            totals[key] += s[key]
        all_textures.update(m.texture_names)
        for issue in m.issues:
            kind = issue.split(":", 1)[0]
            issue_counts[kind] = issue_counts.get(kind, 0) + 1

    by_size = sorted(per_file, key=lambda s: (-s["file_size"], s["file"]))
    by_verts = sorted(per_file, key=lambda s: (-s["vertices"], s["file"]))
    return {
        "tool": "tools/kmf_tool.py catalog",
        "source_directory": os.path.abspath(directory),
        "file_count": len(files),
        "parse_ok": ok,
        "parse_failed": len(errors),
        "files_with_issues": sum(1 for s in per_file if s["issues"]),
        "total_issue_count": sum(len(s["issues"]) for s in per_file),
        "issue_counts_by_kind": issue_counts,
        "type_distribution": type_dist,
        "version_distribution": version_dist,
        "totals": totals,
        "unique_texture_names": len(all_textures),
        "texture_name_sample": sorted(all_textures)[:40],
        "largest_by_file_size": by_size[:10],
        "largest_by_vertex_count": by_verts[:10],
        "errors": errors,
        "files": per_file,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _fmt(v: Any) -> str:
    if isinstance(v, float):
        return f"{v:.6g}"
    if isinstance(v, list):
        return "[" + ", ".join(_fmt(x) for x in v) + "]"
    return str(v)


def cmd_info(args: argparse.Namespace) -> int:
    m = parse_file(args.file)
    s = m.summary()
    if args.json:
        out = dict(s)
        out["head"] = m.head
        out["materials"] = m.materials
        out["controls"] = m.controls
        out["geometry"] = m.geometry
        out["sprites"] = m.sprites
        out["grops"] = m.grops
        print(json.dumps(out, indent=2, sort_keys=True))
        return 0
    print(f"file        : {m.path}")
    print(f"size        : {m.file_size} bytes")
    print(f"magic/version: KMSH / {m.version}")
    print(f"type        : {m.model_type} (HEAD.format="
          f"{next(k for k, v in TYPE_NAMES.items() if v == m.model_type)})")
    print("head        :")
    for k, v in m.head.items():
        print(f"  {k:24s} {_fmt(v)}")
    print(f"controls    : {[c['count'] for c in m.controls]}")
    print(f"materials   : {len(m.materials)}")
    for mat in m.materials[:20]:
        flags = ",".join(mat["flag_names"]) or "-"
        print(f"  {mat['name']!r}: textures={mat['textures']} "
              f"flags=0x{mat['flags']:08x} ({flags}) "
              f"brightness={_fmt(mat['brightness'])} "
              f"gamma={_fmt(mat['gamma'])} envmap={mat['envmap']!r}")
    if len(m.materials) > 20:
        print(f"  ... {len(m.materials) - 20} more")
    print(f"sprites     : {len(m.sprites)}")
    for sp in m.sprites[:20]:
        tris = sp["triangle_counts"]
        print(f"  [{sp['index']}] mat={sp['material_index']} "
              f"lod_tris={tris} verts={sp['vertex_count']} "
              f"mm={_fmt(sp['mm_factor'])}")
    if len(m.sprites) > 20:
        print(f"  ... {len(m.sprites) - 20} more")
    print("geometry    :")
    for k, v in m.geometry.items():
        print(f"  {k:24s} {_fmt(v)}")
    if m.grops:
        print(f"elements    : {len(m.grops)}")
        for g in m.grops[:20]:
            print(f"  {g['name']!r} pos={_fmt(g['pos'])}")
    print(f"chunks      : {len(m.chunks)}")
    print(f"trailing    : {m.trailing_bytes} byte(s)")
    print(f"issues      : {len(m.issues)}")
    for issue in m.issues[:20]:
        print(f"  ! {issue}")
    return 0


def cmd_chunks(args: argparse.Namespace) -> int:
    m = parse_file(args.file)
    print(f"{'OFFSET':>9} {'TAG':<5} {'DECLARED':>9} {'END':>9}  STATUS")
    for c in m.chunks:
        print(f"{c.offset:9d} {'  ' * c.depth + c.tag:<5} "
              f"{c.size:9d} {c.end:9d}  {'OK' if c.size_ok else 'BAD SIZE'}")
    if args.scan:
        print("\nheuristic byte-scan matches (may include false positives):")
        for c in scan_chunks(open(args.file, "rb").read()):
            print(f"{c['offset']:9d} {c['tag']:<5} {c['size']:9d} "
                  f"{c['end']:9d}")
    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    if os.path.isdir(args.path):
        paths = sorted(os.path.join(args.path, n)
                       for n in os.listdir(args.path)
                       if n.lower().endswith(".kmf"))
    else:
        paths = [args.path]
    n_ok = n_warn = 0
    rows: List[Dict[str, Any]] = []
    for path in paths:
        row: Dict[str, Any] = {"file": os.path.basename(path)}
        try:
            m = parse_file(path)
        except Exception as exc:  # noqa: BLE001
            row.update({"ok": False, "error": f"{type(exc).__name__}: {exc}"})
            rows.append(row)
            continue
        row.update({
            "ok": True,
            "type": m.model_type,
            "version": m.version,
            "size_ok": all(c.size_ok for c in m.chunks),
            "issues": m.issues,
        })
        if row["size_ok"] and not m.issues:
            n_ok += 1
        else:
            n_warn += 1
        rows.append(row)
    if args.json:
        print(json.dumps({"files": rows, "clean": n_ok,
                          "with_issues": n_warn,
                          "errors": sum(1 for r in rows if not r["ok"])},
                         indent=2))
    else:
        for r in rows:
            if r["ok"]:
                status = "OK" if r["size_ok"] and not r["issues"] else "WARN"
                extra = "; ".join(r["issues"][:3])
                print(f"{status:4s} {r['file']:<48} {r['type']:<4} "
                      f"v{r['version']} {extra}")
            else:
                print(f"FAIL {r['file']:<48} {r['error']}")
        print(f"\n{len(rows)} file(s): {n_ok} clean, {n_warn} with issues, "
              f"{sum(1 for r in rows if not r['ok'])} hard failure(s)")
    return 0 if all(r["ok"] for r in rows) else 1


def cmd_catalog(args: argparse.Namespace) -> int:
    result = catalog(args.dir)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=2, sort_keys=True, ensure_ascii=False)
        fh.write("\n")
    print(f"catalog: {result['file_count']} file(s), "
          f"{result['parse_ok']} parsed, {result['parse_failed']} failed")
    print(f"types  : {result['type_distribution']}")
    print(f"versions: {result['version_distribution']}")
    print(f"totals : {result['totals']}")
    print(f"unique texture names: {result['unique_texture_names']}")
    print(f"issues : files={result['files_with_issues']} "
          f"count={result['total_issue_count']} "
          f"{result['issue_counts_by_kind']}")
    print("largest 10 by file size:")
    for s in result["largest_by_file_size"]:
        print(f"  {s['file']:<48} {s['file_size']:>8} B  {s['type']:<4} "
              f"vtx={s['vertices']:>6} tri={s['triangles_all_lods']:>6} "
              f"mat={s['materials']:>2}")
    print(f"written: {args.out}")
    return 0 if result["parse_failed"] == 0 else 1


def main(argv: Optional[List[str]] = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    pi = sub.add_parser("info", help="show one KMF file")
    pi.add_argument("file")
    pi.add_argument("--json", action="store_true")
    pi.set_defaults(func=cmd_info)

    pc = sub.add_parser("chunks", help="list chunk headers")
    pc.add_argument("file")
    pc.add_argument("--scan", action="store_true",
                    help="also run the heuristic byte-level tag scan")
    pc.set_defaults(func=cmd_chunks)

    pv = sub.add_parser("validate", help="validate one file or a directory")
    pv.add_argument("path")
    pv.add_argument("--json", action="store_true")
    pv.set_defaults(func=cmd_validate)

    pa = sub.add_parser("catalog", help="catalog a directory of .kmf files")
    pa.add_argument("dir")
    pa.add_argument("--out", required=True)
    pa.set_defaults(func=cmd_catalog)

    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
