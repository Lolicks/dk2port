#!/usr/bin/env python3
"""Tests for the KWD stat recovery pipeline (decoder + generated stats.json).

All expectations are tied either to the shipped .kwd bytes, to the 010 Editor
layouts/OpenKeeper parser that the decoder derives from, or to numbers actually
printed in the shipped manual (via docs/balance.json).  No expected stat value
is invented here.
"""

from __future__ import annotations

import json
import struct
import sys
import unittest
from pathlib import Path

PORT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT_DIR / "tools"))

import stats_tool as st  # noqa: E402
from kwd_tool import KwdFile  # noqa: E402

STATS_PATH = PORT_DIR / "docs" / "stats.json"
GAME_DATA_PATH = PORT_DIR / "docs" / "game_data.json"
BALANCE_PATH = PORT_DIR / "docs" / "balance.json"

with open(STATS_PATH, encoding="utf-8") as _fh:
    STATS = json.load(_fh)
with open(GAME_DATA_PATH, encoding="utf-8") as _fh:
    GAME_DATA = json.load(_fh)
with open(BALANCE_PATH, encoding="utf-8") as _fh:
    BALANCE = json.load(_fh)

CATEGORY_KEYS = [row[0] for row in st.CATEGORIES]
SOURCE_DIRS = {
    "templates": PORT_DIR / "research" / "dk2-tools" / "Formats" / "Level",
    "java": PORT_DIR / "research" / "OpenKeeper" / "src" / "main" / "java"
            / "toniarts" / "openkeeper" / "tools" / "convert" / "map",
}


def by_name(category, name):
    for record in STATS[category]:
        if record["name"].strip() == name:
            return record["stats"]
    raise KeyError("%s: %s" % (category, name))


def value(category, name, field):
    return by_name(category, name)[field]["value"]


def leaves(node):
    """Yield every {value, source, confidence} leaf, including inside lists."""
    if isinstance(node, dict):
        if {"value", "source", "confidence"} <= set(node):
            yield node
        for child in node.values():
            yield from leaves(child)
    elif isinstance(node, list):
        for child in node:
            yield from leaves(child)


class LayoutTests(unittest.TestCase):
    def test_every_layout_reproduces_record_size(self):
        for key, kwd_name, count, size, bt_file, _spec, _fields in st.CATEGORIES:
            with self.subTest(category=key):
                kwd = KwdFile(str(st.DATA_DIR / (kwd_name + ".kwd")))
                self.assertEqual(kwd.record_count, count)
                self.assertEqual(kwd.record_size, size)
                self.assertEqual(st.LAYOUTS[key].size, size, bt_file)
                self.assertEqual(st.LAYOUTS[key].bt_file, bt_file)

    def test_anchor_offsets_match_template_comments(self):
        creature = st.LAYOUTS["creatures"].by_name()
        anchors = {"swipe_speed": 0xCEC, "portrait": 0xD40, "light": 0xD94,
                   "attractions": 0xDAC, "spells": 0xDC4,
                   "resistances": 0xE0C, "width": 0xEA0, "hp": 0xF0C,
                   "pay": 0xF1C, "flags": 0xEFC, "creature_id": 0xF4B,
                   "flags2": 0x1545}
        for name, offset in anchors.items():
            self.assertEqual(creature[name].offset, offset, name)
        rooms = st.LAYOUTS["rooms"].by_name()
        self.assertEqual(rooms["cost"].offset, 888)
        self.assertEqual(rooms["room_id"].offset, 914)
        self.assertEqual(rooms["health_gain"].offset, 1053)

    def test_fixed_point_decoders(self):
        blob = struct.pack("<i", 2 * 4096) + struct.pack("<h", 2048) + \
               struct.pack("<i", 2 * 65536)
        self.assertEqual(st.decode_scalar("fixed32", blob, 0), 2.0)
        self.assertEqual(st.decode_scalar("fixed16", blob, 4), 0.5)
        self.assertEqual(st.decode_scalar("fixed64", blob, 6), 2.0)


class GenerationTests(unittest.TestCase):
    def test_counts_match_manifest(self):
        fresh = st.decode_all()
        for key, _name, count, _size, _bt, _spec, _fields in st.CATEGORIES:
            with self.subTest(category=key):
                self.assertEqual(len(STATS[key]), count)
                self.assertEqual(len(fresh[key]), count)
                self.assertEqual(STATS["meta"]["record_sizes"][key]["count"], count)

    def test_stats_json_is_reproducible(self):
        self.assertEqual(st.build_document(), STATS)

    def test_game_data_names_match(self):
        mapping = (("creatures", "creature_names"), ("rooms", "room_names"),
                   ("traps", "trap_names"), ("keeper_spells", "keeper_spell_names"),
                   ("creature_spells", "creature_spell_names"),
                   ("doors", "door_names"))
        for key, data_key in mapping:
            with self.subTest(category=key):
                ours = [record["name"] for record in STATS[key]]
                theirs = GAME_DATA[data_key]
                # game_data.json built Record.name from the first 28 bytes;
                # decoder reads the full 32-byte field, so compare that prefix.
                if key == "rooms":
                    ours = [name[:28] for name in ours]
                self.assertEqual([n.strip() for n in ours],
                                 [n.strip() for n in theirs])

    def test_every_field_has_traceable_source(self):
        total = 0
        for category in CATEGORY_KEYS:
            for record in STATS[category]:
                for leaf in leaves(record["stats"]):
                    total += 1
                    self.assertEqual(set(leaf), {"value", "source", "confidence"})
                    self.assertIn(leaf["confidence"], ("high", "medium"))
                    if leaf["value"] is None:
                        # Only the derived room/spell name lookups may be null
                        # (empty attraction/spell slots carry id 0).
                        self.assertIn(leaf["source"].split(";")[0],
                                      ("creatures_struct.bt:310",
                                       "creatures_struct.bt:313"))
                    for citation in leaf["source"].split("; "):
                        file_name, line = citation.rsplit(":", 1)
                        candidates = [SOURCE_DIRS["templates"] / file_name,
                                      SOURCE_DIRS["java"] / file_name]
                        self.assertTrue(any(p.is_file() for p in candidates), citation)
                        path = next(p for p in candidates if p.is_file())
                        with open(path, encoding="utf-8", errors="replace") as fh:
                            lines = fh.readlines()
                        self.assertGreaterEqual(int(line), 1)
                        self.assertLessEqual(int(line), len(lines), citation)
        self.assertGreater(total, 5000)

    def test_declared_fields_all_present(self):
        for key, _name, _count, _size, _bt, _spec, _fields in st.CATEGORIES:
            declared = dict(st.CATEGORY_FIELDS[key])
            with self.subTest(category=key):
                for record in STATS[key]:
                    self.assertTrue(set(declared) <= set(record["stats"]))
                    for field_name, confidence in declared.items():
                        self.assertEqual(
                            record["stats"][field_name]["confidence"], confidence)


class PlausibilityTests(unittest.TestCase):
    def test_creature_stat_ranges(self):
        for rec in STATS["creatures"]:
            s = rec["stats"]
            with self.subTest(creature=rec["name"]):
                self.assertTrue(1 <= s["hp"]["value"] <= 100000)
                self.assertTrue(0 < s["speed"]["value"] <= 10)
                self.assertTrue(0 < s["run_speed"]["value"] <= 10)
                self.assertTrue(0 <= s["melee_damage"]["value"] <= 5000)
                self.assertTrue(0 <= s["pay"]["value"] <= 10000)
                self.assertTrue(0 <= s["fear"]["value"] <= 100000)
                self.assertTrue(0 <= s["threat"]["value"] <= 100000)
                self.assertTrue(0 <= s["hunger_rate"]["value"] <= 32768)
                self.assertTrue(0 <= s["time_sleep"]["value"] <= 100)
                self.assertTrue(0 <= s["exp_per_second"]["value"] <= 10000)
                self.assertTrue(0 <= s["max_angle_change"]["value"] <= 65535)
                self.assertEqual(len(s["resistances"]["value"]), 4)
                self.assertEqual(len(s["spells"]["value"]), 3)
                self.assertEqual(len(s["attractions"]["value"]), 2)
                self.assertEqual(len(s["happy_jobs"]["value"]), 3)

    def test_key_creatures(self):
        imp = by_name("creatures", "Imp")
        bd = by_name("creatures", "Bile Demon")
        bk = by_name("creatures", "Black Knight")
        da = by_name("creatures", "Dark Angel")
        warlock = by_name("creatures", "Warlock")
        jack = by_name("creatures", "Jack")
        self.assertEqual(imp["hp"]["value"], 500)
        self.assertTrue(imp["flag_is_worker"]["value"])
        self.assertTrue(imp["flag_is_evil"]["value"])
        self.assertGreater(imp["speed"]["value"], bd["speed"]["value"])
        self.assertGreater(bd["hp"]["value"], imp["hp"]["value"])
        self.assertGreater(bk["threat"]["value"], imp["threat"]["value"])
        self.assertGreater(da["melee_damage"]["value"],
                           warlock["melee_damage"]["value"])
        self.assertLess(jack["hp"]["value"], 500)
        self.assertGreater(jack["speed"]["value"], 1.5)
        self.assertEqual(value("creatures", "Unique Dark Angel", "hp"), 8000)

    def test_other_categories_ranges(self):
        for room in STATS["rooms"]:
            self.assertTrue(0 <= room["stats"]["cost"]["value"] <= 10000)
            self.assertTrue(0 <= room["stats"]["health_gain"]["value"] <= 10000)
        for trap in STATS["traps"]:
            self.assertTrue(0 <= trap["stats"]["health"]["value"] <= 100000)
            self.assertGreaterEqual(trap["stats"]["recharge_time"]["value"], 0)
        for spell in STATS["keeper_spells"]:
            self.assertTrue(0 <= spell["stats"]["mana_cost"]["value"] <= 100000)
        for obj in STATS["objects"]:
            self.assertTrue(0 <= obj["stats"]["hp"]["value"] <= 100000)
        for terrain in STATS["terrains"]:
            self.assertTrue(0 <= terrain["stats"]["max_health"]["value"] <= 65535)


class CrossCheckTests(unittest.TestCase):
    def test_manual_room_costs_that_agree_with_kwd(self):
        # docs/balance.json quotes the shipped manual.  These eleven entries
        # agree byte-for-byte; the four that disagree are documented as
        # unresolved in STATS_RECOVERY.md rather than silently "fixed".
        for name in ("Treasury", "Lair", "Hatchery", "Training Room",
                     "Combat Pit", "Torture Chamber", "Prison", "Graveyard",
                     "Wooden Bridge", "Stone Bridge", "Guard Room"):
            with self.subTest(room=name):
                self.assertEqual(value("rooms", name, "cost"),
                                 BALANCE["room_costs"][name])

    def test_manual_creature_requirements(self):
        # Reference: docs/balance.json "Hatchery(5x5)" for the Bile Demon.
        bd = by_name("creatures", "Bile Demon")
        attractions = {(a["room_name"]["value"], a["room_size"]["value"])
                       for a in bd["attractions"]["value"]}
        self.assertIn(("Hatchery", 25), attractions)
        self.assertIn(("Work Shop", 0), attractions)
        spells = {s["spell_name"]["value"] for s in bd["spells"]["value"]} - {None}
        self.assertEqual(spells, {"Gas Cloud", "Gas Missile"})
        self.assertEqual({s["spell_name"]["value"]
                          for s in by_name("creatures", "Warlock")["spells"]["value"]
                          if s["spell_name"]["value"] is not None},
                         {"Warlock Fireball", "Warlock Heal Creature", "Warlock Firebomb"})
        combat = [a["room_name"]["value"] for a in
                  by_name("creatures", "Black Knight")["attractions"]["value"]]
        self.assertIn("Combat Pit", combat)

    def test_high_confidence_majority(self):
        leaves_list = []
        for category in CATEGORY_KEYS:
            for record in STATS[category]:
                leaves_list.extend(leaves(record["stats"]))
        high = sum(1 for leaf in leaves_list if leaf["confidence"] == "high")
        self.assertGreater(high / len(leaves_list), 0.8)


if __name__ == "__main__":
    unittest.main()
