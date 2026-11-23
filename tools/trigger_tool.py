#!/usr/bin/env python3
"""Decode Dungeon Keeper 2 ``*Triggers.kld`` trigger chains.

Pure Python 3 standard library.  Every numeric field is read from the shipped
bytes; enum names, field layouts and link semantics come from the two format
authorities cited in ``docs/format-research/TRIGGERS_FORMAT.md``:

  * ``research/dk2-tools/Formats/Level/triggers_struct.bt`` (ArchDemons)
  * ``research/OpenKeeper/.../convert/map/KwdFile.java`` (readTriggers)

Commands::

    trigger_tool.py list <map>
    trigger_tool.py json <map>
    trigger_tool.py catalog [--out <path>]

``<map>`` is a mission name, a ``*Triggers.kld`` filename, or a path.  Use
``--root`` to point at the game root (default: auto-detect from this script).
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import mission_tool as M  # noqa: E402

SCHEMA_VERSION = 1
ROOT_TAG = 0xD2
INFO_TAG = 0xD3
DATA_TAG = 0xD4
CONDITION_TAG = 213
ACTION_TAG = 214
RECORD_SIZE = 24
PAYLOAD_SIZE = 16
TDATA_SIZE = 8

ACTION_TYPES = {
    1: "CREATE_CREATURE", 2: "CREATE_PORTAL_GEM", 3: "SEND_TO_AP",
    4: "DISPLAY_OBJECTIVE", 5: "INFORMATION", 6: "MAKE", 7: "FLAG",
    8: "INITIALIZE_TIMER", 9: "FLASH_BUTTON", 10: "WIN_GAME",
    11: "LOSE_GAME", 12: "WIN_SUBOBJECTIVE", 13: "LOSE_SUBOBJECTIVE",
    14: "CREATE_HERO_PARTY", 15: "SET_OBJECTIVE", 16: "FLASH_ACTION_POINT",
    17: "REVEAL_ACTION_POINT", 18: "SET_ALLIANCE", 19: "MODIFY_CREATURE_POOL",
    20: "ATTACH_PORTAL_GEM", 21: "FORCE_FIRST_PERSON",
    22: "ALTER_TERRAIN_TYPE", 23: "SET_TIME_LIMIT", 24: "PLAY_SPEECH",
    25: "DISPLAY_TEXT_STRING", 26: "ZOOM_TO_ACTION_POINT",
    27: "ROTATE_AROUND_ACTION_POINT", 28: "GENERATE_CREATURE",
    29: "MAKE_HUNGRY", 30: "SHOW_HEALTH_FLOWER", 31: "FOLLOW_CAMERA_PATH",
    32: "COLLAPSE_HERO_GATE", 33: "ALTER_SPEED", 34: "SET_MUSIC_LEVEL",
    35: "REMOVE_FROM_MAP", 36: "SET_FIGHT_FLAG", 37: "SET_PORTAL_STATUS",
    38: "SET_WIDESCREEN_MODE", 39: "CREATE_DOOR", 40: "CREATE_TRAP",
    41: "CREATE_EFFECT", 42: "MAKE_OBJECTIVE", 43: "ZOOM_TO",
    44: "SET_CREATURE_MOODS", 45: "SET_SYSTEM_MESSAGES",
    46: "DISPLAY_SLAB_OWNER", 47: "DISPLAY_NEXT_ROOM_TYPE",
    48: "TOGGLE_EFFECT_GENERATOR", 49: "CHANGE_ROOM_OWNER",
    50: "SET_SLAPS_LIMIT", 51: "SET_TIMER_SPEECH",
}

TARGET_TYPES = {
    0: "NONE", 1: "FLAG", 2: "TIMER", 3: "CREATURE_CREATED",
    4: "CREATURE_KILLED", 5: "CREATURE_SLAPPED", 6: "CREATURE_ATTACKED",
    7: "CREATURE_IMPRISONED", 8: "CREATURE_TORTURED",
    9: "CREATURE_CONVERTED", 10: "CREATURE_CLAIMED", 11: "CREATURE_ANGRY",
    12: "CREATURE_AFRAID", 13: "CREATURE_STEALS", 14: "CREATURE_LEAVES",
    15: "CREATURE_STUNNED", 16: "CREATURE_DYING", 17: "DOOR_DESTROYED",
    18: "OBJECT_CLAIMED", 19: "PLAYER_CREATURES",
    20: "PLAYER_HAPPY_CREATURES", 21: "PLAYER_ANGRY_CREATURES",
    22: "PLAYER_CREATURES_KILLED", 23: "PLAYER_KILLS_CREATURES",
    24: "PLAYER_ROOM_SLABS", 25: "PLAYER_ROOMS", 26: "PLAYER_ROOM_SIZE",
    27: "PLAYER_DOORS", 28: "PLAYER_TRAPS", 29: "PLAYER_KEEPER_SPELL",
    30: "PLAYER_GOLD", 31: "PLAYER_GOLD_MINED", 32: "PLAYER_MANA",
    33: "PLAYER_DESTROYS", 34: "LEVEL_TIME", 35: "LEVEL_CREATURES",
    36: "CREATURE_HEALTH", 37: "CREATURE_GOLD_HELD", 38: "NOT_USED",
    39: "AP_CONGREGATE_IN", 40: "AP_CLAIM_PART_OF", 41: "AP_CLAIM_ALL_OF",
    42: "AP_SLAB_TYPES", 43: "PARTY_CREATED", 44: "PARTY_MEMBERS_KILLED",
    45: "PARTY_MEMBERS_CAPTURED", 46: "LEVEL_PAY_DAY",
    47: "PLAYER_KILLED", 48: "PLAYER_PORTAL_GEM_IN_HAND",
    49: "CREATURE_EXPERIENCE_LEVEL", 50: "CREATURE_ENTERS_AP",
    51: "PARTY_ENTERS_AP", 52: "PLAYER_CREATURES_AT_LEVEL",
    53: "GUI_BUTTON_PRESSED", 54: "CREATURE_HUNGER_SATED",
    55: "CREATURE_PICKS_UP_PORTAL_GEM", 56: "PLAYER_DUNGEON_BREACHED",
    57: "PLAYER_ENEMY_BREACHED", 58: "PLAYER_CREATURE_PICKED_UP",
    59: "PLAYER_CREATURE_DROPPED", 60: "PLAYER_CREATURE_SLAPPED",
    61: "PLAYER_CREATURE_SACKED", 62: "AP_TAG_PART_OF",
    63: "CREATURE_SACKED", 64: "PARTY_MEMBERS_INCAPACITATED",
    65: "CREATURE_PICKED_UP", 66: "PLAYER_CREATURE_INTRODUCED",
    67: "LEVEL_PLAYED", 68: "PLAYER_ROOM_FURNITURE", 69: "AP_TAG_ALL_OF",
    70: "AP_POSESSED_CREATURE_ENTERS", 71: "PLAYER_SLAPS",
    72: "GUI_TRANSITION_ENDS", 73: "PLAYER_CREATURES_GROUPED",
    74: "PLAYER_CREATURES_DYING", 75: "PLAYER_GEMS_COLLECTED",
}

COMPARISON_TYPES = {
    0: "NONE", 1: "LESS_THAN", 2: "LESS_OR_EQUAL_TO", 3: "EQUAL_TO",
    4: "GREATER_THAN", 5: "GREATER_OR_EQUAL_TO", 6: "NOT_EQUAL_TO",
}

MAKE_TYPES = {
    0: "MISC_BUTTON", 1: "ROOM", 2: "CREATURE", 3: "DOOR", 4: "TRAP",
    5: "KEEPER_SPELL",
}

BUTTON_TYPES = {
    255: "SELL_ROOM", 253: "ZOOM_IN_OUT", 248: "CREATURE_TAB",
    247: "GUI_MIN_MAX", 246: "HORNY_TALISMAN", 245: "MPD_TOOLBOX",
    244: "MPD_SINGLE_INVASION", 243: "GOLD_VALUE", 242: "MANA_VALUE",
    241: "MPD_CONTINUAL_INVASION",
}

FLAG_BITS = [
    (0x002, "VALUE"), (0x004, "TARGET"), (0x008, "EQUAL"),
    (0x010, "PLUS"), (0x020, "MINUS"),
]

ENUM_TABLES = {
    "comparison": COMPARISON_TYPES,
    "make_type": MAKE_TYPES,
    "button_type": BUTTON_TYPES,
}


class TriggerError(Exception):
    """Raised when a Triggers.kld file does not follow the expected layout."""


def _parse_layout(spec, source):
    fields = []
    total = 0
    for token in spec.split():
        name, size_text = token.split(":")
        size = int(size_text)
        if name == "-":
            fields.append((None, "null", None, size))
        else:
            enum = None
            if "@" in name:
                name, enum = name.split("@", 1)
                if not enum:
                    enum = name
            fields.append((name, "u8" if size == 1 else "u16" if size == 2
                           else "u32", enum, size))
        total += size
    if total != TDATA_SIZE:
        raise TriggerError("layout does not occupy %d bytes: %r" %
                           (TDATA_SIZE, spec))
    return tuple(fields), source


GENERIC_LAYOUT_SPECS = {
    39: ("comparison@:1 player_id:1 target_id:1 target_type:1 value:4",
         "KwdFile.java:2404-2411"),
    70: ("comparison@:1 player_id:1 target_id:1 target_type:1 value:4",
         "KwdFile.java:2404-2411"),
    42: ("comparison@:1 player_id:1 terrain_id:1 -:1 value:4",
         "KwdFile.java:2413-2419"),
    62: ("comparison@:1 player_id:1 -:2 value:4", "KwdFile.java:2421-2431"),
    69: ("comparison@:1 player_id:1 -:2 value:4", "KwdFile.java:2421-2431"),
    40: ("comparison@:1 player_id:1 -:2 value:4", "KwdFile.java:2421-2431"),
    41: ("comparison@:1 player_id:1 -:2 value:4", "KwdFile.java:2421-2431"),
    56: ("player_id:1 -:7", "KwdFile.java:2433-2437"),
    57: ("player_id:1 -:7", "KwdFile.java:2433-2437"),
    47: ("player_id:1 -:3 value:4", "KwdFile.java:2439-2443"),
    58: ("creature_id:1 -:7", "KwdFile.java:2445-2450"),
    60: ("creature_id:1 -:7", "KwdFile.java:2445-2450"),
    61: ("creature_id:1 -:7", "KwdFile.java:2445-2450"),
    59: ("creature_id:1 room_id:1 -:6", "KwdFile.java:2452-2456"),
    19: ("comparison@:1 creature_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2458-2465"),
    20: ("comparison@:1 creature_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2458-2465"),
    21: ("comparison@:1 creature_id:1 -:2 value:4",
         "KwdFile.java:2467-2472"),
    22: ("comparison@:1 target_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2474-2481"),
    23: ("comparison@:1 target_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2474-2481"),
    25: ("comparison@:1 room_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2483-2492"),
    24: ("comparison@:1 room_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2483-2492"),
    26: ("comparison@:1 room_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2483-2492"),
    68: ("comparison@:1 room_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2483-2492"),
    27: ("comparison@:1 target_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2494-2503"),
    28: ("comparison@:1 target_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2494-2503"),
    29: ("comparison@:1 target_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2494-2503"),
    33: ("comparison@:1 target_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2494-2503"),
    71: ("comparison@:1 -:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2505-2517"),
    30: ("comparison@:1 -:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2505-2517"),
    31: ("comparison@:1 -:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2505-2517"),
    32: ("comparison@:1 -:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2505-2517"),
    73: ("comparison@:1 -:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2505-2517"),
    74: ("comparison@:1 -:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2505-2517"),
    52: ("comparison@:1 target_id:1 flag:1 player_id:1 value:4",
         "KwdFile.java:2519-2526"),
    46: ("-:8", "KwdFile.java:2528-2549"),
    4: ("-:8", "KwdFile.java:2528-2549"),
    5: ("-:8", "KwdFile.java:2528-2549"),
    6: ("-:8", "KwdFile.java:2528-2549"),
    7: ("-:8", "KwdFile.java:2528-2549"),
    8: ("-:8", "KwdFile.java:2528-2549"),
    9: ("-:8", "KwdFile.java:2528-2549"),
    10: ("-:8", "KwdFile.java:2528-2549"),
    11: ("-:8", "KwdFile.java:2528-2549"),
    12: ("-:8", "KwdFile.java:2528-2549"),
    13: ("-:8", "KwdFile.java:2528-2549"),
    14: ("-:8", "KwdFile.java:2528-2549"),
    15: ("-:8", "KwdFile.java:2528-2549"),
    16: ("-:8", "KwdFile.java:2528-2549"),
    54: ("-:8", "KwdFile.java:2528-2549"),
    55: ("-:8", "KwdFile.java:2528-2549"),
    63: ("-:8", "KwdFile.java:2528-2549"),
    65: ("-:8", "KwdFile.java:2528-2549"),
    43: ("-:8", "KwdFile.java:2528-2549"),
    72: ("-:8", "KwdFile.java:2528-2549"),
    3: ("-:4 value:4", "KwdFile.java:2551-2554"),
    67: ("comparison@:1 -:3 value:4", "KwdFile.java:2556-2566"),
    45: ("comparison@:1 -:3 value:4", "KwdFile.java:2556-2566"),
    49: ("comparison@:1 -:3 value:4", "KwdFile.java:2556-2566"),
    37: ("comparison@:1 -:3 value:4", "KwdFile.java:2556-2566"),
    36: ("comparison@:1 -:3 value:4", "KwdFile.java:2556-2566"),
    34: ("comparison@:1 -:3 value:4", "KwdFile.java:2556-2566"),
    35: ("comparison@:1 -:3 value:4", "KwdFile.java:2556-2566"),
    44: ("comparison@:1 unknown:1 -:2 value:4",
         "KwdFile.java:2568-2574"),
    64: ("comparison@:1 unknown:1 -:2 value:4",
         "KwdFile.java:2568-2574"),
    53: ("target_type@make_type:1 target_id:1 -:2 value:4",
         "KwdFile.java:2576-2582"),
    1: ("comparison@:1 flag_id:1 flag:1 value_id:1 value:4",
        "KwdFile.java:2584-2590"),
    2: ("comparison@:1 timer_id:1 flag:1 value_id:1 value:4",
        "KwdFile.java:2592-2598"),
}

ACTION_LAYOUT_SPECS = {
    22: ("terrain_id:1 player_id:1 -:2 pos_x:2 pos_y:2",
         "KwdFile.java:2626-2632"),
    32: ("-:4 pos_x:2 pos_y:2", "KwdFile.java:2634-2638"),
    49: ("-:1 player_id:1 -:2 pos_x:2 pos_y:2", "KwdFile.java:2640-2646"),
    18: ("player_one_id:1 player_two_id:1 available:1 -:5",
         "KwdFile.java:2648-2653"),
    44: ("available:1 -:7", "KwdFile.java:2655-2664"),
    45: ("available:1 -:7", "KwdFile.java:2655-2664"),
    51: ("available:1 -:7", "KwdFile.java:2655-2664"),
    38: ("available:1 -:7", "KwdFile.java:2655-2664"),
    33: ("available:1 -:7", "KwdFile.java:2655-2664"),
    36: ("available:1 -:7", "KwdFile.java:2655-2664"),
    37: ("available:1 -:7", "KwdFile.java:2655-2664"),
    50: ("-:4 value:4", "KwdFile.java:2666-2669"),
    8: ("timer_id:1 -:3 value:4", "KwdFile.java:2671-2675"),
    7: ("flag_id:1 flag:1 -:2 value:4", "KwdFile.java:2677-2682"),
    6: ("player_id:1 type@make_type:1 target_id:1 available:1 -:4",
        "KwdFile.java:2684-2690"),
    46: ("available:1 -:7", "KwdFile.java:2692-2698"),
    47: ("target_id:1 -:7", "KwdFile.java:2700-2705"),
    42: ("target_id:1 -:7", "KwdFile.java:2700-2705"),
    26: ("target_id:1 -:7", "KwdFile.java:2700-2705"),
    4: ("objective_id:4 action_point_id:1 -:3", "KwdFile.java:2707-2711"),
    24: ("speech_id:4 text:1 introduction:1 path_id:2",
         "KwdFile.java:2713-2718"),
    25: ("text_id:4 -:4", "KwdFile.java:2720-2724"),
    20: ("-:8", "KwdFile.java:2726-2736"),
    29: ("-:8", "KwdFile.java:2726-2736"),
    35: ("-:8", "KwdFile.java:2726-2736"),
    43: ("-:8", "KwdFile.java:2726-2736"),
    10: ("-:8", "KwdFile.java:2726-2736"),
    11: ("-:8", "KwdFile.java:2726-2736"),
    21: ("-:8", "KwdFile.java:2726-2736"),
    13: ("-:8", "KwdFile.java:2726-2736"),
    12: ("-:8", "KwdFile.java:2726-2736"),
    34: ("value:4 -:4", "KwdFile.java:2738-2742"),
    30: ("value:4 -:4", "KwdFile.java:2738-2742"),
    23: ("timer_id:1 -:3 value:4", "KwdFile.java:2744-2748"),
    31: ("path_id:1 action_point_id:1 available:1 -:5",
         "KwdFile.java:2750-2755"),
    9: ("type@make_type:1 target_id:1 available:1 -:1 value:4",
        "KwdFile.java:2757-2763"),
    16: ("action_point_id:1 available:1 -:2 value:4",
         "KwdFile.java:2765-2770"),
    17: ("action_point_id:1 available:1 -:6", "KwdFile.java:2772-2776"),
    27: ("action_point_id:1 available:1 angle:2 time:4",
         "KwdFile.java:2778-2783"),
    1: ("creature_id:1 player_id:1 level:1 flag:1 pos_x:2 pos_y:2",
        "KwdFile.java:2785-2792"),
    15: ("player_id:1 type:1 -:2 action_point_id:4",
         "KwdFile.java:2794-2799"),
    14: ("party_id:1 type:1 -:2 action_point_id:1 -:3",
         "KwdFile.java:2801-2807"),
    48: ("generator_id:1 available:1 -:6", "KwdFile.java:2809-2813"),
    28: ("creature_id:1 level:1 -:6", "KwdFile.java:2815-2819"),
    5: ("information_id:4 action_point_id:1 -:3", "KwdFile.java:2821-2825"),
    3: ("-:4 action_point_id:1 -:3", "KwdFile.java:2827-2831"),
    2: ("object_id:1 player_id:1 -:2 pos_x:2 pos_y:2",
        "KwdFile.java:2833-2839"),
}

DEFAULT_GENERIC_LAYOUT = _parse_layout("-:8", "KwdFile.java:2600-2603")
DEFAULT_ACTION_LAYOUT = _parse_layout("-:8", "KwdFile.java:2841-2844")
GENERIC_LAYOUTS = {k: _parse_layout(*v) for k, v in GENERIC_LAYOUT_SPECS.items()}
ACTION_LAYOUTS = {k: _parse_layout(*v) for k, v in ACTION_LAYOUT_SPECS.items()}


def _u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def enum_name(table, value):
    name = table.get(value)
    return "%s (%d)" % (name, value) if name else "UNKNOWN (%d)" % value


def decode_tdata(tdata, kind, type_id):
    if kind == "condition":
        layout, source = GENERIC_LAYOUTS.get(type_id, DEFAULT_GENERIC_LAYOUT)
        names = TARGET_TYPES
    else:
        layout, source = ACTION_LAYOUTS.get(type_id, DEFAULT_ACTION_LAYOUT)
        names = ACTION_TYPES
    params = {}
    raw = {}
    reserved = []
    offset = 0
    for name, field_kind, enum, size in layout:
        if field_kind == "null":
            reserved.append({"offset": offset, "hex": tdata[offset:offset + size].hex()})
            offset += size
            continue
        value = int.from_bytes(tdata[offset:offset + size], "little")
        raw[name] = value
        if enum is not None:
            params[name] = enum_name(ENUM_TABLES[enum], value)
        else:
            params[name] = value
        offset += size
    if offset < len(tdata):
        reserved.append({"offset": offset, "hex": tdata[offset:].hex()})
    return {
        "params": params,
        "param_raw": raw,
        "reserved_bytes": reserved,
        "layout_source": source,
        "layout_is_default": type_id not in (GENERIC_LAYOUTS if kind == "condition"
                                             else ACTION_LAYOUTS),
        "type_name": enum_name(names, type_id),
    }


def parse_trigger_file(path):
    with open(path, "rb") as handle:
        data = handle.read()
    warnings = []
    if len(data) < 12:
        raise TriggerError("file shorter than the 12-byte root header: %s" % path)
    root_tag, version, declared = struct.unpack_from("<III", data, 0)
    if root_tag != ROOT_TAG:
        warnings.append("unexpected root tag %#x (expected %#x)" % (root_tag, ROOT_TAG))
    if version != 4:
        warnings.append("unexpected version %d (expected 4)" % version)
    if declared != len(data):
        warnings.append("declared size %d != file size %d" % (declared, len(data)))

    info = None
    records_start = None
    pos = 12
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from("<II", data, pos)
        if tag == INFO_TAG and size >= 8:
            info = {"offset": pos, "size": size,
                    "condition_count": _u32(data, pos + 8),
                    "action_count": _u32(data, pos + 12)}
        if tag == DATA_TAG:
            records_start = pos + 8
            break
        pos += 8 + size
    if info is None or records_start is None:
        raise TriggerError("missing 0xD3 info or 0xD4 data chunk: %s" % path)

    nodes = []
    pos = records_start
    while pos + RECORD_SIZE <= len(data):
        tag, size = struct.unpack_from("<II", data, pos)
        if tag not in (CONDITION_TAG, ACTION_TAG) or size != PAYLOAD_SIZE:
            warnings.append("stopped at offset %d: unexpected tag %#x size %d"
                            % (pos, tag, size))
            break
        payload = data[pos + 8:pos + 8 + PAYLOAD_SIZE]
        tdata = payload[:8]
        ident, ident_next, ident_child = struct.unpack_from("<HHH", payload, 8)
        type_id, repeat_times = payload[14], payload[15]
        kind = "condition" if tag == CONDITION_TAG else "action"
        decoded = decode_tdata(tdata, kind, type_id)
        nodes.append({
            "id": ident,
            "kind": kind,
            "tag": tag,
            "type_id": type_id,
            "type_name": decoded["type_name"],
            "repeat_times": repeat_times,
            "offset": pos,
            "record_size": RECORD_SIZE,
            "tdata_raw": tdata.hex(),
            "params": decoded["params"],
            "param_raw": decoded["param_raw"],
            "reserved_bytes": decoded["reserved_bytes"],
            "layout_source": decoded["layout_source"],
            "layout_is_default": decoded["layout_is_default"],
            "next_id": ident_next,
            "child_id": ident_child,
        })
        pos += RECORD_SIZE

    condition_count = sum(1 for n in nodes if n["kind"] == "condition")
    action_count = sum(1 for n in nodes if n["kind"] == "action")
    counts_match = (condition_count == info["condition_count"]
                    and action_count == info["action_count"])
    if not counts_match:
        warnings.append("info counts (%d conditions, %d actions) != parsed counts "
                        "(%d, %d)" % (info["condition_count"], info["action_count"],
                                      condition_count, action_count))
    by_id = {}
    for node in nodes:
        if node["id"] in by_id:
            warnings.append("duplicate id %d at offset %d" % (node["id"], node["offset"]))
        else:
            by_id[node["id"]] = node
    for node in nodes:
        node["next_resolved"] = node["next_id"] in by_id
        node["child_resolved"] = node["child_id"] in by_id

    return {
        "path": os.path.abspath(path),
        "file_name": os.path.basename(path),
        "file_size": len(data),
        "root_tag": hex(root_tag),
        "version": version,
        "info_chunk_offset": info["offset"],
        "records_start": records_start,
        "declared_counts": {
            "conditions": info["condition_count"],
            "actions": info["action_count"],
        },
        "parsed_counts": {"conditions": condition_count, "actions": action_count,
                          "total": len(nodes)},
        "counts_match": counts_match,
        "nodes": nodes,
        "warnings": warnings,
    }


def build_chains(parsed):
    by_id = {node["id"]: node for node in parsed["nodes"]}
    referenced = set()
    parents = {}
    for node in parsed["nodes"]:
        for key in ("next_id", "child_id"):
            target = node[key]
            if target and target in by_id:
                referenced.add(target)
                if key == "child_id":
                    parents.setdefault(target, []).append(node["id"])
    roots = [n["id"] for n in parsed["nodes"] if n["id"] not in referenced]
    chains = []
    for root_id in roots:
        seen = set()
        stack = [(root_id, 0)]
        nodes = []
        while stack and len(nodes) < 256:
            current, depth = stack.pop()
            if current in seen or current not in by_id:
                continue
            seen.add(current)
            node = by_id[current]
            nodes.append({"id": current, "kind": node["kind"],
                          "type_name": node["type_name"], "depth": depth})
            for key in ("child_id", "next_id"):
                target = node[key]
                if target and target in by_id and target not in seen:
                    stack.append((target, depth + 1))
        chains.append({"root_id": root_id, "nodes": nodes})
    return {"roots": roots, "chains": chains,
            "root_count": len(roots), "chain_count": len(chains),
            "parents": parents}


def _ancestors(node_id, reverse, limit=256):
    seen = set()
    stack = [node_id]
    result = []
    while stack and len(result) < limit:
        current = stack.pop()
        if current in seen:
            continue
        seen.add(current)
        for parent_id, via in reverse.get(current, []):
            if parent_id not in seen:
                stack.append(parent_id)
                result.append((parent_id, via))
    return result


def build_win_lose(parsed, mission=None):
    by_id = {node["id"]: node for node in parsed["nodes"]}
    reverse = {}
    for node in parsed["nodes"]:
        for key, via in (("next_id", "next"), ("child_id", "child")):
            target = node[key]
            if target and target in by_id:
                reverse.setdefault(target, []).append((node["id"], via))
    findings = []
    for node in parsed["nodes"]:
        if node["type_id"] not in (10, 11) or node["kind"] != "action":
            continue
        ancestors = []
        for ident, via in _ancestors(node["id"], reverse):
            parent = by_id.get(ident)
            if parent is None:
                continue
            ancestors.append({
                "id": ident,
                "via": via,
                "kind": parent["kind"],
                "type_name": parent["type_name"],
                "params": parent["params"],
                "repeat_times": parent["repeat_times"],
            })
        direct = reverse.get(node["id"], [])
        findings.append({
            "action_id": node["id"],
            "action": ACTION_TYPES[node["type_id"]],
            "offset": node["offset"],
            "direct_predecessors": [{"id": i, "via": v} for i, v in direct],
            "ancestor_conditions": ancestors,
            "confidence": {
                "action_and_params": "high",
                "chain_ancestry": "medium",
                "logical_combination": "unknown",
            },
        })
    return {
        "findings": findings,
        "win_count": sum(1 for f in findings if f["action"] == "WIN_GAME"),
        "lose_count": sum(1 for f in findings if f["action"] == "LOSE_GAME"),
        "mission_name": mission.get("name") if mission else None,
        "enemy": mission.get("enemy") if mission else None,
    }


def _mission_meta(name, missions_by_name):
    meta = missions_by_name.get(name.lower())
    if meta is None:
        return {
            "name": name, "category": "unknown", "campaign_level": None,
            "campaign_order": None, "enemy": None, "enemy_source": None,
        }
    enemy = None
    source = None
    for item in meta.get("recovered_strings", []):
        if item.get("kind") == "enemy_name":
            enemy = item.get("value")
            source = item.get("source")
            break
    return {
        "name": meta["name"],
        "category": meta.get("category"),
        "campaign_level": meta.get("campaign_level"),
        "campaign_order": meta.get("campaign_order"),
        "enemy": enemy,
        "enemy_source": source,
    }


def load_missions(root):
    path = os.path.join(root, "port", "docs", "missions.json")
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return {m["name"].lower(): m for m in data}


def resolve_map(name, root):
    maps_dir = os.path.join(root, "Data", "editor", "maps")
    resolve = M.make_resolver(maps_dir)
    if os.path.isabs(name) and os.path.isfile(name):
        return name, os.path.basename(name)
    if os.path.isfile(name):
        return os.path.abspath(name), os.path.basename(name)
    base = os.path.basename(name)
    if base.lower().endswith(".kld"):
        base = base[:-4]
    if base.lower().endswith("triggers"):
        base = base[:-8]
    path = resolve(base, "Triggers.kld")
    if path is None:
        raise TriggerError("cannot resolve trigger map %r in %s" % (name, maps_dir))
    return path, os.path.basename(path)


def build_map(name, root, missions_by_name):
    path, file_name = resolve_map(name, root)
    stem = file_name[:-len("Triggers.kld")]
    parsed = parse_trigger_file(path)
    meta = _mission_meta(stem, missions_by_name)
    chain = build_chains(parsed)
    return {
        "name": stem,
        "trigger_file": file_name,
        "category": meta["category"],
        "campaign_level": meta["campaign_level"],
        "campaign_order": meta["campaign_order"],
        "enemy": meta["enemy"],
        "enemy_source": meta["enemy_source"],
        "root_tag": parsed["root_tag"],
        "version": parsed["version"],
        "file_size": parsed["file_size"],
        "declared_counts": parsed["declared_counts"],
        "parsed_counts": parsed["parsed_counts"],
        "counts_match": parsed["counts_match"],
        "chain_stats": {
            "roots": chain["roots"],
            "root_count": chain["root_count"],
            "chain_count": chain["chain_count"],
            "chains": chain["chains"],
        },
        "win_lose": build_win_lose(parsed, meta),
        "warnings": parsed["warnings"],
        "nodes": parsed["nodes"],
    }


def build_catalog(root=None):
    root = root or M.find_game_root()
    missions_by_name = load_missions(root)
    catalog = M.load_catalog(root)
    maps = [build_map(name, root, missions_by_name) for name, _ in catalog]
    totals = {
        "maps": len(maps),
        "conditions": sum(m["parsed_counts"]["conditions"] for m in maps),
        "actions": sum(m["parsed_counts"]["actions"] for m in maps),
        "records": sum(m["parsed_counts"]["total"] for m in maps),
        "maps_with_triggers": sum(1 for m in maps if m["parsed_counts"]["total"]),
        "maps_with_win": sum(1 for m in maps if m["win_lose"]["win_count"]),
        "maps_with_lose": sum(1 for m in maps if m["win_lose"]["lose_count"]),
    }
    return {
        "schema_version": SCHEMA_VERSION,
        "generated_by": "port/tools/trigger_tool.py",
        "sources": {
            "template": "port/research/dk2-tools/Formats/Level/triggers_struct.bt",
            "parser": "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/KwdFile.java",
            "enums": "port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/TriggerAction.java,TriggerGeneric.java",
            "missions": "port/docs/missions.json",
        },
        "confidence": {
            "high": "byte reads: tag, size, tdata, id/idNext/idChild, type, repeatTimes",
            "medium": "enum names, field layouts and chain ancestry from cited sources",
            "unknown": "logical AND/OR meaning of next/child, text-string resolution",
        },
        "totals": totals,
        "action_types": {str(k): v for k, v in sorted(ACTION_TYPES.items())},
        "target_types": {str(k): v for k, v in sorted(TARGET_TYPES.items())},
        "maps": maps,
    }


def _write_json(text, out):
    if out:
        out = os.path.abspath(out)
        parent = os.path.dirname(out)
        if not os.path.isdir(parent):
            raise TriggerError("output directory does not exist: %s" % parent)
        with open(out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    else:
        sys.stdout.write(text)


def cmd_list(args):
    root = args.root or M.find_game_root()
    path, _ = resolve_map(args.map, root)
    parsed = parse_trigger_file(path)
    print("file: %s" % parsed["file_name"])
    print("root: %s version=%d  info@0x%X  records@0x%X"
          % (parsed["root_tag"], parsed["version"],
             parsed["info_chunk_offset"], parsed["records_start"]))
    print("declared: %d conditions, %d actions"
          % (parsed["declared_counts"]["conditions"],
             parsed["declared_counts"]["actions"]))
    print("parsed:   %d conditions, %d actions (match=%s)"
          % (parsed["parsed_counts"]["conditions"],
             parsed["parsed_counts"]["actions"], parsed["counts_match"]))
    print("")
    print("   id  kind       type                          rep  next   child   params")
    for node in parsed["nodes"]:
        params = " ".join("%s=%s" % (k, v) for k, v in node["params"].items())
        if len(params) > 76:
            params = params[:73] + "..."
        print("%6d  %-9s %-28s %3d %6d %7d  %s"
              % (node["id"], node["kind"], node["type_name"],
                 node["repeat_times"], node["next_id"], node["child_id"], params))
    wl = build_win_lose(parsed)
    print("")
    print("WIN_GAME=%d LOSE_GAME=%d" % (wl["win_count"], wl["lose_count"]))
    for warning in parsed["warnings"]:
        print("WARNING: %s" % warning)
    return 0


def cmd_json(args):
    root = args.root or M.find_game_root()
    result = build_map(args.map, root, load_missions(root))
    _write_json(json.dumps(result, indent=2, ensure_ascii=False) + "\n", args.out)
    return 0


def cmd_catalog(args):
    result = build_catalog(args.root)
    _write_json(json.dumps(result, indent=2, ensure_ascii=False) + "\n", args.out)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=None,
                        help="game root (default: auto-detect from this script)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="list decoded triggers for one map")
    p_list.add_argument("map")
    p_list.set_defaults(func=cmd_list)

    p_json = sub.add_parser("json", help="structured JSON for one map")
    p_json.add_argument("map")
    p_json.add_argument("--out", default=None)
    p_json.set_defaults(func=cmd_json)

    p_catalog = sub.add_parser("catalog", help="decode all 66 trigger files")
    p_catalog.add_argument("--out", default=None)
    p_catalog.set_defaults(func=cmd_catalog)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (TriggerError, RuntimeError, OSError, ValueError, struct.error) as exc:
        print("trigger_tool: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
