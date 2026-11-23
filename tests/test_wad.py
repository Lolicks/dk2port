#!/usr/bin/env python3
"""Tests for the DWFB WAD decoder, run against the shipped DK2 WAD files."""

import os
import struct
import sys
import tempfile
import unittest

PORT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(PORT_DIR, "tools")
sys.path.insert(0, TOOLS_DIR)

import wad_tool  # noqa: E402

GAME_DIR = os.path.dirname(PORT_DIR)
DATA_DIR = os.path.join(GAME_DIR, "Data")

if not os.path.isdir(DATA_DIR):
    raise unittest.SkipTest("original Dungeon Keeper 2 data is not available")

WADS = {
    "EngineTextures.WAD": 102,
    "Sprite.WAD": 24,
    "Paths.WAD": 81,
    "FrontEnd.WAD": 445,
    "Meshes.WAD": 2015,
}


def wad_path(name):
    return os.path.join(DATA_DIR, name)


class HeaderTests(unittest.TestCase):
    def test_files_present(self):
        for name in WADS:
            self.assertTrue(os.path.isfile(wad_path(name)), name)

    def test_magic_and_version(self):
        for name in WADS:
            with self.subTest(wad=name):
                wad = wad_tool.WadFile(wad_path(name))
                self.assertEqual(wad.data[:4], b"DWFB")
                self.assertEqual(wad.version, 2)
                self.assertEqual(wad.header_reserved, 0)

    def test_entry_counts_plausible(self):
        for name, expected in WADS.items():
            with self.subTest(wad=name):
                wad = wad_tool.WadFile(wad_path(name))
                self.assertEqual(wad.count, expected)
                index_end = wad_tool.HEADER_SIZE + wad.count * wad_tool.ENTRY_SIZE
                self.assertLessEqual(index_end, len(wad.data))

    def test_name_pool_layout(self):
        for name in WADS:
            with self.subTest(wad=name):
                wad = wad_tool.WadFile(wad_path(name))
                self.assertEqual(wad.pool_offset + wad.pool_size, len(wad.data))
                self.assertEqual(sum(e.name_length for e in wad.entries),
                                 wad.pool_size)
                self.assertEqual(wad.warnings, [])


class EntryTests(unittest.TestCase):
    def test_entry_offsets_within_bounds(self):
        for name in WADS:
            with self.subTest(wad=name):
                wad = wad_tool.WadFile(wad_path(name))
                self.assertGreater(wad.count, 0)
                for e in wad.entries:
                    self.assertLessEqual(e.name_offset + e.name_length,
                                         len(wad.data))
                    self.assertLessEqual(e.data_offset, len(wad.data))
                    self.assertLessEqual(e.data_offset + e.data_size,
                                         len(wad.data))
                    self.assertGreater(e.name_length, 1)

    def test_names_are_null_terminated_ascii(self):
        for name in WADS:
            with self.subTest(wad=name):
                wad = wad_tool.WadFile(wad_path(name))
                for e in wad.entries:
                    raw = wad.data[e.name_offset:e.name_offset + e.name_length]
                    self.assertTrue(raw.endswith(b"\x00"))
                    self.assertEqual(raw[:-1].decode("latin-1"), e.name)

    def test_data_regions_contiguous_and_ordered(self):
        for name in WADS:
            with self.subTest(wad=name):
                wad = wad_tool.WadFile(wad_path(name))
                expected = wad_tool.HEADER_SIZE + wad.count * wad_tool.ENTRY_SIZE
                for e in wad.entries:
                    self.assertEqual(e.data_offset, expected)
                    expected += e.data_size
                self.assertEqual(expected, wad.pool_offset)

    def test_mesh_compression_fields(self):
        wad = wad_tool.WadFile(wad_path("Meshes.WAD"))
        for e in wad.entries:
            blob = wad.payload(e)
            self.assertEqual(e.flags, 4)
            self.assertGreater(e.uncompressed_size, e.data_size)
            self.assertEqual(blob[6:10], b"KMSH")
            self.assertEqual(struct.unpack_from("<I", blob, 10)[0],
                             e.uncompressed_size)


class ExtractionTests(unittest.TestCase):
    def test_extract_round_trips_source_range(self):
        for name in WADS:
            with self.subTest(wad=name):
                wad = wad_tool.WadFile(wad_path(name))
                entry = wad.entries[0]
                with tempfile.TemporaryDirectory() as td:
                    results = wad.extract(td, [entry.name])
                    self.assertEqual(len(results), 1)
                    out = os.path.join(td, results[0]["output"])
                    with open(out, "rb") as fh:
                        extracted = fh.read()
                    source = wad.data[entry.data_offset:
                                      entry.data_offset + entry.data_size]
                    self.assertEqual(extracted, source)
                    self.assertEqual(len(extracted), entry.data_size)

    def test_extract_large_png_and_validate_png_signature(self):
        wad = wad_tool.WadFile(wad_path("EngineTextures.WAD"))
        with tempfile.TemporaryDirectory() as td:
            wad.extract(td, ["MapRock.png"])
            out = os.path.join(td, "MapRock.png")
            with open(out, "rb") as fh:
                self.assertEqual(fh.read(8), b"\x89PNG\r\n\x1a\n")
            self.assertEqual(os.path.getsize(out), 225335)

    def test_extract_subdirectories_and_duplicate_names(self):
        wad = wad_tool.WadFile(wad_path("EngineTextures.WAD"))
        with tempfile.TemporaryDirectory() as td:
            results = wad.extract(td, [r"GUI\Creatures\more_creatures.png"])
            self.assertEqual(len(results), 1)
            self.assertEqual(results[0]["output"],
                             os.path.join("GUI", "Creatures", "more_creatures.png"))
            self.assertTrue(os.path.isfile(os.path.join(td, results[0]["output"])))

        wad = wad_tool.WadFile(wad_path("FrontEnd.WAD"))
        seen = {}
        for e in wad.entries:
            seen[e.name] = seen.get(e.name, 0) + 1
        self.assertTrue(any(v > 1 for v in seen.values()))
        with tempfile.TemporaryDirectory() as td:
            results = wad.extract(td)
            self.assertEqual(len(results), wad.count)
            self.assertEqual(len({r["output"] for r in results}), wad.count)


if __name__ == "__main__":
    unittest.main(verbosity=2)
