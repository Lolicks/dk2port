#!/usr/bin/env python3
"""Real-file assertions for the recovered Dungeon Keeper 2 mission data."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ROOT = os.path.dirname(PORT)
TOOLS = os.path.join(PORT, "tools")
DOCS = os.path.join(PORT, "docs")
MISSIONS_JSON = os.path.join(DOCS, "missions.json")

sys.path.insert(0, TOOLS)

import kwd_tool as K  # noqa: E402
import mission_tool as M  # noqa: E402

CAMPAIGN_NAMES = [
    "level1", "level2", "level3", "Level4", "level5",
    "Level6a", "Level6b", "level7", "level8", "Level9", "Level10",
    "level11A", "level11B", "level11C", "level12", "Level13", "Level14",
    "Level15a", "level15b", "Level16", "Level17", "level18", "Level19",
    "Level20",
]
SECRET_NAMES = ["Secret1", "Secret2", "Secret3", "Secret4", "Secret5"]
MP_NAMES = ["mpd1", "mpd2", "mpd3", "mpd4", "mpd5", "MPD6", "mpd7"]


class MissionDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(MISSIONS_JSON, "r", encoding="utf-8") as handle:
            cls.data = json.load(handle)
        cls.by_name = {m["name"]: m for m in cls.data}

    def _maps_dir(self):
        return os.path.join(ROOT, "Data", "editor", "maps")

    # --- completeness ----------------------------------------------------

    def test_66_entries(self):
        self.assertEqual(len(self.data), 66)
        self.assertEqual(len(self.by_name), 66, "mission names must be unique")

    def test_all_map_files_exist(self):
        maps_dir = self._maps_dir()
        for mission in self.data:
            with self.subTest(mission=mission["name"]):
                path = os.path.join(maps_dir, mission["map_file"])
                self.assertTrue(os.path.isfile(path), "missing %s" % path)

    def test_all_sidecars_exist(self):
        maps_dir = self._maps_dir()
        for mission in self.data:
            for key, filename in mission["sidecars"].items():
                with self.subTest(mission=mission["name"], sidecar=key):
                    self.assertTrue(os.path.isfile(os.path.join(maps_dir, filename)))

    # --- ordering --------------------------------------------------------

    def test_campaign_order_sanity(self):
        campaign = [m for m in self.data if m["category"] == "campaign"]
        self.assertEqual(len(campaign), 24)
        self.assertEqual({m["name"] for m in campaign}, set(CAMPAIGN_NAMES))

        expected = {name: (label, level, variant)
                    for label, level, variant, name in M.CAMPAIGN}
        for mission in campaign:
            label, level, variant = expected[mission["name"]]
            self.assertEqual(mission["campaign_order"], label)
            self.assertEqual(mission["campaign_level"], level)
            self.assertEqual(mission["campaign_variant"], variant)

        # one variant per ordered slot, levels 1..20 all represented
        self.assertEqual(
            {m["campaign_level"] for m in campaign}, set(range(1, 21)))
        orders = [m["campaign_order"] for m in campaign]
        self.assertEqual(len(orders), len(set(orders)))

    def test_secret_and_multiplayer_ordering(self):
        secrets = {m["name"] for m in self.data if m["category"] == "secret"}
        mps = {m["name"] for m in self.data if m["category"] == "multiplayer"}
        self.assertEqual(secrets, set(SECRET_NAMES))
        self.assertEqual(mps, set(MP_NAMES))

        # all categories account for exactly 66 missions
        counts = {}
        for mission in self.data:
            counts[mission["category"]] = counts.get(mission["category"], 0) + 1
        self.assertEqual(sum(counts.values()), 66)
        self.assertEqual(counts["campaign"], 24)
        self.assertEqual(counts["secret"], 5)
        self.assertEqual(counts["multiplayer"], 7)

    # --- recovered content -----------------------------------------------

    def test_at_least_ten_missions_with_strings(self):
        populated = [m for m in self.data if m["recovered_strings"]]
        self.assertGreaterEqual(len(populated), 10)
        for mission in populated:
            for item in mission["recovered_strings"]:
                self.assertTrue(item["value"])
                self.assertIn(item["confidence"],
                              {"high", "medium", "low", "unknown"})
                self.assertTrue(item["source"])

    def test_briefing_text_is_real(self):
        briefed = [m for m in self.data
                   if any(s["kind"] == "briefing_text"
                          for s in m["recovered_strings"])]
        self.assertGreaterEqual(len(briefed), 3)
        level3 = self.by_name["level3"]
        texts = [s["value"] for s in level3["recovered_strings"]
                 if s["kind"] == "briefing_text"]
        self.assertTrue(any("gold-grubber" in t for t in texts))
        self.assertTrue(any("Kill him!" in t for t in texts))

    def test_enemy_names_recovered(self):
        enemies = {}
        for mission in self.data:
            for item in mission["recovered_strings"]:
                if item["kind"] == "enemy_name":
                    enemies[mission["name"]] = item["value"]
        self.assertGreaterEqual(len(enemies), 10)
        self.assertEqual(enemies["level1"], "Antonius")
        self.assertEqual(enemies["level3"], "Avaricious")
        self.assertEqual(enemies["Level20"], "Reginald")

    def test_numeric_parameters_have_confidence_and_source(self):
        for mission in self.data:
            params = mission["numeric_parameters"]
            for key, spec in params.items():
                if isinstance(spec, list):
                    for item in spec:
                        self.assertIn("confidence", item)
                        self.assertIn("source", item)
                else:
                    self.assertIn("confidence", spec, (mission["name"], key))
                    self.assertIn("source", spec, (mission["name"], key))
            self.assertIsNone(params["starting_gold"]["value"])
            self.assertEqual(params["starting_gold"]["confidence"], "unknown")
            self.assertIsNone(params["starting_mana"]["value"])
            self.assertEqual(params["starting_mana"]["confidence"], "unknown")

    def test_dimensions_match_map_files(self):
        maps_dir = self._maps_dir()
        for mission in self.data:
            path = os.path.join(maps_dir, mission["map_file"])
            kmap = K.KldMap(path)
            self.assertEqual((kmap.width, kmap.height),
                             (mission["width"], mission["height"]))

    # --- parser / determinism --------------------------------------------

    def test_parser_roundtrip_from_bytes(self):
        path = os.path.join(self._maps_dir(), "AlcatrazVariables.kld")
        with open(path, "rb") as handle:
            original = handle.read()
        parsed = K.KwdFile(path)
        self.assertEqual(parsed.to_bytes(), original)

        map_path = os.path.join(self._maps_dir(), "CircletMap.kld")
        with open(map_path, "rb") as handle:
            map_original = handle.read()
        self.assertEqual(K.KldMap(map_path).to_bytes(), map_original)

    def test_json_roundtrip_is_deterministic(self):
        rebuilt = M.build_all(M.find_game_root())
        self.assertEqual(rebuilt, self.data)

        proc = subprocess.run(
            [sys.executable, os.path.join(TOOLS, "mission_tool.py"), "json"],
            cwd=ROOT, capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(proc.stdout), self.data)

    def test_cli_list_and_show(self):
        tool = os.path.join(TOOLS, "mission_tool.py")
        listing = subprocess.run(
            [sys.executable, tool, "list"], cwd=ROOT,
            capture_output=True, text=True, check=True).stdout
        self.assertEqual(len(listing.strip().splitlines()), 66)

        shown = subprocess.run(
            [sys.executable, tool, "show", "level3"], cwd=ROOT,
            capture_output=True, text=True, check=True).stdout
        self.assertIn("level3", shown)
        self.assertIn("gold-grubber", shown)


if __name__ == "__main__":
    unittest.main(verbosity=2)
