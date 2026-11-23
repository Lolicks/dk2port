#!/usr/bin/env python3
"""Structural and citation checks for the two format-research documents.

These tests are deliberately honest: they verify that the documents exist,
carry the required sections, and that each ``path:line`` citation resolves to a
real source file whose line range exists. They also spot-check a few exact
source lines so a citation cannot silently drift to unrelated code. They do
NOT assert that the prose is true, and they do not re-run the gameplay scans.
"""

import os
import re
import unittest

PORT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT_DIR = os.path.dirname(PORT_DIR)

DOC_A = os.path.join(PORT_DIR, "docs", "format-research", "WAD_FORMAT.md")
DOC_B = os.path.join(PORT_DIR, "docs", "format-research", "OPENKEEPER_ANALYSIS.md")

CITATION = re.compile(
    r"(?<![\w/])((?:[\w.\-]+/)*[\w.\-]+\.(?:java|bt|1sc|py|json|md|gradle|txt))"
    r":(\d+)(?:-(\d+))?"
)


def read_text(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def flattened(text):
    return re.sub(r"\s+", " ", text.replace("`", ""))


_SOURCE_FILES = None


def source_files():
    global _SOURCE_FILES
    if _SOURCE_FILES is None:
        _SOURCE_FILES = []
        for dirpath, _dirnames, filenames in os.walk(PORT_DIR):
            for name in filenames:
                _SOURCE_FILES.append(os.path.join(dirpath, name))
    return _SOURCE_FILES


def resolve_source(cited):
    """Return an existing file for a cited path, or None."""
    candidates = [cited]
    candidates += [full for full in source_files()
                   if full.replace(os.sep, "/").endswith(cited)]
    for candidate in candidates:
        path = os.path.join(ROOT_DIR, candidate) if not os.path.isabs(candidate) else candidate
        if os.path.isfile(path):
            return path
    return None


class DocumentsExistTests(unittest.TestCase):
    def test_both_documents_exist_and_are_substantial(self):
        for path in (DOC_A, DOC_B):
            with self.subTest(doc=path):
                self.assertTrue(os.path.isfile(path), path)
                self.assertGreater(len(read_text(path)), 5000, path)

    def test_cited_source_files_exist(self):
        required = [
            "port/research/dk2-tools/Formats/Archives/wad_struct.bt",
            "port/research/dk2-tools/Scripts/wad_extract.1sc",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFileEntry.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/WadExtractor.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/kmf/KmfFile.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/KmfModelLoader.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/KmfAssetInfo.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/Dk2TextureDecoder.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/enginetextures/EngineTextureDecoder.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/enginetextures/EngineTexturesFile.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/loadingscreens/LoadingScreenFile.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/loadingscreens/LoadingScreenTextureDecoder.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFile.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/EAAudioFrame.java",
            "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/TgqPlayer.java",
            "port/research/dk2-tools/Formats/Model/kmf_struct.bt",
            "port/research/dk2-tools/Formats/Level/kwd_struct.bt",
            "port/research/dk2-tools/Formats/Sound/sdt_struct.bt",
            "port/research/dk2-tools/Scripts/sdt_extract.1sc",
            "port/tools/kmsh_decompress.py",
            "port/tools/wad_tool.py",
            "port/docs/WAD_FORMAT.md",
            "port/docs/wad_inventory.json",
        ]
        for cited in required:
            with self.subTest(path=cited):
                self.assertIsNotNone(resolve_source(cited), cited)


class CitationIntegrityTests(unittest.TestCase):
    def test_every_path_line_citation_resolves_and_is_in_range(self):
        for doc in (DOC_A, DOC_B):
            matches = CITATION.findall(read_text(doc))
            self.assertGreater(len(matches), 50, doc)
            for cited, start, end in matches:
                with self.subTest(doc=os.path.basename(doc), citation=f"{cited}:{start}-{end}"):
                    path = resolve_source(cited)
                    self.assertIsNotNone(path, f"unresolved citation {cited}")
                    with open(path, encoding="utf-8", errors="replace") as fh:
                        line_count = sum(1 for _ in fh)
                    first, last = int(start), int(end or start)
                    self.assertGreaterEqual(first, 1)
                    self.assertGreaterEqual(last, first)
                    self.assertLessEqual(last, line_count,
                                         f"{cited}:{last} past EOF ({line_count} lines)")

    def test_cited_lines_contain_the_claimed_symbols(self):
        """A small, hand-verified sample; guards against citation drift."""
        expected = [
            ("port/research/dk2-tools/Formats/Archives/wad_struct.bt", 12, "CompressType"),
            ("port/research/dk2-tools/Formats/Archives/wad_struct.bt", 33, "struct WadEntry"),
            ("port/research/dk2-tools/Scripts/wad_extract.1sc", 74, "decompressFileData"),
            ("port/research/dk2-tools/Scripts/wad_extract.1sc", 88, "decsize"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java",
             292, "decompressFileData"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java",
             117, "LinkedHashMap"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/kmf/KmfFile.java",
             123, "parseKmfFile"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/KmfModelLoader.java",
             144, "public Object load"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/Dk2TextureDecoder.java",
             440, "dd_texture"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/enginetextures/EngineTextureDecoder.java",
             65, "decompress_block"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/textures/loadingscreens/LoadingScreenFile.java",
             36, "LoadingScreenFile"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFile.java",
             119, "readFrame"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java",
             177, "decodeFrame"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/EAAudioFrame.java",
             52, "decodeFrame"),
            ("port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/TgqPlayer.java",
             113, "public void play"),
            ("port/research/dk2-tools/Formats/Model/kmf_struct.bt", 109, "struct Model"),
            ("port/research/dk2-tools/Formats/Level/kwd_struct.bt", 38, "struct Header"),
            ("port/research/dk2-tools/Formats/Sound/sdt_struct.bt", 19, "SoundType"),
            ("port/research/OpenKeeper/build.gradle", 12, "jmonkeyengine_version"),
            ("port/research/OpenKeeper/build.gradle", 96, "gdx-ai"),
        ]
        for cited, line_no, needle in expected:
            with self.subTest(citation=f"{cited}:{line_no}", needle=needle):
                path = resolve_source(cited)
                self.assertIsNotNone(path, cited)
                lines = read_text(path).splitlines()
                self.assertLess(line_no, len(lines) + 1)
                self.assertIn(needle, lines[line_no - 1])


class DocContentTests(unittest.TestCase):
    """Shared helpers; comparisons ignore Markdown backticks and line wraps."""

    def check_all(self, markers):
        for marker in markers:
            with self.subTest(marker=marker):
                self.assertIn(marker.replace("`", ""), self.flat)


class WadDocContentTests(DocContentTests):
    def setUp(self):
        self.text = read_text(DOC_A)
        self.flat = flattened(self.text)

    def test_required_sections(self):
        self.check_all((
            "# DWFB (`.WAD`) archive specification",
            "## 0. Source inventory", "## 1. Header", "## 2. Entry table",
            "## 3. Name pool", "## 4. Flags `0` vs `4`",
            "## 5. Compressed payload envelope", "## 6. The exact token algorithm",
            "## 7. Error handling", "## 8. Verification", "## 9. Repacking",
            "## 10. Reproduction",
        ))

    def test_field_tables_and_flag_values(self):
        self.check_all(("0x48", "0x4C", "0x50", "0x54", "+0x00", "+0x14",
                        "+0x18", "UNCOMPRESSED", "COMPRESSED"))

    def test_token_algorithm_markers(self):
        self.check_all(("0x80", "0x40", "0x20", "0x70", "counter + 1",
                        "big-endian", "start offset 5", "offset 2", "do/while",
                        "| 0 |", "| 1 |", "| 2 |", "| 3 |"))

    def test_duplicates_corrupt_entries_and_repacking_are_covered(self):
        self.check_all(("Duplicate names", "Error handling for corrupt entries",
                        "No tool in this checkout writes DWFB archives",
                        "has 31 duplicate names"))

    def test_recorded_verification_numbers(self):
        self.check_all(("2015 statuses {'ok'}", "21287", "24377", "53622297",
                        "831250", "2387386", "761971", "37770",
                        "'clean': 2015, 'with_issues': 0, 'errors': 0"))

    def test_inventory_numbers_match_generated_json(self):
        import json
        inventory = json.loads(read_text(os.path.join(PORT_DIR, "docs", "wad_inventory.json")))
        self.assertEqual(len(inventory["wads"]), 5)
        for name, facts in inventory["wads"].items():
            with self.subTest(wad=name):
                self.assertIn(name.replace(".WAD", ""), self.flat)
                self.assertIn(str(facts["entry_count"]), self.flat)


class OpenKeeperDocContentTests(DocContentTests):
    def setUp(self):
        self.text = read_text(DOC_B)
        self.flat = flattened(self.text)

    def test_required_sections(self):
        self.check_all((
            "# OpenKeeper / dk2-tools implementation analysis",
            "## 1. Licence and dependency facts", "## 2. WAD / DWFB reader",
            "## 3. KMF models", "## 4. Engine textures", "## 5. TGQ / TQI movies",
            "## 6. dk2-tools binary templates and scripts",
            "## 7. Format → implementation → verified?",
            "## 8. Verification commands and results",
            "## 9. What OpenKeeper does **not** implement",
            "## 10. Standalone C decoder",
        ))

    def test_all_requested_classes_are_cited(self):
        self.check_all((
            "KmfFile.java:", "KmfModelLoader.java:", "KmfAssetInfo.java:",
            "WadFile.java:", "WadExtractor.java:", "Dk2TextureDecoder.java:",
            "EngineTextureDecoder.java:", "EngineTexturesFile.java:",
            "LoadingScreenFile.java:", "LoadingScreenTextureDecoder.java:",
            "TgqFile.java:", "TgqFrame.java:", "EAAudioFrame.java:",
            "TgqPlayer.java:", "wad_struct.bt:", "kmf_struct.bt:",
            "sdt_struct.bt:", "kwd_struct.bt:", "sprite_struct.bt:",
            "texturecache_dir_struct.bt:", "texturecache_dat_struct.bt:",
            "wad_extract.1sc:", "sdt_extract.1sc:",
        ))

    def test_license_and_dependency_facts(self):
        self.check_all(("GPLv3-or-later", "GNU GPL v3", "jme3-core", "gdx-ai:1.8.2",
                        "javax.vecmath:vecmath:1.5.2", "no LICENSE/COPYING file",
                        "Directly portable arithmetic", "Avoidable runtime deps"))

    def test_not_implemented_claim_is_corrected(self):
        self.check_all(("is **false for this checkout**", "hand-written",
                        "not a byte-exact", "543 Java files"))

    def test_verification_table_and_results(self):
        self.check_all(("| Format | Implementation(s) | Verified? |",
                        "sdt_files= 142 total_entries= 8959",
                        '"chunk_count":1503', '"frame_count":750',
                        "{'clean': 2015, 'with_issues': 0, 'errors': 0}",
                        "Source-read only"))


if __name__ == "__main__":
    unittest.main()
