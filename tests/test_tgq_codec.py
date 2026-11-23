#!/usr/bin/env python3
"""Honest tests for the TGQ container and the identified TQI frame structure.

These tests deliberately do NOT assert any decoded image output: the TQI pixel
decoder was not implemented (see docs/TGQ_CODEC_ANALYSIS.md).  They assert only
facts that were verified from the shipped bytes and from the decompiled game
binary.
"""

from __future__ import annotations

import glob
import os
import struct
import sys

import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MOVIES = os.path.join(ROOT, "Data", "Movies")
TOOLS = os.path.join(os.path.dirname(__file__), "..", "tools")
sys.path.insert(0, TOOLS)

from tgq_probe import TgqFile, TAG_STREAM_HEADER, TAG_VIDEO_FRAME, TAG_STREAM_CLOSE, TAG_AUDIO_BLOCK, TAG_STREAM_END  # noqa: E402
from tgq_decode import (TQI_HEADER_SIZE, frame_header, probe_file,
                        count_mpeg_start_codes, video_payloads)  # noqa: E402

MOVIE_FILES = sorted(glob.glob(os.path.join(MOVIES, "*.[Tt][Gg][Qq]")))


def _first_payload(path):
    tgq = TgqFile(path)
    for _, _, payload in video_payloads(tgq):
        return payload
    raise AssertionError("no video frames in %s" % path)


@pytest.mark.parametrize("path", MOVIE_FILES)
def test_container_walk_lands_exactly_on_eof(path):
    tgq = TgqFile(path)
    assert tgq.ok, "chunk walk failed for %s: %s" % (path, tgq.warnings)
    chunks = tgq.chunks
    assert chunks[0]["tag"] == TAG_STREAM_HEADER.decode("latin1")
    assert chunks[0]["size"] == 40
    assert chunks[-1]["tag"] == TAG_STREAM_END.decode("latin1")
    assert chunks[-1]["size"] == 8
    for a, b in zip(chunks, chunks[1:]):
        assert b["offset"] == a["offset"] + a["size"]
    assert chunks[-1]["offset"] + chunks[-1]["size"] == tgq.size


@pytest.mark.parametrize("path", MOVIE_FILES)
def test_sccl_equals_audio_chunk_count(path):
    tgq = TgqFile(path)
    sccl = tgq.of_tag(TAG_STREAM_CLOSE)
    audio_count = len(tgq.of_tag(TAG_AUDIO_BLOCK))
    assert len(sccl) == 1
    value = struct.unpack("<I", tgq.read_payload(sccl[0], 4))[0]
    assert value == audio_count


@pytest.mark.parametrize("path", MOVIE_FILES)
def test_first_frame_tqi_header(path):
    payload = _first_payload(path)
    assert len(payload) > TQI_HEADER_SIZE + 16
    h = frame_header(payload)
    assert h["width"] == 320
    assert h["height"] == 480
    assert 0x60 <= h["quantizer_flags"] <= 0x67
    assert h["extra"] == "14 1e 03"
    # bit 4 is the path selector in FUN_005b26f0; all shipped first frames
    # clear it.  bit 7 likewise selects the alternate IDCT path.
    assert h["flags_bit4"] == 0
    assert h["flags_bit7"] == 0


@pytest.mark.parametrize("path", MOVIE_FILES)
def test_no_raw_mpeg_start_codes_in_video(path):
    payload = _first_payload(path)
    assert count_mpeg_start_codes(payload) == 0


def test_all_movies_share_the_same_frame_geometry():
    result = [probe_file(p) for p in MOVIE_FILES]
    assert len(result) == 24
    for r in result:
        assert r["walk_exact"]
        assert r["widths"] == {320: r["frame_count"]}
        assert r["heights"] == {480: r["frame_count"]}
        # Occasional coincidental "00 00 01" byte triples occur deep inside
        # VLC bitstreams; what matters is that no frame *starts* with a valid
        # MPEG start code (tested separately via the first-frame check).
        assert r["mpeg_start_codes_total"] < r["frame_count"]
        assert r["decoder_status"].startswith("NOT_IMPLEMENTED")


def test_known_first_frame_bitstream_prefix():
    """Byte-exact anchor from BullfrogIntro.tgq frame 0."""
    path = os.path.join(MOVIES, "BullfrogIntro.tgq")
    payload = _first_payload(path)
    assert payload[:8] == bytes.fromhex("40 01 e0 01 64 14 1e 03")
    assert payload[8:24].hex(" ") == (
        "2f a5 34 f4 02 fa 0b e8 29 62 84 34 94 22 52 4a")


def test_chunks_are_the_known_ea_set():
    path = os.path.join(MOVIES, "BullfrogIntro.tgq")
    tgq = TgqFile(path)
    tags = {c["tag"] for c in tgq.chunks}
    assert tags == {
        TAG_STREAM_HEADER.decode("latin1"),
        TAG_VIDEO_FRAME.decode("latin1"),
        TAG_STREAM_CLOSE.decode("latin1"),
        TAG_AUDIO_BLOCK.decode("latin1"),
        TAG_STREAM_END.decode("latin1"),
    }
