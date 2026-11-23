#!/usr/bin/env python3
"""Honest assertions about the decompiled-function mapping research."""
import json
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)

DOC = os.path.join(PORT, "docs", "format-research", "DECOMPILED_FUNCTIONS.md")
FUNCS = os.path.join(PORT, "re", "ghidra", "functions.json")


class TestDecompiledMapping(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(DOC, encoding="utf-8") as fh:
            cls.text = fh.read()
        with open(FUNCS, encoding="utf-8") as fh:
            cls.functions = json.load(fh)

    def test_corpus_counts(self):
        self.assertEqual(len(self.functions), 8260)

    def test_doc_has_required_sections(self):
        for section in (
            "Method and confidence rubric", "Candidate catalogue",
            "Cross-implementation correspondence table",
            "Where no matching function was found", "Limitations",
        ):
            self.assertIn(section, self.text, section)

    def test_anchors_exist_in_function_inventory(self):
        addrs = {f["address"] for f in self.functions}
        for anchor in ("00448d80", "00575700", "005b5e00", "00555b10",
                       "0057d270", "005b26f0", "005d15a0"):
            self.assertIn(anchor, addrs, anchor)

    def test_anchors_cited_in_doc(self):
        low = self.text.lower()
        for anchor in ("00448d80", "0057d270", "005b26f0", "00659580"):
            self.assertIn(anchor, low, anchor)

    def test_no_bare_name_claims(self):
        self.assertIn("no claim below rests\non a function name alone", self.text.lower())

    def test_confidence_vocabulary(self):
        for level in ("HIGH", "MEDIUM", "LOW"):
            self.assertIn(level, self.text)


if __name__ == "__main__":
    unittest.main()
