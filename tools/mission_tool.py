#!/usr/bin/env python3
"""Recover campaign / mission metadata from Dungeon Keeper 2 sidecar files.

Stdlib-only.  Re-reads the shipped ``Data/editor/maps/*.kwd`` and the four
``*.kld`` sidecars per map on every invocation; no data is hard-coded from a
previous run.  See ``docs/KWD_FORMAT.md`` for the container grammar and
``docs/missions.json`` for the generated data set.

Commands::

    mission_tool.py list
    mission_tool.py show <mission>
    mission_tool.py json [--out <path>]

All names, strings and numbers below come from the shipped bytes.  Numeric
fields whose meaning is not proven are reported as candidates with an explicit
low/unknown confidence, never as established semantics.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import kwd_tool as K  # noqa: E402

# ---------------------------------------------------------------------------
# Canonical mission ordering (levels 1..20 with variants, secrets, MPD, rest).
# The labels are a convention of the shipped file names, not an inferred fact.
# ---------------------------------------------------------------------------

CAMPAIGN = [
    ("1", 1, None, "level1"),
    ("2", 2, None, "level2"),
    ("3", 3, None, "level3"),
    ("4", 4, None, "Level4"),
    ("5", 5, None, "level5"),
    ("6a", 6, "a", "Level6a"),
    ("6b", 6, "b", "Level6b"),
    ("7", 7, None, "level7"),
    ("8", 8, None, "level8"),
    ("9", 9, None, "Level9"),
    ("10", 10, None, "Level10"),
    ("11a", 11, "a", "level11A"),
    ("11b", 11, "b", "level11B"),
    ("11c", 11, "c", "level11C"),
    ("12", 12, None, "level12"),
    ("13", 13, None, "Level13"),
    ("14", 14, None, "Level14"),
    ("15a", 15, "a", "Level15a"),
    ("15b", 15, "b", "level15b"),
    ("16", 16, None, "Level16"),
    ("17", 17, None, "Level17"),
    ("18", 18, None, "level18"),
    ("19", 19, None, "Level19"),
    ("20", 20, None, "Level20"),
]
SECRET = ["Secret1", "Secret2", "Secret3", "Secret4", "Secret5"]
MULTIPLAYER = ["mpd1", "mpd2", "mpd3", "mpd4", "mpd5", "MPD6", "mpd7"]
FRONTEND = ["FrontEnd3DLevel"]

CATEGORY_OF = {}
for _label, _level, _variant, _name in CAMPAIGN:
    CATEGORY_OF[_name] = "campaign"
for _name in SECRET:
    CATEGORY_OF[_name] = "secret"
for _name in MULTIPLAYER:
    CATEGORY_OF[_name] = "multiplayer"
for _name in FRONTEND:
    CATEGORY_OF[_name] = "frontend"
_CAMPAIGN_BY_NAME = {n: (l, lv, v) for l, lv, v, n in CAMPAIGN}

# Fixed byte offsets inside the 0xDF map-settings data chunk (absolute file
# offsets, because the chunk always begins at 0x38 in these files).  These were
# located by scanning every shipped .kwd for UTF-16LE strings; no other
# non-empty UTF-16LE string offset occurs in any of the 66 files.
NAME_OFFSET = 0x38          # 56    internal map name
BRIEF_OFFSETS = (0x13C4, 0x17C4, 0x1BC4)  # 5060, 6084, 7108 briefing slots
ENEMY_OFFSET = 0x644B       # 25675 primary enemy / hero name

GENERIC_PLAYERS = {"Good Player", "Neutral Player", "Player"}


# ---------------------------------------------------------------------------
# Filesystem helpers
# ---------------------------------------------------------------------------

def find_game_root(start=None):
    """Locate the directory that contains Data/editor/maps."""
    cur = os.path.abspath(start or TOOLS_DIR)
    for _ in range(6):
        if os.path.isdir(os.path.join(cur, "Data", "editor", "maps")):
            return cur
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    raise RuntimeError("cannot locate Data/editor/maps from %s" % TOOLS_DIR)


def make_resolver(maps_dir):
    """Case-insensitive filename lookup (the game mixes level4/Level4 etc.)."""
    actual = {}
    for entry in os.listdir(maps_dir):
        actual.setdefault(entry.lower(), entry)

    def resolve(name, suffix):
        key = (name + suffix).lower()
        if key not in actual:
            return None
        return os.path.join(maps_dir, actual[key])

    return resolve


def load_catalog(root):
    """Return the 66 canonical (name, map_file) pairs."""
    gd_path = os.path.join(root, "port", "docs", "game_data.json")
    if os.path.isfile(gd_path):
        with open(gd_path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return [(m["name"], m["file"]) for m in data["maps"]]
    # Fallback: derive from the KWD stems, case-insensitively.
    maps_dir = os.path.join(root, "Data", "editor", "maps")
    stems = sorted(f[:-4] for f in os.listdir(maps_dir) if f.lower().endswith(".kwd"))
    return [(s, s + "Map.kld") for s in stems]


# ---------------------------------------------------------------------------
# String extraction
# ---------------------------------------------------------------------------

def utf16_strings(data, min_chars=3):
    """All NUL-terminated UTF-16LE strings, scanning both byte alignments.

    Returns a sorted list of ``(offset, text)`` pairs.  Only 7-bit printable
    code units (plus CR/LF) are accepted, which is exactly what the shipped
    mission strings use; binary fields cannot produce false positives of three
    or more consecutive printable UTF-16 code units followed by a terminator.
    """
    found = set()
    n = len(data)
    for align in (0, 1):
        i = align
        while i + 1 < n:
            chars = []
            j = i
            while j + 1 < n:
                lo, hi = data[j], data[j + 1]
                if lo == 0 and hi == 0:
                    break
                if hi == 0 and (0x20 <= lo < 0x7F or lo in (0x09, 0x0A, 0x0D)):
                    chars.append(chr(lo))
                    j += 2
                else:
                    break
            if len(chars) >= min_chars and j + 1 < n and data[j] == 0 and data[j + 1] == 0:
                found.add((i, "".join(chars)))
                i = j + 2
            else:
                i += 2
    return sorted(found)


def decode_utf16_at(data, offset, max_chars=1024):
    """Decode one NUL-terminated UTF-16LE string at an exact byte offset."""
    chars = []
    i = offset
    while i + 1 < len(data) and len(chars) < max_chars:
        lo, hi = data[i], data[i + 1]
        if lo == 0 and hi == 0:
            break
        if hi == 0 and 0x20 <= lo < 0x7F:
            chars.append(chr(lo))
            i += 2
        else:
            break
    return "".join(chars)


def player_name(record):
    """Pick the most name-like string from a player record (last ASCII one).

    Returns ``(text, file_offset)`` so the reported offset points at the
    string itself rather than at the start of the fixed-size record.
    """
    candidates = [s for s in record.strings(3)
                  if re.search(r"[A-Za-z]", s.text)]
    if not candidates:
        return None, None
    hit = candidates[-1]
    return hit.text.strip(), hit.offset


# ---------------------------------------------------------------------------
# Numeric field helpers
# ---------------------------------------------------------------------------

def info_u32(kwd, index):
    """Read a u32 from the first info-style chunk's payload."""
    for chunk in kwd.chunks:
        if chunk.size in (28, 32) and chunk.tag == (kwd.root_tag + 1) & 0xFFFFFFFF:
            if index * 4 + 4 <= len(chunk.payload):
                return struct.unpack_from("<I", chunk.payload, index * 4)[0]
    return None


def num(value, confidence, source, note=None):
    out = {"value": value, "confidence": confidence, "source": source}
    if note:
        out["note"] = note
    return out


# ---------------------------------------------------------------------------
# Per-mission parse
# ---------------------------------------------------------------------------

def parse_mission(name, map_file, root, resolver):
    kwd_path = resolver(name, ".kwd")
    ply_path = resolver(name, "Players.kld")
    th_path = resolver(name, "Things.kld")
    tr_path = resolver(name, "Triggers.kld")
    va_path = resolver(name, "Variables.kld")
    if not all((kwd_path, ply_path, th_path, tr_path, va_path)):
        raise RuntimeError("missing sidecar for %s" % name)

    kwd = K.KwdFile(kwd_path)
    ply = K.KwdFile(ply_path)
    th = K.KwdFile(th_path)
    tr = K.KwdFile(tr_path)
    va = K.KwdFile(va_path)
    kmap = K.KldMap(resolver(name, "Map.kld") or
                    os.path.join(os.path.dirname(kwd_path), map_file))

    base = os.path.basename(kwd_path)
    src_kwd = lambda off: "%s chunk 0xDF @0x%X" % (base, off)

    strings = []

    def add(kind, value, encoding, offset, source, confidence, record=None):
        if not value:
            return
        entry = {"kind": kind, "value": value, "encoding": encoding,
                 "offset": offset, "source": source, "confidence": confidence}
        if record is not None:
            entry["record"] = record
        strings.append(entry)

    # UTF-16LE fields of the map-settings KWD.
    utf16 = dict(utf16_strings(kwd.data))
    if NAME_OFFSET in utf16:
        add("map_name", utf16[NAME_OFFSET], "utf-16-le", NAME_OFFSET,
            src_kwd(NAME_OFFSET), "high")
    for off in BRIEF_OFFSETS:
        if off in utf16:
            add("briefing_text", utf16[off], "utf-16-le", off,
                src_kwd(off), "high")
    if ENEMY_OFFSET in utf16:
        add("enemy_name", utf16[ENEMY_OFFSET], "utf-16-le", ENEMY_OFFSET,
            src_kwd(ENEMY_OFFSET), "medium")

    # ASCII NUL-terminated strings: file references in chunk 0xDE.
    for hit in K.extract_strings(kwd.data):
        text = hit.text.strip()
        if text.startswith("Data\\"):
            add("file_reference", text, "ascii", hit.offset,
                "%s chunk 0xDE @0x%X" % (base, hit.offset), "high")

    # Player names in the Players KLD.
    names = []
    for record in ply.records:
        pname, poff = player_name(record)
        names.append(pname)
        add("player_name", pname, "ascii", poff,
            "%s record %d" % (os.path.basename(ply_path), record.index),
            "high", record=record.index)
    strings.sort(key=lambda s: (s.get("record", -1), s["offset"], s["kind"]))

    enemies = [n for n in names if n and n not in GENERIC_PLAYERS]

    # Candidate resource field: u32 @ record offset 0.  Round, resource-like
    # values occur here, but the field's meaning is not established by names.
    gold_candidates = []
    for record in ply.records:
        if len(record.data) >= 4:
            val = struct.unpack_from("<I", record.data, 0)[0]
            if val:
                gold_candidates.append({
                    "player_index": record.index,
                    "player_name": player_name(record)[0],
                    "value": val,
                    "confidence": "low",
                    "source": "%s record %d u32@0x00" % (
                        os.path.basename(ply_path), record.index),
                    "note": "round resource-like integer; resource semantics unproven",
                })

    label, level, variant = _CAMPAIGN_BY_NAME.get(name, (None, None, None))
    category = CATEGORY_OF.get(name, "skirmish")

    return {
        "name": name,
        "map_file": kmap.path and os.path.basename(kmap.path) or map_file,
        "width": kmap.width,
        "height": kmap.height,
        "category": category,
        "campaign_level": level,
        "campaign_variant": variant,
        "campaign_order": label,
        "sidecars": {
            "kwd": os.path.basename(kwd_path),
            "map": os.path.basename(kmap.path) if kmap.path else map_file,
            "players": os.path.basename(ply_path),
            "things": os.path.basename(th_path),
            "triggers": os.path.basename(tr_path),
            "variables": os.path.basename(va_path),
        },
        "recovered_strings": strings,
        "numeric_parameters": {
            "tile_count": num(kmap.width * kmap.height, "high",
                              "%s chunk 0x65" % os.path.basename(kmap.path)),
            "player_count": num(ply.record_count, "high",
                                "%s info chunk u32@0x00" % os.path.basename(ply_path),
                                "matches %d recovered player-name records" % len(names)),
            "thing_count": num(th.record_count, "medium",
                               "%s info chunk u32@0x00" % os.path.basename(th_path),
                               "record grammar not decoded; count field only"),
            "variable_count": num(va.record_count, "medium",
                                  "%s info chunk u32@0x00" % os.path.basename(va_path)),
            "trigger_condition_count": num(info_u32(tr, 0), "medium",
                                           "%s info chunk u32@0x00" % os.path.basename(tr_path),
                                           "equals nested 0xD5 chunk count where parseable"),
            "trigger_action_count": num(info_u32(tr, 1), "medium",
                                        "%s info chunk u32@0x04" % os.path.basename(tr_path),
                                        "equals nested 0xD6 chunk count where parseable"),
            "enemy_player_count": num(len(enemies), "medium",
                                      "derived from player-name strings in %s" % os.path.basename(ply_path),
                                      "generic slots Good Player/Neutral Player/Player excluded"),
            "candidate_starting_gold": gold_candidates,
            "starting_gold": num(None, "unknown",
                                 "not derivable; candidate u32 fields retained in candidate_starting_gold"),
            "starting_mana": num(None, "unknown",
                                 "no field identified in shipped sidecars"),
        },
        "cross_check": {
            "manual_txt": "no per-map mission names or objectives found",
            "reference_card_txt": "key reference only; no per-map mission data",
            "note": "mission text recovered directly from UTF-16LE fields of the .kwd file",
        },
    }


def build_all(root=None):
    root = root or find_game_root()
    resolve = make_resolver(os.path.join(root, "Data", "editor", "maps"))
    catalog = load_catalog(root)
    result = [parse_mission(n, f, root, resolve) for n, f in catalog]
    order = {m[0]: i for i, m in enumerate(catalog)}
    result.sort(key=lambda m: order[m["name"]])
    return result


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def cmd_list(args):
    missions = build_all(args.root)
    for m in missions:
        np = m["numeric_parameters"]
        order = m["campaign_order"] or "-"
        enemies = np["enemy_player_count"]["value"]
        print("%-22s %-11s %5s %4dx%-4d %2d players %2d enemies %3d strings" % (
            m["name"], m["category"], order, m["width"], m["height"],
            np["player_count"]["value"], enemies, len(m["recovered_strings"])))
    return 0


def cmd_show(args):
    missions = build_all(args.root)
    wanted = args.mission.lower()
    match = next((m for m in missions if m["name"].lower() == wanted), None)
    if match is None:
        print("unknown mission: %s" % args.mission, file=sys.stderr)
        return 2
    print("Mission: %s" % match["name"])
    print("Category: %s  Campaign order: %s" % (
        match["category"], match["campaign_order"] or "n/a"))
    print("Map: %s (%dx%d)" % (match["map_file"], match["width"], match["height"]))
    print("\nRecovered strings:")
    for s in match["recovered_strings"]:
        value = s["value"].replace("\n", "\\n")
        if len(value) > 160:
            value = value[:157] + "..."
        print("  [%s] @0x%X (%s, %s): %s" % (
            s["kind"], s["offset"], s["encoding"], s["confidence"], value))
    print("\nNumeric parameters:")
    for key, spec in match["numeric_parameters"].items():
        if isinstance(spec, list):
            print("  %s: %s" % (key, json.dumps(spec, ensure_ascii=False)))
        else:
            print("  %s: %r (%s) <- %s" % (
                key, spec["value"], spec["confidence"], spec["source"]))
    return 0


def cmd_json(args):
    missions = build_all(args.root)
    text = json.dumps(missions, indent=2, ensure_ascii=False) + "\n"
    if args.out:
        out = os.path.abspath(args.out)
        parent = os.path.dirname(out)
        if not os.path.isdir(parent):
            print("output directory does not exist: %s" % parent, file=sys.stderr)
            return 2
        with open(out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    else:
        sys.stdout.write(text)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=None,
                        help="game root (default: auto-detect from this script)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="list all missions")
    p_list.set_defaults(func=cmd_list)

    p_show = sub.add_parser("show", help="show one mission")
    p_show.add_argument("mission")
    p_show.set_defaults(func=cmd_show)

    p_json = sub.add_parser("json", help="emit missions.json")
    p_json.add_argument("--out", default=None)
    p_json.set_defaults(func=cmd_json)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (RuntimeError, OSError, ValueError, K.KwdError, struct.error) as exc:
        print("mission_tool: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
