#!/usr/bin/env python3
"""Real-file tests for the KMF parser, catalog and CLI.

Run::

    python3 -m unittest tests.test_kmf -v

All assertions are against real files produced by ``tools/kmsh_decompress.py``
from ``../Data/Meshes.WAD``, never against synthetic fixtures.  The test module
skips itself with an explicit message if that corpus is not available.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest

PORT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(PORT_DIR, "tools")
sys.path.insert(0, TOOLS_DIR)

import kmf_tool  # noqa: E402

KMF_DIR = os.path.join(PORT_DIR, "build", "KMF")
CATALOG = os.path.join(PORT_DIR, "docs", "format-research", "kmf_catalog.json")
TOOL = os.path.join(TOOLS_DIR, "kmf_tool.py")
SAMPLE_ANIM = "3dfe_beams.kmf"
SAMPLE_GROP = "Group_door1.kmf"


@unittest.skipUnless(os.path.isdir(KMF_DIR), "build/KMF corpus not available")
class KmfRealFileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.names = sorted(n for n in os.listdir(KMF_DIR)
                           if n.lower().endswith(".kmf"))
        # 24 deterministic, spread-out files: at least 20 are parsed in full.
        picks = set(cls.names[:8])
        picks.update(cls.names[i] for i in range(0, len(cls.names), 97))
        picks.update(cls.names[-8:])
        picks.update({SAMPLE_ANIM, SAMPLE_GROP})
        cls.sample_names = sorted(picks)
        cls.models = {}
        cls.raw = {}
        for name in cls.sample_names:
            path = os.path.join(KMF_DIR, name)
            with open(path, "rb") as fh:
                cls.raw[name] = fh.read()
            cls.models[name] = kmf_tool.parse_file(path)

    def test_at_least_20_files_parsed(self):
        self.assertGreaterEqual(len(self.sample_names), 20)
        self.assertEqual(len(self.models), len(self.sample_names))

    def test_kmsh_magic_and_version_and_type(self):
        for name, m in self.models.items():
            raw = self.raw[name]
            self.assertEqual(raw[:4], b"KMSH", name)
            self.assertEqual(m.version, 17, name)
            self.assertIn(m.model_type, ("MESH", "ANIM", "GROP"), name)
            self.assertEqual(int.from_bytes(raw[4:8], "little"),
                             len(raw), name)

    def test_chunk_sizes_within_bounds(self):
        for name, m in self.models.items():
            boundaries = {c.offset for c in m.chunks} | {m.file_size}
            self.assertGreater(len(m.chunks), 0, name)
            for c in m.chunks:
                self.assertGreaterEqual(c.offset, 0, name)
                self.assertGreaterEqual(c.size, 8, (name, c.tag))
                self.assertLessEqual(c.end, m.file_size, (name, c.tag))
                self.assertTrue(c.size_ok, (name, c.tag, c.size))
                self.assertIn(c.end, boundaries, (name, c.tag, c.end))
            self.assertEqual(m.trailing_bytes, 0, name)
            self.assertEqual(m.issues, [], name)

    def test_header_fields_sane(self):
        for name, m in self.models.items():
            self.assertEqual(m.head_unknown, 1, name)
            if m.model_type == "MESH":
                self.assertIn("verts_count", m.head, name)
                self.assertEqual(m.geometry["geom_count"],
                                 m.head["verts_count"], name)
            elif m.model_type == "ANIM":
                for key in ("frame_count", "index_count", "geom_count",
                            "frame_factor_function", "lod_count"):
                    self.assertIn(key, m.head, (name, key))
                self.assertEqual(m.geometry["anim_geom_count"],
                                 m.head["geom_count"], name)
                self.assertEqual(
                    m.geometry["vgeo_entries"],
                    m.head["index_count"] * m.head["frame_count"], name)
            else:
                self.assertEqual(len(m.grops),
                                 m.head["element_count"], name)

    def test_material_and_texture_parsing(self):
        m = self.models[SAMPLE_ANIM]
        self.assertEqual(len(m.materials), 1)
        mat = m.materials[0]
        self.assertEqual(mat["name"], "Material #4#add#Falloff")
        self.assertEqual(mat["textures"], ["#add#Falloff"])
        self.assertEqual(mat["flags"], 0x00000006)
        self.assertEqual(mat["flag_names"],
                         ["DOUBLE_SIDED", "ALPHA_ADDITIVE"])
        self.assertEqual(mat["envmap"], "DefaultEnvMap")

    def test_anim_packed_10bit_formula(self):
        m = self.models[SAMPLE_ANIM]
        self.assertEqual(m.head["frame_factor_name"], "WRAP")
        self.assertEqual(m.head["frame_count"], 251)
        self.assertEqual(m.head["index_count"], 70)
        self.assertEqual(m.head["geom_count"], 513)
        self.assertEqual(m.geometry["itab_chunks"], 2)
        entry = m.geometry["anim_geom_sample"][0]
        packed = entry["packed"]
        self.assertEqual([((packed >> s) & 0x3FF) - 0x200
                          for s in (20, 10, 0)], entry["raw10"])
        self.assertEqual(entry["raw10"], [45, 222, -90])
        for raw, xyz, scaled in zip(entry["raw10"], entry["xyz"],
                                    entry["xyz_scaled"]):
            self.assertAlmostEqual(xyz, raw / 511.0)
            self.assertAlmostEqual(scaled, xyz * m.head["scale"])
        self.assertEqual(m.geometry["frame_base_sample"][:2], [0, 128])

    @unittest.skipUnless(os.path.isfile(CATALOG), "catalog JSON not generated")
    def test_catalog_totals(self):
        with open(CATALOG, encoding="utf-8") as fh:
            cat = json.load(fh)
        self.assertEqual(cat["file_count"], 2015)
        self.assertEqual(cat["parse_ok"], 2015)
        self.assertEqual(cat["parse_failed"], 0)
        self.assertEqual(cat["files_with_issues"], 0)
        self.assertEqual(cat["version_distribution"], {"17": 2015})
        self.assertEqual(cat["type_distribution"],
                         {"MESH": 862, "ANIM": 1145, "GROP": 8})
        totals = cat["totals"]
        self.assertEqual(totals["materials"], 6492)
        self.assertEqual(totals["texture_references"], 7263)
        self.assertEqual(totals["sprites"], 6551)
        self.assertEqual(totals["geom_vertices"], 46462)
        self.assertEqual(totals["anim_geom_samples"], 4606094)
        self.assertEqual(totals["extended_vertices"], 631995)
        self.assertEqual(totals["vertices"], 5112121)
        self.assertEqual(totals["triangles_all_lods"], 2853857)
        self.assertEqual(totals["triangles_lod0"], 339469)
        self.assertEqual(cat["unique_texture_names"], 818)
        self.assertEqual(len(cat["largest_by_file_size"]), 10)
        self.assertEqual(len(cat["largest_by_vertex_count"]), 10)
        self.assertEqual(len(cat["files"]), 2015)

    @unittest.skipUnless(os.path.isfile(CATALOG), "catalog JSON not generated")
    def test_anim_and_grop_present_and_parse(self):
        with open(CATALOG, encoding="utf-8") as fh:
            cat = json.load(fh)
        self.assertGreater(cat["type_distribution"]["ANIM"], 0,
                           "no ANIM files in corpus: nothing to assert")
        self.assertGreater(cat["type_distribution"]["GROP"], 0,
                           "no GROP files in corpus: nothing to assert")

        anim = self.models[SAMPLE_ANIM]
        self.assertEqual(anim.model_type, "ANIM")
        self.assertGreater(len(anim.sprites), 0)
        self.assertGreater(anim.sprites[0]["triangle_counts"][0], 0)
        self.assertGreater(anim.geometry["anim_geom_count"], 0)

        grop = self.models[SAMPLE_GROP]
        self.assertEqual(grop.model_type, "GROP")
        self.assertEqual(grop.materials, [])
        self.assertEqual(grop.head["element_count"], 9)
        self.assertEqual(len(grop.grops), 9)
        self.assertEqual(grop.grops[0]["name"], "metaldoor7")
        self.assertEqual(len(grop.grops[0]["pos"]), 3)

    def test_cli_info_json(self):
        path = os.path.join(KMF_DIR, SAMPLE_ANIM)
        out = subprocess.run(
            [sys.executable, TOOL, "info", path, "--json"],
            capture_output=True, text=True, check=True).stdout
        data = json.loads(out)
        self.assertEqual(data["type"], "ANIM")
        self.assertEqual(data["version"], 17)
        self.assertEqual(data["materials"][0]["name"],
                         "Material #4#add#Falloff")


if __name__ == "__main__":
    unittest.main()
