#!/usr/bin/env python3
"""Honest tests for the documented DK2S save reader (synthetic fixtures)."""
import json
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import save_tool
CREATURE_CPP = os.path.join(ROOT, "engine", "DK2.cpp")


def make_save(path, rooms=3, creatures=5):
    buf = bytearray(24 + rooms * 28 + 64)
    buf[0:4] = b"DK2S"
    struct.pack_into("<HHIII", buf, 4, 6, 4, 12345, creatures, rooms)
    for i in range(rooms):
        base = 24 + i * 28
        name = b"Treasury"[:20]
        struct.pack_into("<H", buf, base, len(name))
        buf[base + 2:base + 2 + len(name)] = name
    with open(path, "wb") as fh:
        fh.write(buf)
    return bytes(buf)


class TestSaveReader(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "test.dk2")
        make_save(self.path)

    def tearDown(self):
        self.tmp.cleanup()

    def test_header(self):
        with open(self.path, "rb") as fh:
            h = save_tool.read_header(fh.read())
        self.assertEqual(h["magic"], "DK2S")
        self.assertEqual(h["version"], 6)
        self.assertEqual(h["level"], 4)
        self.assertEqual(h["gold"], 12345)
        self.assertEqual(h["creature_count"], 5)
        self.assertEqual(h["room_count"], 3)

    def test_rooms(self):
        with open(self.path, "rb") as fh:
            data = fh.read()
        rooms, off = save_tool.read_rooms(data, save_tool.read_header(data))
        self.assertEqual(len(rooms), 3)
        self.assertEqual(rooms[0]["name"], "Treasury")
        self.assertEqual(off, 24 + 3 * 28)

    def test_bad_magic(self):
        with self.assertRaises(save_tool.SaveError):
            save_tool.read_header(b"NOPE" + b"\0" * 32)

    def test_truncated(self):
        with self.assertRaises(save_tool.SaveError):
            save_tool.read_header(b"DK2S")

    def test_convert_is_honest(self):
        out = os.path.join(self.tmp.name, "converted.json")
        with open(self.path, "rb") as fh:
            result = save_tool.convert(fh.read(), out)
        self.assertEqual(result["creatures"], [])
        self.assertTrue(any("NOT decoded" in x for x in result["limitations"]))
        with open(out) as fh:
            self.assertEqual(json.load(fh)["creatures"], [])

    def test_reference_documented(self):
        src = os.path.join(ROOT, "research", "dk2-save-tools", "src", "header.ts")
        if not os.path.isfile(src):
            self.skipTest("dk2-save-tools not cloned")
        text = open(src).read()
        self.assertIn("DK2S", text)
        self.assertIn("readUInt16LE(4)", text)
        self.assertIn("readUInt32LE(8)", text)

    def test_creature_block_marked_broken_upstream(self):
        src = os.path.join(ROOT, "research", "dk2-save-tools", "src", "creature.ts")
        if not os.path.isfile(src):
            self.skipTest("dk2-save-tools not cloned")
        self.assertIn("BROKEN", open(src).read())


if __name__ == "__main__":
    unittest.main()
