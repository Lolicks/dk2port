#!/usr/bin/env python3
"""Real-file assertions for the decoded Dungeon Keeper 2 trigger chains."""

from __future__ import annotations

import json
import os
import re
import struct
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ROOT = os.path.dirname(PORT)
TOOLS = os.path.join(PORT, "tools")
DOCS = os.path.join(PORT, "docs")
TRIGGERS_JSON = os.path.join(DOCS, "triggers.json")
FORMAT_DOC = os.path.join(PORT, "docs", "format-research", "TRIGGERS_FORMAT.md")

sys.path.insert(0, TOOLS)

import mission_tool as M  # noqa: E402
import trigger_tool as T  # noqa: E402

CITATION = re.compile(
    r"(?<![\w/])((?:[\w.\-]+/)*[\w.\-]+\.(?:java|bt|py|json|md|txt))"
    r":(\d+)(?:-(\d+))?"
)

SHORT_CITATIONS = {
    "triggers_struct.bt":
        "port/research/dk2-tools/Formats/Level/triggers_struct.bt",
    "KwdFile.java":
        "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/"
        "convert/map/KwdFile.java",
    "Trigger.java":
        "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/"
        "convert/map/Trigger.java",
    "TriggerGeneric.java":
        "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/"
        "convert/map/TriggerGeneric.java",
    "TriggerAction.java":
        "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/"
        "convert/map/TriggerAction.java",
}


class TriggerCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(TRIGGERS_JSON, "r", encoding="utf-8") as handle:
            cls.data = json.load(handle)
        cls.by_name = {m["name"]: m for m in cls.data["maps"]}
        cls.root = M.find_game_root()

    def test_schema_and_66_maps(self):
        self.assertEqual(self.data["schema_version"], T.SCHEMA_VERSION)
        self.assertEqual(self.data["totals"]["maps"], 66)
        self.assertEqual(len(self.data["maps"]), 66)
        self.assertEqual(len(self.by_name), 66)

    def test_every_catalog_entry_points_at_a_real_trigger_file(self):
        missions = {m["name"].lower(): m for m in json.load(
            open(os.path.join(DOCS, "missions.json"), encoding="utf-8"))}
        maps_dir = os.path.join(self.root, "Data", "editor", "maps")
        for entry in self.data["maps"]:
            with self.subTest(map=entry["name"]):
                path = os.path.join(maps_dir, entry["trigger_file"])
                self.assertTrue(os.path.isfile(path), path)
                self.assertEqual(entry["trigger_file"],
                                 missions[entry["name"].lower()]["sidecars"]["triggers"])
                self.assertEqual(entry["category"],
                                 missions[entry["name"].lower()]["category"])

    def test_declared_counts_match_parsed_counts_on_all_files(self):
        for entry in self.data["maps"]:
            with self.subTest(map=entry["name"]):
                self.assertTrue(entry["counts_match"], entry["warnings"])
                self.assertEqual(entry["warnings"], [])
                self.assertEqual(entry["parsed_counts"]["conditions"],
                                 entry["declared_counts"]["conditions"])
                self.assertEqual(entry["parsed_counts"]["actions"],
                                 entry["declared_counts"]["actions"])
                self.assertEqual(entry["parsed_counts"]["total"],
                                 entry["parsed_counts"]["conditions"]
                                 + entry["parsed_counts"]["actions"])

    def test_totals_are_the_sum_of_the_maps(self):
        totals = self.data["totals"]
        self.assertEqual(totals["conditions"],
                         sum(m["parsed_counts"]["conditions"]
                             for m in self.data["maps"]))
        self.assertEqual(totals["actions"],
                         sum(m["parsed_counts"]["actions"]
                             for m in self.data["maps"]))
        self.assertEqual(totals["records"],
                         sum(m["parsed_counts"]["total"]
                             for m in self.data["maps"]))
        self.assertEqual((totals["conditions"], totals["actions"],
                          totals["records"]), (3753, 5408, 9161))

    def test_at_least_20_maps_with_decoded_triggers(self):
        populated = [m for m in self.data["maps"]
                     if m["parsed_counts"]["total"] > 0]
        self.assertGreaterEqual(len(populated), 20)
        self.assertEqual(len(populated), 60)
        for entry in populated:
            self.assertTrue(entry["nodes"])

    def test_empty_maps_are_reported_not_skipped(self):
        empty = sorted(m["name"] for m in self.data["maps"]
                       if m["parsed_counts"]["total"] == 0)
        self.assertEqual(empty, ["Small", "Spiral", "Swiss Cheese",
                                 "The Deep End", "U Turn", "Warrens"])


class RecordDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = os.path.join(M.find_game_root(), "Data", "editor", "maps",
                                "level1Triggers.kld")
        cls.parsed = T.parse_trigger_file(cls.path)

    def test_record_header_matches_template_byte_positions(self):
        with open(self.path, "rb") as handle:
            raw = handle.read()
        first = self.parsed["nodes"][0]
        self.assertEqual(first["offset"], 0x3C)
        tag, size = struct.unpack_from("<II", raw, first["offset"])
        self.assertEqual((tag, size), (T.CONDITION_TAG, 16))
        ident, nxt, child = struct.unpack_from("<HHH", raw, first["offset"] + 16)
        self.assertEqual((ident, nxt, child), (1, 10, 65223))
        self.assertEqual(raw[first["offset"] + 22], 34)
        self.assertEqual(raw[first["offset"] + 23], 1)
        self.assertEqual(first["type_name"], "LEVEL_TIME (34)")

    def test_tdata_layout_decodes_comparison_and_value(self):
        node = self.parsed["nodes"][0]
        self.assertEqual(node["tdata_raw"], "0500000000000000")
        self.assertEqual(node["params"]["comparison"],
                         "GREATER_OR_EQUAL_TO (5)")
        self.assertEqual(node["params"]["value"], 0)
        self.assertEqual(node["layout_source"], "KwdFile.java:2556-2566")

    def test_all_layouts_occupy_eight_bytes(self):
        for table in (T.GENERIC_LAYOUTS, T.ACTION_LAYOUTS):
            for fields, _source in table.values():
                self.assertEqual(sum(size for _n, _k, _e, size in fields), 8)
        self.assertTrue(all(n["layout_is_default"] is False
                            for n in self.parsed["nodes"]))

    def test_id_spaces_are_separate(self):
        for node in self.parsed["nodes"]:
            if node["kind"] == "condition":
                self.assertLess(node["id"], 0x8000)
            else:
                self.assertGreaterEqual(node["id"], 0x8000)

    def test_enum_names_are_resolved(self):
        self.assertEqual(T.ACTION_TYPES[10], "WIN_GAME")
        self.assertEqual(T.ACTION_TYPES[11], "LOSE_GAME")
        self.assertEqual(T.TARGET_TYPES[34], "LEVEL_TIME")
        self.assertEqual(T.TARGET_TYPES[47], "PLAYER_KILLED")
        self.assertEqual(T.COMPARISON_TYPES[5], "GREATER_OR_EQUAL_TO")
        self.assertEqual(T.MAKE_TYPES[3], "DOOR")

    def test_links_resolve_in_shared_id_space(self):
        by_id = {n["id"]: n for n in self.parsed["nodes"]}
        first = by_id[1]
        self.assertEqual(first["child_id"], 65223)
        self.assertTrue(first["child_resolved"])
        self.assertEqual(by_id[65223]["kind"], "action")
        self.assertEqual(by_id[10]["kind"], "condition")


class WinLoseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(TRIGGERS_JSON, "r", encoding="utf-8") as handle:
            cls.data = json.load(handle)
        cls.by_name = {m["name"]: m for m in cls.data["maps"]}

    def test_win_lose_present_in_several_campaign_maps(self):
        campaign = [m for m in self.data["maps"] if m["category"] == "campaign"]
        self.assertEqual(len(campaign), 24)
        with_win = [m for m in campaign if m["win_lose"]["win_count"]]
        with_lose = [m for m in campaign if m["win_lose"]["lose_count"]]
        self.assertGreaterEqual(len(with_win), 20)
        self.assertGreaterEqual(len(with_lose), 15)
        for name in ("level1", "level5", "level7", "Level10", "Level14",
                     "Level15a", "Level16", "Level20"):
            self.assertGreater(self.by_name[name]["win_lose"]["win_count"], 0, name)

    def test_level1_objective_chain_matches_bytes(self):
        entry = self.by_name["level1"]
        self.assertEqual(entry["enemy"], "Antonius")
        win = next(f for f in entry["win_lose"]["findings"]
                   if f["action"] == "WIN_GAME")
        self.assertEqual(win["action_id"], 65458)
        self.assertEqual(win["direct_predecessors"],
                         [{"id": 204, "via": "child"}])
        conditions = [a for a in win["ancestor_conditions"]
                      if a["kind"] == "condition"]
        names = {a["type_name"].split(" (")[0] for a in conditions}
        self.assertTrue({"TIMER", "PLAYER_CREATURES", "PLAYER_ROOMS",
                         "PLAYER_GOLD", "FLAG"} <= names)
        gold = next(a for a in conditions
                    if a["type_name"].startswith("PLAYER_GOLD"))
        self.assertEqual(gold["params"]["value"], 12000)

    def test_player_killed_and_level_time_chains(self):
        for name, enemy, player in (("level5", "Constantine", 1),
                                    ("level7", "Sigmund", 0),
                                    ("Level15a", "Volstag", 1)):
            with self.subTest(map=name):
                entry = self.by_name[name]
                self.assertEqual(entry["enemy"], enemy)
                win = next(f for f in entry["win_lose"]["findings"]
                           if f["action"] == "WIN_GAME")
                types = {a["type_name"].split(" (")[0]: a
                         for a in win["ancestor_conditions"]
                         if a["kind"] == "condition"}
                self.assertIn("PLAYER_KILLED", types)
                self.assertIn("LEVEL_TIME", types)
                self.assertEqual(types["PLAYER_KILLED"]["params"]["player_id"],
                                 player)

    def test_level20_reginald_win_chain(self):
        entry = self.by_name["Level20"]
        self.assertEqual(entry["enemy"], "Reginald")
        win = next(f for f in entry["win_lose"]["findings"]
                   if f["action"] == "WIN_GAME")
        killed = [a for a in win["ancestor_conditions"]
                  if a["type_name"].startswith("PLAYER_KILLED")]
        self.assertTrue(killed)
        self.assertEqual(killed[0]["params"]["player_id"], 1)

    def test_confidence_is_honest(self):
        for entry in self.data["maps"]:
            for finding in entry["win_lose"]["findings"]:
                self.assertEqual(finding["confidence"]["action_and_params"],
                                 "high")
                self.assertEqual(finding["confidence"]["chain_ancestry"],
                                 "medium")
                self.assertEqual(finding["confidence"]["logical_combination"],
                                 "unknown")


class CliAndDeterminismTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(TRIGGERS_JSON, "r", encoding="utf-8") as handle:
            cls.disk = json.load(handle)

    def test_rebuild_matches_committed_json(self):
        self.assertEqual(T.build_catalog(M.find_game_root()), self.disk)

    def test_cli_catalog_is_byte_deterministic(self):
        script = os.path.join(TOOLS, "trigger_tool.py")
        first = subprocess.run([sys.executable, script, "catalog"],
                               cwd=ROOT, capture_output=True, text=True,
                               check=True)
        second = subprocess.run([sys.executable, script, "catalog"],
                                cwd=ROOT, capture_output=True, text=True,
                                check=True)
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(json.loads(first.stdout), self.disk)

    def test_cli_list_and_json(self):
        script = os.path.join(TOOLS, "trigger_tool.py")
        listed = subprocess.run([sys.executable, script, "list", "level1"],
                                cwd=ROOT, capture_output=True, text=True,
                                check=True).stdout
        self.assertIn("356 conditions, 326 actions", listed)
        self.assertIn("LEVEL_TIME (34)", listed)
        self.assertIn("WIN_GAME=1 LOSE_GAME=1", listed)
        one = subprocess.run([sys.executable, script, "json", "level1"],
                             cwd=ROOT, capture_output=True, text=True,
                             check=True).stdout
        parsed = json.loads(one)
        self.assertEqual(parsed["trigger_file"], "level1Triggers.kld")
        self.assertEqual(parsed["enemy"], "Antonius")
        self.assertEqual(parsed["parsed_counts"]["total"], 682)

    def test_cli_rejects_unknown_map(self):
        script = os.path.join(TOOLS, "trigger_tool.py")
        proc = subprocess.run([sys.executable, script, "list", "no_such_map"],
                              cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("cannot resolve", proc.stderr)


class FormatDocTests(unittest.TestCase):
    def test_document_exists_and_cites_real_lines(self):
        self.assertTrue(os.path.isfile(FORMAT_DOC))
        with open(FORMAT_DOC, encoding="utf-8") as handle:
            text = handle.read()
        self.assertGreater(len(text), 10000)
        matches = CITATION.findall(text)
        self.assertGreater(len(matches), 30)
        for cited, start, end in matches:
            with self.subTest(citation="%s:%s-%s" % (cited, start, end)):
                path = os.path.join(ROOT, SHORT_CITATIONS.get(cited, cited))
                self.assertTrue(os.path.isfile(path), cited)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    lines = sum(1 for _ in fh)
                self.assertLessEqual(int(end or start), lines)

    def test_key_citations_point_at_the_claimed_symbols(self):
        expected = [
            ("port/research/dk2-tools/Formats/Level/triggers_struct.bt",
             146, "TriggerGeneric"),
            ("port/research/dk2-tools/Formats/Level/triggers_struct.bt",
             164, "TriggerAction"),
            ("port/research/dk2-tools/Formats/Level/triggers_struct.bt",
             150, "struct TriggerData"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/"
             "tools/convert/map/KwdFile.java", 2373, "readTriggers"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/"
             "tools/convert/map/KwdFile.java", 2607, "setId"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/"
             "tools/convert/map/Trigger.java", 35, "repeatTimes"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/"
             "tools/convert/map/TriggerGeneric.java", 31, "ComparisonType"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/"
             "tools/convert/map/TriggerAction.java", 33, "ActionType"),
        ]
        for cited, line_no, needle in expected:
            with self.subTest(citation="%s:%d" % (cited, line_no)):
                with open(os.path.join(ROOT, cited), encoding="utf-8",
                          errors="replace") as fh:
                    lines = fh.read().splitlines()
                self.assertIn(needle, lines[line_no - 1])


if __name__ == "__main__":
    unittest.main()
