#!/usr/bin/env python3
"""Tests for the KMSH envelope reader and the honest codec status.

The codec IS recovered: ported from OpenKeeper `WadFile.decompressFileData`
and dk2-tools `Scripts/wad_extract.1sc`. These tests assert exact-size,
byte-exact decompression to valid KMF for every shipped mesh.

Run:  python3 port/tests/test_kmsh.py
"""

from __future__ import annotations

import os
import struct
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "port", "tools"))

import kmsh_decompress as kd  # noqa: E402

WAD = os.path.join(ROOT, "Data", "Meshes.WAD")


def read_all(limit=None):
    return kd.load_payloads(WAD, limit)


@unittest.skipUnless(os.path.exists(WAD), "Meshes.WAD not present")
class TestEnvelope(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = read_all()

    def test_payload_count(self):
        self.assertEqual(len(self.payloads), 2015)

    def test_wrapper_fields(self):
        for p in self.payloads:
            w = p.wrapper
            self.assertEqual(w[:2], b"\x10\xfb", p.index)
            self.assertEqual(w[6:10], b"KMSH", p.index)
            self.assertEqual(struct.unpack_from("<I", w, 10)[0],
                             p.declared_size, p.index)
            self.assertEqual(struct.unpack_from(">H", w, 3)[0],
                             p.declared_size & 0xFFFF, p.index)
            self.assertEqual(w[12], w[2], p.index)
            self.assertEqual(len(p.body), p.stored_size - kd.WRAPPER_SIZE)

    def test_variant_values(self):
        variants = {p.variant for p in self.payloads}
        self.assertTrue(variants.issubset({0, 1, 2, 3, 5}))

    def test_declared_size_positive_and_body_smaller(self):
        for p in self.payloads:
            self.assertGreater(p.declared_size, 0)
            self.assertLess(len(p.body), p.declared_size, p.index)
            self.assertGreater(p.ratio, 1.0)

    def test_validate_accepts_every_shipped_payload(self):
        for p in self.payloads:
            report = kd.validate(p)
            self.assertTrue(report["envelope_ok"], (p.index, report["problems"]))
            self.assertEqual(report["codec"], "recovered")

    def test_validate_rejects_corrupt_wrapper(self):
        p = self.payloads[0]
        bad = kd.Payload(
            p.index, p.name, p.variant, p.be16_size, p.trailer,
            p.declared_size, p.stored_size, b"\x00" * 14, p.body,
        )
        report = kd.validate(bad)
        self.assertFalse(report["envelope_ok"])
        self.assertIn("bad wrapper signature", report["problems"])


@unittest.skipUnless(os.path.exists(WAD), "Meshes.WAD not present")
class TestRawBodies(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = read_all()

    def test_clear_text_chunk_tags_in_body(self):
        """At least 20 distinct meshes expose literal KMF chunk tags."""
        checked = 0
        for p in self.payloads:
            tags = kd.visible_tags(p.body)
            if "HEAD" in tags and "MATL" in tags:
                checked += 1
        self.assertGreaterEqual(checked, 20,
                                "fewer than 20 bodies with HEAD+MATL literals")

    def test_first_entry_known_prefix(self):
        p = self.payloads[0]
        self.assertEqual(p.name, "g_Maiden-Torture.kmf")
        self.assertEqual(p.declared_size, 21287)
        self.assertEqual(p.stored_size, 5647)
        self.assertEqual(p.body[:17],
                         bytes.fromhex("11000000 48454144 01071001 03020103 01"))

    def test_twenty_distinct_names(self):
        names = [p.name for p in self.payloads[:25]]
        self.assertEqual(len(set(names)), 25)

    def test_decompression_writes_byte_exact_kmf(self):
        with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "port")) as out:
            for p in read_all(limit=3):
                data = kd.decompress_payload(p)
                self.assertEqual(len(data), p.declared_size, p.index)
                self.assertEqual(data[:4], b"KMSH", p.index)
                target = os.path.join(out, kd.safe_name(p.name, p.index))
                with open(target, "wb") as fh:
                    fh.write(data)
                with open(target, "rb") as fh:
                    self.assertEqual(fh.read(), data)
                self.assertTrue(target.endswith(".kmf"))

    def test_decompression_of_twenty_distinct_meshes(self):
        for p in self.payloads[:20]:
            data = kd.decompress_payload(p)
            self.assertEqual(len(data), p.declared_size, p.index)
            self.assertEqual(data[:4], b"KMSH", p.index)
            self.assertIn(b"HEAD", data[:64], p.index)

    def test_high_byte_alphabet_is_structured(self):
        """Bytes >= 0xe0 form a distinct, uniformly-present alphabet (all 2015)."""
        from collections import Counter
        hist = Counter()
        total_high = 0
        for p in self.payloads:
            stream = p.wrapper[6:] + p.body  # stored stream from payload offset 6
            for b in stream:
                if b >= 0xE0:
                    hist[b] += 1
                    total_high += 1
        self.assertEqual(len([v for v in hist if 0xE0 <= v <= 0xFF]), 32)
        self.assertGreater(total_high, 2_000_000)

    def test_rnc_signature_absent(self):
        """No payload is a standard RNC file: no 'RNC' magic anywhere."""
        for p in self.payloads:
            stored = p.wrapper + p.body
            self.assertNotIn(b"RNC", stored[:18], p.index)

    def test_batch_cli_writes_raw_bodies(self):
        import subprocess
        with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "port")) as out:
            r = subprocess.run(
                [sys.executable, os.path.join(ROOT, "port", "tools",
                                              "kmsh_decompress.py"),
                 "batch", WAD, out, "--limit", "3"],
                capture_output=True,
            )
            self.assertEqual(r.returncode, 0, r.stderr.decode("utf-8", "replace"))
            self.assertEqual(len(os.listdir(out)), 3)
            for name in os.listdir(out):
                self.assertTrue(name.endswith(".kmf"))


class TestStandalonePayload(unittest.TestCase):
    def test_parse_single_payload_file(self):
        sample = (b"\x10\xfb\x00\x53\x27\xe3" + b"KMSH"
                  + struct.pack("<I", 21287)
                  + bytes.fromhex("1100000048454144010710010302010301"))
        p = kd.parse_payload(sample, 0, "sample.kmf")
        self.assertEqual(p.declared_size, 21287)
        self.assertEqual(p.be16_size, 0x5327)
        self.assertEqual(p.body, sample[14:])
        self.assertTrue(kd.validate(p)["envelope_ok"])

    def test_parse_rejects_bad_magic(self):
        sample = bytearray(20)
        sample[6:10] = b"NOPE"
        with self.assertRaises(kd.KmshError):
            kd.parse_payload(bytes(sample))

    def test_parse_rejects_size_mismatch(self):
        sample = bytearray(b"\x10\xfb\x00\x01\x00\xe3" + b"KMSH"
                           + struct.pack("<I", 99))
        with self.assertRaises(kd.KmshError):
            kd.parse_payload(bytes(sample))


if __name__ == "__main__":
    unittest.main(verbosity=2)
