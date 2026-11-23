#!/usr/bin/env python3
"""Honest assertions for docs/format-research/TQI_TGQ_FORMAT.md.

The tests check two things only: (a) the document contains the exact
source citations and measured numbers it claims, and (b) those numbers can be
reproduced from the shipped movies and the cited source files.  No image
decoder is reimplemented or assumed here; PNG hashes are verified only when the
artifacts produced by tools/tgq_decode_openkeeper.sh are present.
"""

from __future__ import annotations

import collections
import glob
import hashlib
import os
import struct
import sys

import pytest

PORT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ROOT = os.path.abspath(os.path.join(PORT, ".."))
DOC = os.path.join(PORT, "docs", "format-research", "TQI_TGQ_FORMAT.md")
MOVIES = os.path.join(ROOT, "Data", "Movies")
sys.path.insert(0, os.path.join(PORT, "tools"))

from tgq_probe import TgqFile, TAG_VIDEO_FRAME, TAG_AUDIO_BLOCK, TAG_STREAM_CLOSE  # noqa: E402

MOVIE_FILES = sorted(glob.glob(os.path.join(MOVIES, "*.[Tt][Gg][Qq]")))


@pytest.fixture(scope="module")
def doc():
    with open(DOC, encoding="utf-8") as fh:
        return fh.read()


def _summary(path):
    return TgqFile(path).summary()


def _first_video_payload(tgq, n=16):
    chunks = tgq.of_tag(TAG_VIDEO_FRAME)
    assert chunks, "no pIQT chunks"
    return tgq.read_payload(chunks[0], n)


def test_doc_exists_and_has_required_sections(doc):
    for heading in (
        "## 0. Verdict / executive summary",
        "## 2. Third-party dependency audit (the \"jlibtqi\" question)",
        "## 4. The corrected 8-byte TQI frame header",
        "## 5. OpenKeeper video decoder — exact source walkthrough",
        "## 7. Ghidra cross-check: what is proved and what is assumed",
        "## 8. Probe evidence — exact measured numbers",
        "## 9. Successful pixel decode",
        "## 10. Confirmed facts / hypotheses / unknowns",
        "## 11. Requirements for a future independent C decoder",
    ):
        assert heading in doc, "missing section: %s" % heading
    assert "HYPOTHESIS" in doc and "UNKNOWN" in doc
    assert "not a new decoder" in doc


def test_doc_cites_exact_source_lines_that_exist(doc):
    cases = [
        ("research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFile.java",
         55, "TQG_TAG"),
        ("research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java",
         178, "quantizer"),
        ("research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java",
         179, "Skip 3 bytes"),
        ("research/OpenKeeper/src/main/java/toniarts/openkeeper/video/TgqPlayer.java",
         52, "FPS = 25"),
        ("re/ghidra/decomp/005b26f0.c", 31, "FUN_00659580(param_1 + 8)"),
        ("re/ghidra/decomp/005b1d7e.c", 5, "DAT_007ad000 = param_2 << 2"),
        ("re/ghidra/decomp/00659b90.c", 154, "DAT_007af0cc != 0"),
    ]
    for rel, lineno, needle in cases:
        with open(os.path.join(PORT, rel), encoding="utf-8") as fh:
            line = fh.read().splitlines()[lineno - 1]
        assert needle in line, "%s:%d does not contain %r" % (rel, lineno, needle)
        assert "%s:%d" % (os.path.basename(rel) if rel.endswith(".java") else rel, lineno) in doc or needle in doc


def test_doc_records_no_jlibtqi_dependency(doc):
    assert "no `jlibtqi`" in doc.lower() or "no jlibtqi" in doc.lower()
    build = os.path.join(PORT, "research", "OpenKeeper", "build.gradle")
    with open(build, encoding="utf-8") as fh:
        text = fh.read().lower()
    assert "jlibtqi" not in text and "libtqi" not in text
    assert "jmonkeyengine_version = '3.9.0-stable'" in text


def test_doc_probe_numbers_match_measured_files(doc):
    assert "213 445 428 bytes" in doc
    assert "16 108 video frames" in doc and "16 109 audio blocks" in doc
    assert "16087/16109" in doc
    assert "6492" in doc and "0x195c" in doc
    assert "14 1e 03" in doc
    assert "0x62` → 5321" in doc and "0x63` → 9882" in doc and "0x64` → 905" in doc

    total_bytes = sum(os.path.getsize(p) for p in MOVIE_FILES)
    assert total_bytes == 213_445_428
    video_total = audio_total = 0
    qf = collections.Counter()
    extras = collections.Counter()
    sizes = []
    for path in MOVIE_FILES:
        tgq = TgqFile(path)
        assert tgq.ok
        video_total += len(tgq.of_tag(TAG_VIDEO_FRAME))
        audio_total += len(tgq.of_tag(TAG_AUDIO_BLOCK))
        for chunk in tgq.of_tag(TAG_VIDEO_FRAME):
            payload = tgq.read_payload(chunk, 8)
            qf[payload[4]] += 1
            extras[payload[5:8].hex(" ")] += 1
            sizes.append(chunk["size"])
    assert video_total == 16_108
    assert audio_total == 16_109
    assert qf == {0x62: 5321, 0x63: 9882, 0x64: 905}
    assert extras == {"14 1e 03": 16_108}
    assert (min(sizes), max(sizes)) == (2124, 32452)


@pytest.mark.parametrize("name,size,chunks,video,audio,sccl,qflags", [
    ("BullfrogIntro.tgq", 1_986_192, 379, 189, 187, 187, 0x64),
    ("INTRO.TGQ", 40_589_016, 6580, 3288, 3289, 3289, 0x62),
    ("trailer.tgq", 6_785_068, 1503, 750, 750, 750, 0x64),
])
def test_named_movie_numbers(doc, name, size, chunks, video, audio, sccl, qflags):
    path = os.path.join(MOVIES, name)
    s = _summary(path)
    assert s["file_size"] == size
    assert s["chunk_count"] == chunks
    assert s["counts"]["pIQT"] == video
    assert s["counts"]["SCDl"] == audio
    assert s["sccl_value"] == sccl
    assert s["video_header"] == {
        "width": 320, "height": 480,
        "quantizer_flags": qflags,
        "quantizer_flags_hex": "0x%02X" % qflags,
        "extra_hex": "14 1e 03",
    }
    assert name in doc


def test_bullfrog_first_chunks_and_header(doc):
    tgq = TgqFile(os.path.join(MOVIES, "BullfrogIntro.tgq"))
    assert tgq.chunks[0]["tag"] == "SCHl" and tgq.chunks[0]["size"] == 40
    assert tgq.chunks[-1]["tag"] == "SCEl" and tgq.chunks[-1]["size"] == 8
    first_video = next(c for c in tgq.chunks if c["tag"] == "pIQT")
    assert first_video["offset"] == 40 and first_video["size"] == 6492
    assert _first_video_payload(tgq, 8)[:8] == bytes.fromhex("40 01 e0 01 64 14 1e 03")
    assert "40 01 e0 01 64 14 1e 03" in doc
    assert "1  40      pIQT  6492" in doc


def test_audio_block_equation_matches_doc(doc):
    matched = total = 0
    for path in MOVIE_FILES:
        tgq = TgqFile(path)
        for chunk in tgq.of_tag(TAG_AUDIO_BLOCK):
            payload = tgq.read_payload(chunk)
            coded = struct.unpack_from("<I", payload, 0)[0]
            total += 1
            if coded == (len(payload) - 12) // 30 * 28:
                matched += 1
    assert (matched, total) == (16087, 16109)


def test_decode_tool_and_documented_png_hashes(doc):
    script = os.path.join(PORT, "tools", "tgq_decode_openkeeper.sh")
    harness = os.path.join(PORT, "tools", "TgqDecodeOne.java")
    assert os.path.isfile(script) and os.access(script, os.X_OK)
    assert os.path.isfile(harness)
    script_text = open(script, encoding="utf-8").read()
    assert "TgqFrame.java" in script_text and "TgqFile.java" in script_text
    assert "jme3-core" in script_text and "vecmath" in script_text

    documented = {
        "a8c4184d589d9cc5db65f672ec08fc70c4e58df5fcc3a8f2035f0ec7fd0be4f1": "frame_000.png",
        "0da6fda1ada14922cd0b2e3e312fadb7a718bf722388ed5b770ae21d1c37131b": "frame_001.png",
        "a5f47d5900bc5d653184fc4e8a6419a2da1160afbc6e9b135f139b97eb6e07b8": "frame_002.png",
    }
    for digest, name in documented.items():
        assert digest in doc, "%s not documented" % name
        produced = os.path.join(PORT, ".work", "tqi_research", "frames_script", name)
        if os.path.isfile(produced):
            with open(produced, "rb") as fh:
                assert hashlib.sha256(fh.read()).hexdigest() == digest
