#!/usr/bin/env python3
"""Real-file tests for the DK2 KWD/KLD decoder.

Run with::

    python3 -m unittest discover -s port/tests -v
"""

import glob
import json
import os
import subprocess
import sys
import unittest

PORT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(PORT_DIR, "tools")
sys.path.insert(0, TOOLS_DIR)

import kwd_tool  # noqa: E402

GAME_DIR = os.path.dirname(PORT_DIR)
EDITOR_DIR = os.path.join(GAME_DIR, "Data", "editor")
MAP_DIR = os.path.join(EDITOR_DIR, "maps")
TOOL = os.path.join(TOOLS_DIR, "kwd_tool.py")

if not os.path.isdir(EDITOR_DIR):
    raise unittest.SkipTest("original Dungeon Keeper 2 data is not available")

EDITOR_KWDS = [
    "Creatures.kwd", "CreatureSpells.kwd", "Doors.kwd",
    "EffectElements.kwd", "Effects.kwd", "GlobalVariables.kwd",
    "KeeperSpells.kwd", "Objects.kwd", "Rooms.kwd", "Shots.kwd",
    "Terrain.kwd", "Traps.kwd",
]


def editor_path(name):
    return os.path.join(EDITOR_DIR, name)


class ContainerTests(unittest.TestCase):
    def test_files_present(self):
        for name in EDITOR_KWDS:
            self.assertTrue(os.path.isfile(editor_path(name)), name)

    def test_root_header_and_chunk_walk(self):
        for name in EDITOR_KWDS:
            with self.subTest(kwd=name):
                kwd = kwd_tool.KwdFile(editor_path(name))
                self.assertEqual(kwd.version, 4)
                self.assertGreaterEqual(len(kwd.chunks), 2)
                if name == "GlobalVariables.kwd":
                    self.assertEqual(kwd.declared_size, 9992)
                    self.assertEqual(kwd.chunks[1].end_offset,
                                     kwd.declared_size)
                    self.assertEqual(kwd.chunks[0].tag, 0xE7)
                    self.assertEqual(kwd.chunks[2].tag, 0xBE)
                    self.assertEqual(kwd.chunks[-1].end_offset, len(kwd.data))
                    return
                self.assertEqual(kwd.declared_size, len(kwd.data))
                self.assertEqual(kwd.warnings, [])
                self.assertEqual(kwd.chunks[-1].end_offset, len(kwd.data))

    def test_marker_sequence(self):
        for name in EDITOR_KWDS:
            with self.subTest(kwd=name):
                kwd = kwd_tool.KwdFile(editor_path(name))
                info, data = kwd.chunks[0], kwd.chunks[1]
                self.assertEqual(info.tag, (kwd.root_tag + 1) & 0xFFFFFFFF)
                self.assertEqual(info.size, 28)
                self.assertEqual(data.tag, (kwd.root_tag + 2) & 0xFFFFFFFF)
                self.assertIs(kwd.info_chunk, info)
                self.assertIs(kwd.data_chunk, data)

    def test_record_length_consistency(self):
        for name in EDITOR_KWDS:
            with self.subTest(kwd=name):
                kwd = kwd_tool.KwdFile(editor_path(name))
                self.assertIsNotNone(kwd.record_count)
                self.assertIsNotNone(kwd.record_size)
                self.assertEqual(kwd.record_count * kwd.record_size,
                                 kwd.data_chunk.size)
                self.assertEqual(len(kwd.records), kwd.record_count)
                self.assertEqual(kwd.records[-1].offset + kwd.record_size,
                                 kwd.data_chunk.end_offset)

    def test_root_tags_are_known(self):
        expected = {
            "Rooms.kwd": 0x78, "Traps.kwd": 0x82, "Doors.kwd": 0x8C,
            "KeeperSpells.kwd": 0x96, "CreatureSpells.kwd": 0xA0,
            "Creatures.kwd": 0xAA,
        }
        for name, tag in expected.items():
            with self.subTest(kwd=name):
                kwd = kwd_tool.KwdFile(editor_path(name))
                self.assertEqual(kwd.root_tag, tag)

    def test_roundtrip(self):
        for name in ["Creatures.kwd", "Rooms.kwd", "Traps.kwd",
                     "KeeperSpells.kwd"]:
            with self.subTest(kwd=name):
                with open(editor_path(name), "rb") as handle:
                    raw = handle.read()
                self.assertEqual(kwd_tool.KwdFile(data=raw).to_bytes(), raw)
        with open(os.path.join(MAP_DIR, "Level4Map.kld"), "rb") as handle:
            raw = handle.read()
        self.assertEqual(kwd_tool.KwdFile(data=raw).to_bytes(), raw)


class RecordTests(unittest.TestCase):
    def test_creature_names_recovered(self):
        kwd = kwd_tool.KwdFile(editor_path("Creatures.kwd"))
        names = kwd.record_names()
        self.assertEqual(len(names), 46)
        for expected in ["Imp", "Bile Demon", "Black Knight", "Warlock",
                         "Dark Angel ", "Horny", "Maiden Of The Nest",
                         "Unique Firefly", "Jack"]:
            self.assertIn(expected, names)
        self.assertEqual(names[0], "Imp")
        self.assertEqual(names[-1], "Jack")

    def test_room_names_recovered(self):
        kwd = kwd_tool.KwdFile(editor_path("Rooms.kwd"))
        names = kwd.record_names()
        self.assertEqual(len(names), 26)
        for expected in ["Treasury", "Lair", "Portal", "Dungeon Heart",
                         "Library", "Training Room", "Torture Chamber"]:
            self.assertIn(expected, names)

    def test_trap_names_recovered(self):
        kwd = kwd_tool.KwdFile(editor_path("Traps.kwd"))
        names = kwd.record_names()
        self.assertEqual(len(names), 16)
        for expected in ["Gas Trap", "Boulder Trap", "Sentry Trap",
                         "Fireburst Trap", "Lightning Trap", "Freeze Trap"]:
            self.assertIn(expected, names)

    def test_spell_names_recovered(self):
        keeper = kwd_tool.KwdFile(editor_path("KeeperSpells.kwd"))
        spells = keeper.record_names()
        self.assertEqual(len(spells), 13)
        for expected in ["Sight Of Evil", "Possession", "Create Imp",
                         "Thunderbolt", "Heal"]:
            self.assertIn(expected, spells)
        creature = kwd_tool.KwdFile(editor_path("CreatureSpells.kwd"))
        self.assertEqual(len(creature.record_names()), 41)
        self.assertEqual(creature.record_names()[0], "Warlock Fireball")

    def test_name_is_first_field_of_record(self):
        for name, expected in [("Creatures.kwd", "Imp"),
                               ("Rooms.kwd", "Treasury"),
                               ("Traps.kwd", "Gas Trap")]:
            with self.subTest(kwd=name):
                kwd = kwd_tool.KwdFile(editor_path(name))
                record = kwd.records[0]
                raw_name = kwd.data[record.offset:record.offset + 28]
                self.assertEqual(raw_name.split(b"\0", 1)[0],
                                 expected.encode("ascii"))
                self.assertEqual(raw_name[28 - 1], 0)

    def test_code_tokens_present(self):
        cases = [
            ("Creatures.kwd", "CREATURE_IMP"),
            ("Rooms.kwd", "ROOM_TREASURE"),
            ("Traps.kwd", "TRAP_GAS"),
            ("KeeperSpells.kwd", "KEEPER_SPELL_SIGHT_OF_EVIL"),
        ]
        for name, token in cases:
            with self.subTest(kwd=name):
                kwd = kwd_tool.KwdFile(editor_path(name))
                texts = [hit.text for hit in kwd.strings()]
                self.assertTrue(any(token in text for text in texts),
                                "%s not found in %s" % (token, name))


class MapTests(unittest.TestCase):
    def test_all_maps_dimension_equation(self):
        maps = sorted(glob.glob(os.path.join(MAP_DIR, "*Map.kld")))
        self.assertEqual(len(maps), 66)
        for path in maps:
            with self.subTest(map_file=os.path.basename(path)):
                kld = kwd_tool.KldMap(path)
                self.assertEqual(kld.version, 4)
                self.assertEqual(kld.declared_size, len(kld.data))
                self.assertTrue(kld.dimensions_valid)
                self.assertEqual(kld.tile_chunk.size,
                                 kld.width * kld.height * 4)
                self.assertEqual(len(kld.tiles), kld.width * kld.height)
                self.assertEqual(kld.chunks[-1].end_offset, len(kld.data))
                self.assertEqual(kld.warnings, [])

    def test_specific_dimensions(self):
        expected = {
            "Level4Map.kld": (50, 64),
            "CircletMap.kld": (80, 80),
            "Secret1Map.kld": (50, 50),
            "CavernsMap.kld": (101, 101),
            "FrontEnd3DLevelMap.kld": (7, 14),
        }
        for name, dims in expected.items():
            with self.subTest(map_file=name):
                kld = kwd_tool.KldMap(os.path.join(MAP_DIR, name))
                self.assertEqual((kld.width, kld.height), dims)
                self.assertEqual(len(kld.data), 36 + dims[0] * dims[1] * 4)

    def test_tile_word_layout(self):
        kld = kwd_tool.KldMap(os.path.join(MAP_DIR, "Level4Map.kld"))
        self.assertTrue(all((tile >> 24) == 1 for tile in kld.tiles))
        self.assertLess(len(set(kld.tiles)), 64)
        self.assertEqual(kld.tiles[0] & 0xFF, 0x1E)

    def test_non_map_kld_rejected(self):
        with self.assertRaises(kwd_tool.KwdError):
            kwd_tool.KldMap(os.path.join(MAP_DIR, "AlcatrazPlayers.kld"))


class CliTests(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, TOOL, *args],
                              capture_output=True, text=True, check=True)

    def test_tree_cli(self):
        out = self.run_tool("tree", editor_path("Creatures.kwd")).stdout
        self.assertIn("root tag:        0xAA", out)
        self.assertIn("record count:    46", out)
        self.assertIn("record size:     5449", out)

    def test_map_cli(self):
        path = os.path.join(MAP_DIR, "Level4Map.kld")
        out = self.run_tool("map", path).stdout
        self.assertIn("width x height:  50 x 64", out)
        self.assertIn("consistent:      True", out)
        data = json.loads(self.run_tool("map", path, "--json").stdout)
        self.assertEqual((data["width"], data["height"]), (50, 64))
        self.assertEqual(len(data["tiles"]), 3200)

    def test_json_cli(self):
        data = json.loads(self.run_tool("json", editor_path("Rooms.kwd")).stdout)
        self.assertEqual(data["kind"], "rooms")
        self.assertEqual(data["record_count"], 26)
        self.assertEqual(data["records"][0]["name"], "Treasury")
        self.assertGreater(data["string_total"], 50)

    def test_dump_strings_cli(self):
        out = self.run_tool("dump", editor_path("Traps.kwd"),
                            "--strings").stdout
        self.assertIn("Gas Trap", out)
        self.assertIn("TRAP_GAS", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
