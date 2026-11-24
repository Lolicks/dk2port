#!/usr/bin/env python3
"""Build the iOS runtime audio set from Dungeon Keeper 2 SDT banks.

The tool is intentionally stdlib-only for parsing/classification/manifest work.
External binaries are used only for decoding MPEG elementary streams and for
AAC encoding.  Output is deterministic for a fixed input set and fixed tool
versions, and repeated runs are no-ops via a small state cache.

Usage::

    build_audio --game-root <dir> --out <dir> [--ffmpeg <path>]
    build_audio --self-test

See docs/format-research/AUDIO_PIPELINE.md for the full specification.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from sdt_probe import SdtFile, SdtError, _wav_header  # noqa: E402

TOOL_VERSION = "1.0"
SCHEMA_NAME = "audio_manifest.schema.json"
MANIFEST_NAME = "manifest.json"
STATE_NAME = ".build_audio_state.json"
AUDIO_DIR = "audio"
CACHE_REL = AUDIO_DIR + "/.wav-cache"
STATE_REL = AUDIO_DIR + "/" + STATE_NAME
MANIFEST_REL = AUDIO_DIR + "/" + MANIFEST_NAME
AAC_BITRATE = "192k"
CRITICAL_DURATION_S = 0.6
CRITICAL_NAME_KEYWORDS = (
    "speech", "voice", "ui", "click", "menu", "button", "select",
    "beep", "alarm", "confirm", "cancel", "hint",
)
MPEG_CODECS = ("mpeg-layer1", "mpeg-layer2")


class AudioToolError(Exception):
    pass


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def which(spec):
    if not spec:
        return None
    if os.sep in spec or (os.altsep and os.altsep in spec):
        return spec if os.path.isfile(spec) and os.access(spec, os.X_OK) else None
    return shutil.which(spec)


def tool_version(exe, flag="-version"):
    if not exe:
        return None
    try:
        r = subprocess.run([exe, flag], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=10, check=False)
        text = r.stdout.decode("utf-8", "replace").splitlines()
        return text[0].strip()[:200] if text else "unknown"
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"


def safe_name(name, maxlen=48):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name)[:maxlen] or "unnamed"


def entry_id(bank, index):
    return "%s/%04d" % (bank, index)


BITRATES = {
    ("mpeg1", "mpeg-layer1"): [0, 32, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 0],
    ("mpeg2", "mpeg-layer1"): [0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 0],
    ("mpeg1", "mpeg-layer2"): [0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0],
    ("mpeg2", "mpeg-layer2"): [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 0],
}


def _mpeg_duration_from_frames(sample):
    from sdt_probe import mpeg_header_at
    blob = sample.get("_payload")
    if not blob:
        return None
    header = mpeg_header_at(blob, 0)
    if not header:
        return None
    rate = int(sample.get("sample_rate") or 0)
    if not rate:
        return None
    version = "mpeg1" if header["version_bits"] == 3 else "mpeg2"
    layer = "mpeg-layer1" if header["layer_bits"] == 3 else "mpeg-layer2"
    idx = header["bitrate_index"]
    table = BITRATES.get((version, layer), [])
    if idx >= len(table) or not table[idx]:
        return None
    bitrate = table[idx] * 1000
    padding = header["padding"]
    if layer == "mpeg-layer1":
        frame_len = ((12 * bitrate) // rate + padding) * 4
        samples_per_frame = 384
    elif version == "mpeg1":
        frame_len = (144 * bitrate) // rate + padding
        samples_per_frame = 1152
    else:
        frame_len = (72 * bitrate) // rate + padding
        samples_per_frame = 1152
    if frame_len <= 0:
        return None
    return (len(blob) / frame_len) * samples_per_frame / rate


def estimate_duration_s(sample):
    """Deterministic duration estimate from SDT metadata only."""
    rate = int(sample.get("sample_rate") or 0)
    codec = sample.get("codec")
    size = int(sample.get("data_size") or 0)
    if codec == "empty/blank" or not rate:
        return 0.0
    if codec == "pcm16le":
        return size / float(rate * 2)
    hint = int(sample.get("field_20") or 0)
    channels = 2 if codec == "mpeg-layer2" else 1
    if hint > 0:
        return hint / float(rate * 2 * channels)
    framed = _mpeg_duration_from_frames(sample)
    if framed is not None:
        return framed
    bytes_per_sec = 8000.0 if codec == "mpeg-layer1" else 14000.0
    return size / bytes_per_sec


def classify(sample):
    """Return (critical, reason, estimated_seconds).

    Classification never depends on decoder output: the same SDT bytes always
    produce the same decision on Linux and macOS.
    """
    codec = sample.get("codec")
    seconds = estimate_duration_s(sample)
    if codec == "empty/blank":
        return False, "blank", seconds
    if codec == "pcm16le":
        return True, "pcm16-source-lossless", seconds
    lowered = (sample.get("name") or "").lower()
    for word in CRITICAL_NAME_KEYWORDS:
        if word in lowered:
            return True, "name-keyword:%s" % word, seconds
    if seconds < CRITICAL_DURATION_S:
        return True, "short-duration", seconds
    return False, "music-sfx", seconds


def wav_from_pcm(pcm, rate, channels=1):
    return _wav_header(len(pcm), rate, channels) + pcm


def parse_wav(data):
    """Extract 16-bit PCM payload plus rate/channels from a RIFF/WAVE file."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise AudioToolError("decoder did not produce a RIFF/WAVE file")
    pos = 12
    fmt = None
    pcm = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt " and len(body) >= 16 and fmt is None:
            tag, channels, rate, _byte_rate, _align, bits = struct.unpack_from(
                "<HHIIHH", body, 0)
            fmt = (tag, channels, rate, bits)
        elif cid == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    if fmt is None or pcm is None:
        raise AudioToolError("WAVE is missing fmt or data chunk")
    tag, channels, rate, bits = fmt
    if tag != 1 or bits != 16:
        raise AudioToolError("expected 16-bit PCM WAVE, got tag=%d bits=%d" % (tag, bits))
    if not 1 <= channels <= 8:
        raise AudioToolError("invalid WAVE channel count %d" % channels)
    return pcm, int(rate), int(channels)


def canonical_wav(data):
    """Return (bytes, rate, channels, duration_ms) for a decoded WAVE."""
    pcm, rate, channels = parse_wav(data)
    frames = len(pcm) // (channels * 2)
    duration_ms = int(round(frames * 1000.0 / rate)) if rate else 0
    return wav_from_pcm(pcm, rate, channels), rate, channels, duration_ms


def _run(cmd, timeout=120):
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise AudioToolError("%s failed: %s" % (os.path.basename(cmd[0]), exc))
    if r.returncode != 0:
        detail = r.stderr.decode("utf-8", "replace").strip().splitlines()
        raise AudioToolError("%s exited %d: %s" % (
            os.path.basename(cmd[0]), r.returncode,
            detail[-1] if detail else "no stderr"))
    return r


def ffmpeg_decode_cmd(ffmpeg, src, dst):
    return [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
            "-i", src, "-vn", "-map_metadata", "-1", "-c:a", "pcm_s16le", dst]


def mpg123_decode_cmd(mpg123, src, dst):
    return [mpg123, "-q", "-w", dst, src]


def decode_mpeg(raw_path, wav_path, ffmpeg=None, mpg123=None):
    try:
        if ffmpeg:
            _run(ffmpeg_decode_cmd(ffmpeg, raw_path, wav_path))
            return "ffmpeg"
        if mpg123:
            _run(mpg123_decode_cmd(mpg123, raw_path, wav_path))
            return "mpg123"
    except AudioToolError:
        return None
    raise AudioToolError("no MPEG decoder available (install ffmpeg or mpg123)")


def ffmpeg_aac_cmd(ffmpeg, src, dst):
    return [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
            "-i", src, "-vn", "-map_metadata", "-1", "-c:a", "aac",
            "-b:a", AAC_BITRATE, "-movflags", "+faststart", dst]


def afconvert_aac_cmd(afconvert, src, dst):
    return [afconvert, "-f", "m4af", "-d", "aac", "-b", "192000",
            "-q", "127", src, dst]


def encode_aac(wav_path, out_path, ffmpeg=None, afconvert=None):
    if ffmpeg:
        _run(ffmpeg_aac_cmd(ffmpeg, wav_path, out_path), timeout=300)
        return "ffmpeg-aac"
    if afconvert:
        _run(afconvert_aac_cmd(afconvert, wav_path, out_path), timeout=300)
        return "afconvert-aac"
    raise AudioToolError("no AAC encoder available (install ffmpeg or use macOS afconvert)")


def validate_manifest(manifest):
    errors = []
    if not isinstance(manifest, dict):
        return ["manifest is not an object"]
    if manifest.get("schema") != SCHEMA_NAME:
        errors.append("schema is not %s" % SCHEMA_NAME)
    if manifest.get("version") != 1:
        errors.append("version is not 1")
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        return errors + ["entries is not a list"]
    seen = set()
    for i, e in enumerate(entries):
        label = "entries[%d]" % i
        for key in ("id", "bank", "index", "codec", "rate", "path",
                    "wav_source", "critical"):
            if key not in e:
                errors.append("%s missing %s" % (label, key))
        if not re.match(r"^[^/]+/[0-9]{4}$", str(e.get("id", ""))):
            errors.append("%s bad id %r" % (label, e.get("id")))
        if e.get("id") in seen:
            errors.append("%s duplicate id %r" % (label, e.get("id")))
        seen.add(e.get("id"))
        if e.get("codec") not in ("aac", "pcm16", "blank"):
            errors.append("%s bad codec %r" % (label, e.get("codec")))
        if not isinstance(e.get("index"), int) or e["index"] < 0:
            errors.append("%s bad index %r" % (label, e.get("index")))
        if not isinstance(e.get("rate"), int) or e["rate"] < 0:
            errors.append("%s bad rate %r" % (label, e.get("rate")))
        if not isinstance(e.get("critical"), bool):
            errors.append("%s critical is not bool" % label)
        for key in ("path", "wav_source"):
            if e.get(key) is not None and not isinstance(e.get(key), str):
                errors.append("%s bad %s" % (label, key))
    return errors


def _load_json(path, default):
    if not os.path.isfile(path):
        return default
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return default


def _dump_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(obj, fh, indent=2, sort_keys=True, ensure_ascii=True)
        fh.write("\n")
    os.replace(tmp, path)


def _file_stem(bank, sample, ext):
    return "s%04d_%s.%s" % (sample["index"], safe_name(sample["name"]), ext)


def build(game_root, out, ffmpeg=None, afconvert=None, mpg123=None,
          keep_wavs=False, limit=None, force=False):
    game_root = os.path.abspath(game_root)
    out = os.path.abspath(out)
    sdt_dir = os.path.join(game_root, "Data", "Sound", "Sfx", "Global")
    if not os.path.isdir(sdt_dir):
        raise AudioToolError("SDT directory not found: %s" % sdt_dir)

    ffmpeg = which(ffmpeg) or which("ffmpeg")
    mpg123 = which(mpg123) or which("mpg123")
    afconvert = which(afconvert) or which("afconvert")
    aac_encoder = ffmpeg or afconvert
    mpeg_decoder = ffmpeg or mpg123

    banks = []
    for name in sorted(os.listdir(sdt_dir)):
        if not name.lower().endswith(".sdt"):
            continue
        if limit is not None and len(banks) >= limit:
            break
        bank = os.path.splitext(name)[0]
        sdt = SdtFile(os.path.join(sdt_dir, name))
        banks.append((bank, sdt))

    descriptors = []
    for bank, sdt in banks:
        for sample in sdt.samples:
            descriptors.append((bank, sdt, sample))

    need_aac = any(classify(s)[0] is False and s["codec"] in MPEG_CODECS
                   for _b, _s, s in descriptors)
    need_decode = any(s["codec"] in MPEG_CODECS for _b, _s, s in descriptors)
    if need_aac and not aac_encoder:
        raise AudioToolError(
            "AAC encoder required for %d non-critical samples but none found; "
            "pass --ffmpeg <path> or run on macOS with afconvert" %
            sum(1 for _b, _s, s in descriptors
                if classify(s)[0] is False and s["codec"] in MPEG_CODECS))
    if need_decode and not mpeg_decoder:
        raise AudioToolError("MPEG samples present but no decoder found")

    os.makedirs(out, exist_ok=True)
    os.makedirs(os.path.join(out, AUDIO_DIR), exist_ok=True)
    state_path = os.path.join(out, STATE_REL)
    state = _load_json(state_path, {"version": 1, "entries": {}})
    if not isinstance(state.get("entries"), dict):
        state = {"version": 1, "entries": {}}
    state_entries = state["entries"]
    used_tools = set(state.get("used", []))

    cache_root = os.path.join(out, CACHE_REL)
    if keep_wavs:
        os.makedirs(cache_root, exist_ok=True)
    count = {"aac": 0, "pcm16": 0, "blank": 0, "critical": 0}
    encoders_used = set()
    audio_bytes = 0
    entries = []

    for bank, sdt, sample in descriptors:
        ident = entry_id(bank, sample["index"])
        base = _file_stem(bank, sample, "bin")
        wav_rel = "%s/%s/%s" % (CACHE_REL, bank,
                                os.path.splitext(base)[0] + ".wav")
        wav_path = os.path.join(out, wav_rel)
        source_bytes = sdt.payload(sample)
        sample["_payload"] = source_bytes
        source_sha = sha256_bytes(source_bytes)
        cached = state_entries.get(ident) if not force else None
        if (cached and cached.get("source_sha256") == source_sha
                and cached.get("output_sha256")
                and cached.get("entry", {}).get("path") is not None
                and os.path.isfile(os.path.join(out, cached["entry"]["path"]))
                and sha256_file(os.path.join(out, cached["entry"]["path"]))
                == cached["output_sha256"]):
            entry = cached["entry"]
        else:
            if sample["codec"] == "empty/blank":
                entry = {
                    "id": ident, "bank": bank, "index": sample["index"],
                    "name": sample["name"], "codec": "blank",
                    "source_codec": sample["codec"], "rate": sample["sample_rate"],
                    "channels": 0, "duration_ms": 0,
                    "path": None, "wav_source": None, "critical": False,
                    "reason": "blank", "bytes": 0,
                    "source_sha256": source_sha, "output_sha256": None,
                }
            else:
                os.makedirs(os.path.dirname(wav_path), exist_ok=True)
                if sample["codec"] == "pcm16le":
                    wav_bytes = wav_from_pcm(source_bytes,
                                             sample["sample_rate"], 1)
                else:
                    raw_dir = os.path.join(out, CACHE_REL, bank)
                    os.makedirs(raw_dir, exist_ok=True)
                    raw_path = os.path.join(raw_dir, base)
                    with open(raw_path, "wb") as fh:
                        fh.write(source_bytes)
                    tmp_wav = raw_path + ".decoded.wav"
                    used = decode_mpeg(raw_path, tmp_wav, ffmpeg, mpg123)
                    if used is None:
                        entry = {
                            "id": ident, "bank": bank, "index": sample["index"],
                            "name": sample["name"], "codec": "blank",
                            "source_codec": sample["codec"],
                            "rate": sample["sample_rate"], "channels": 0,
                            "duration_ms": 0, "path": None, "wav_source": None,
                            "critical": False, "reason": "decode_failed",
                            "bytes": 0, "source_sha256": source_sha,
                            "output_sha256": None,
                        }
                        entries.append(entry)
                        count["blank"] = count.get("blank", 0) + 1
                        continue
                    encoders_used.add(used)
                    with open(tmp_wav, "rb") as fh:
                        decoded = fh.read()
                    wav_bytes, rate, channels, duration_ms = canonical_wav(decoded)
                    os.remove(tmp_wav)
                    os.remove(raw_path)
                if sample["codec"] == "pcm16le":
                    _w, rate, channels, duration_ms = canonical_wav(wav_bytes)
                critical, reason, estimate = classify(sample)
                if critical:
                    ext, out_codec = "wav", "pcm16"
                else:
                    ext, out_codec = "m4a", "aac"
                out_rel = "%s/%s/%s" % (AUDIO_DIR, bank,
                                        os.path.splitext(base)[0] + "." + ext)
                out_path = os.path.join(out, out_rel)
                os.makedirs(os.path.dirname(out_path), exist_ok=True)
                with open(wav_path, "wb") as fh:
                    fh.write(wav_bytes)
                if critical:
                    with open(out_path, "wb") as fh:
                        fh.write(wav_bytes)
                else:
                    used = encode_aac(wav_path, out_path, ffmpeg, afconvert)
                    encoders_used.add(used)
                if not keep_wavs:
                    os.remove(wav_path)
                output_sha = sha256_file(out_path)
                entry = {
                    "id": ident, "bank": bank, "index": sample["index"],
                    "name": sample["name"], "codec": out_codec,
                    "source_codec": sample["codec"], "rate": rate,
                    "channels": channels, "duration_ms": duration_ms,
                    "estimated_duration_ms": int(round(estimate * 1000)),
                    "path": out_rel,
                    "wav_source": wav_rel,
                    "critical": critical, "reason": reason,
                    "bytes": os.path.getsize(out_path),
                    "source_sha256": source_sha, "output_sha256": output_sha,
                }
            state_entries[ident] = {
                "source_sha256": source_sha,
                "output_sha256": entry.get("output_sha256"),
                "entry": entry,
            }
        count[entry["codec"]] = count.get(entry["codec"], 0) + 1
        if entry["critical"]:
            count["critical"] += 1
        if entry.get("bytes"):
            audio_bytes += entry["bytes"]
        entries.append(entry)
    used_tools.update(encoders_used)

    if not keep_wavs and os.path.isdir(cache_root):
        shutil.rmtree(cache_root, ignore_errors=True)

    manifest = {
        "schema": SCHEMA_NAME,
        "version": 1,
        "generator": "tools/build_audio.py %s" % TOOL_VERSION,
        "source": {
            "game_root": game_root,
            "sdt_dir": "Data/Sound/Sfx/Global",
            "banks": len(banks),
            "samples": len(descriptors),
        },
        "policy": {
            "aac_bitrate": AAC_BITRATE,
            "critical_duration_s": CRITICAL_DURATION_S,
            "critical_name_keywords": list(CRITICAL_NAME_KEYWORDS),
        },
        "tools": {
            "mpeg_decoder": {"path": mpeg_decoder, "version": tool_version(mpeg_decoder)}
            if mpeg_decoder else None,
            "ffmpeg": {"path": ffmpeg, "version": tool_version(ffmpeg)} if ffmpeg else None,
            "afconvert": {"path": afconvert, "version": tool_version(afconvert, "-h")}
            if afconvert else None,
            "used": sorted(used_tools),
        },
        "counts": count,
        "total_bytes": audio_bytes,
        "wav_cache_retained": bool(keep_wavs),
        "entries": entries,
    }
    errors = validate_manifest(manifest)
    if errors:
        raise AudioToolError("manifest validation failed: %s" % "; ".join(errors[:5]))

    _dump_json(os.path.join(out, MANIFEST_REL), manifest)
    state["version"] = 1
    state["used"] = sorted(used_tools)
    _dump_json(state_path, state)
    return {
        "banks": len(banks), "samples": len(descriptors), "counts": count,
        "total_bytes": audio_bytes,
        "manifest": os.path.join(out, MANIFEST_REL),
        "state": state_path,
        "tools": sorted(encoders_used),
    }


def _synth_sdt(path, samples):
    offsets = []
    blobs = []
    pos = 4 + 4 * len(samples)
    for s in samples:
        offsets.append(pos)
        name = s["name"].encode("latin1")[:16].ljust(16, b"\0")
        header = (struct.pack("<II", 40, len(s["payload"])) + name
                  + struct.pack("<H", s["rate"]) + struct.pack("<H", s["fmt"])
                  + struct.pack("<I", 0) + struct.pack("<I", s.get("hint", 0))
                  + struct.pack("<I", 0))
        assert len(header) == 40
        blobs.append(header + s["payload"])
        pos += len(blobs[-1])
    with open(path, "wb") as fh:
        fh.write(struct.pack("<I", len(samples)))
        fh.write(struct.pack("<%dI" % len(samples), *offsets))
        for blob in blobs:
            fh.write(blob)


def self_test():
    checks = 0

    def ok(cond, label):
        nonlocal checks
        if not cond:
            raise AssertionError("self-test failed: %s" % label)
        checks += 1

    pcm_short = struct.pack("<h", 0) * 100
    pcm_long = struct.pack("<h", 0) * 4000
    blank = {"name": "Blank", "rate": 22050, "fmt": 0x0010,
             "hint": 0, "payload": b""}
    s_ui = {"name": "UI_Click", "rate": 22050, "fmt": 0x0210,
            "hint": len(pcm_short), "payload": pcm_short}
    s_speech = {"name": "speech_intro", "rate": 22050, "fmt": 0x0210,
                "hint": len(pcm_long), "payload": pcm_long}

    mpeg_like = {"name": "Track_3", "codec": "mpeg-layer1", "sample_rate": 22050,
                 "data_size": 200000, "field_20": 882000}
    short_mpeg = dict(mpeg_like, field_20=4410)
    speech_mpeg = dict(mpeg_like, name="voice_speech_01")
    ok(classify(mpeg_like)[:2] == (False, "music-sfx")
       and classify(mpeg_like)[2] > CRITICAL_DURATION_S, "long MPEG non-critical")
    ok(classify(short_mpeg)[0] is True and classify(short_mpeg)[1] == "short-duration",
       "short MPEG critical")
    ok(classify(speech_mpeg)[0] is True and classify(speech_mpeg)[1].startswith("name-keyword"),
       "speech keyword critical")
    ok(classify({"name": "Blank", "codec": "empty/blank", "sample_rate": 22050,
                 "data_size": 0, "field_20": 0})[1] == "blank", "blank classification")

    pcm = struct.pack("<h", 1234) * 8000
    wav = wav_from_pcm(pcm, 22050, 1)
    ok(wav[:4] == b"RIFF" and wav[8:12] == b"WAVE", "canonical WAV header")
    canon, rate, channels, ms = canonical_wav(wav)
    ok(canon == wav and rate == 22050 and channels == 1 and ms == 363,
       "canonical WAV roundtrip")
    junk = b"JUNK" + struct.pack("<I", 3) + b"abc\x00"
    noisy = wav[:12] + junk + wav[12:]
    ok(canonical_wav(noisy)[0] == wav, "WAV with extra chunk")

    aac_cmd = ffmpeg_aac_cmd("/usr/bin/ffmpeg", "in.wav", "out.m4a")
    ok(all(x in aac_cmd for x in ("-c:a", "aac", "-b:a", "192k")), "ffmpeg AAC command")
    af_cmd = afconvert_aac_cmd("/usr/bin/afconvert", "in.wav", "out.m4a")
    ok(all(x in af_cmd for x in ("-d", "aac", "-b", "192000")), "afconvert AAC command")
    ok("pcm_s16le" in ffmpeg_decode_cmd("ffmpeg", "a.mp1", "a.wav"), "ffmpeg decode command")

    with tempfile.TemporaryDirectory(prefix="build_audio_selftest_") as td:
        root = os.path.join(td, "game")
        sdir = os.path.join(root, "Data", "Sound", "Sfx", "Global")
        os.makedirs(sdir)
        _synth_sdt(os.path.join(sdir, "TestBank.sdt"), [blank, s_ui, s_speech])
        out = os.path.join(td, "out")
        stats = build(root, out)
        ok(stats["samples"] == 3 and stats["counts"]["pcm16"] == 2
           and stats["counts"]["blank"] == 1 and stats["counts"]["critical"] == 2,
           "end-to-end counts")
        with open(os.path.join(out, MANIFEST_REL), "rb") as fh:
            first = fh.read()
        manifest = json.loads(first.decode("utf-8"))
        ok(not validate_manifest(manifest), "manifest schema validation")
        ids = [e["id"] for e in manifest["entries"]]
        ok(ids == ["TestBank/0000", "TestBank/0001", "TestBank/0002"], "deterministic ids")
        ok(manifest["entries"][1]["path"] == "audio/TestBank/s0001_UI_Click.wav",
           "deterministic output path")
        ok(manifest["entries"][0]["path"] is None, "blank has no artifact")
        stats2 = build(root, out)
        ok(stats2["samples"] == 3 and stats2["counts"] == stats["counts"],
           "idempotent re-run counts")
        with open(os.path.join(out, MANIFEST_REL), "rb") as fh:
            second = fh.read()
        ok(first == second, "idempotent manifest bytes")

    orig_decode, orig_encode = decode_mpeg, encode_aac

    def fake_decode(raw, wav, ffmpeg=None, mpg123=None):
        with open(wav, "wb") as fh:
            fh.write(wav_from_pcm(struct.pack("<h", 0) * 22050, 22050, 1))
        return "fake-decoder"

    def fake_encode(wav, out, ffmpeg=None, afconvert=None):
        with open(out, "wb") as fh:
            fh.write(b"FAKEAAC" + sha256_file(wav).encode("ascii")[:16])
        return "fake-aac"

    globals()["decode_mpeg"] = fake_decode
    globals()["encode_aac"] = fake_encode
    try:
        with tempfile.TemporaryDirectory(prefix="build_audio_aac_") as td:
            root = os.path.join(td, "game")
            sdir = os.path.join(root, "Data", "Sound", "Sfx", "Global")
            os.makedirs(sdir)
            music = {"name": "Track_3", "rate": 22050, "fmt": 0x2410,
                     "hint": 441000, "payload": b"\xff\xf7\x42\xc0" * 100}
            _synth_sdt(os.path.join(sdir, "Music.sdt"), [music])
            out = os.path.join(td, "out")
            build(root, out, ffmpeg="/bin/true")
            with open(os.path.join(out, MANIFEST_REL), "rb") as fh:
                aac_first = fh.read()
            aac_manifest = json.loads(aac_first.decode("utf-8"))
            aac_entry = aac_manifest["entries"][0]
            ok(aac_entry["codec"] == "aac" and aac_entry["critical"] is False,
               "non-critical MPEG encoded as AAC")
            ok(aac_entry["path"] == "audio/Music/s0000_Track_3.m4a",
               "AAC output path")
            ok(aac_entry["wav_source"].startswith("audio/.wav-cache/Music/"),
               "wav_source provenance recorded")
            ok(os.path.isfile(os.path.join(out, aac_entry["path"])), "AAC artifact exists")
            build(root, out, ffmpeg="/bin/true")
            with open(os.path.join(out, MANIFEST_REL), "rb") as fh:
                ok(aac_first == fh.read(), "AAC path idempotent")
    finally:
        globals()["decode_mpeg"] = orig_decode
        globals()["encode_aac"] = orig_encode

    print("SELF-TEST OK: %d checks" % checks)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game-root", help="directory containing Data/Sound/Sfx/Global")
    ap.add_argument("--out", help="Assets directory; writes audio/ and audio/manifest.json")
    ap.add_argument("--ffmpeg", help="explicit ffmpeg path (overrides PATH)")
    ap.add_argument("--afconvert", help="explicit afconvert path (macOS)")
    ap.add_argument("--mpg123", help="explicit mpg123 path (Linux decoder fallback)")
    ap.add_argument("--keep-wavs", action="store_true",
                    help="retain intermediate WAVs under .wav-cache")
    ap.add_argument("--limit", type=int, help="process only the first N SDT banks")
    ap.add_argument("--force", action="store_true", help="ignore the state cache")
    ap.add_argument("--self-test", action="store_true", help="run built-in tests and exit")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.game_root or not args.out:
        ap.error("--game-root and --out are required (or use --self-test)")

    try:
        stats = build(args.game_root, args.out, ffmpeg=args.ffmpeg,
                      afconvert=args.afconvert, mpg123=args.mpg123,
                      keep_wavs=args.keep_wavs, limit=args.limit, force=args.force)
    except (AudioToolError, SdtError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    print("banks=%d samples=%d aac=%d pcm16=%d blank=%d critical=%d bytes=%d" % (
        stats["banks"], stats["samples"], stats["counts"]["aac"],
        stats["counts"]["pcm16"], stats["counts"]["blank"],
        stats["counts"]["critical"], stats["total_bytes"]))
    print("manifest: %s" % stats["manifest"])
    if stats["tools"]:
        print("encoders/decoders used: %s" % ", ".join(stats["tools"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
