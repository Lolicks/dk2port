#!/usr/bin/env python3
"""Generate engine/Structures.generated.hpp from docs/stats.json.

Emits flat, C-compatible tables for the five catalogues the engine can use:
rooms, traps, doors, terrains and objects.  Only fields that are present in
every record AND carry high/medium confidence in stats.json are emitted.

The generator is deterministic and uses only the Python standard library:

    python3 tools/gen_structures.py

Do not edit engine/Structures.generated.hpp by hand; edit this script instead.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
STATS_PATH = os.path.join(PORT, "docs", "stats.json")
OUTPUT_PATH = os.path.join(PORT, "engine", "Structures.generated.hpp")

ALLOWED_CONFIDENCE = frozenset(("high", "medium"))

# Readable, stable column order.  Eligible fields not listed here are appended
# alphabetically so that adding a field to stats.json cannot reshuffle columns.
FIELD_ORDER = {
    "rooms": [
        "room_id", "terrain_id", "cost", "research_time", "health_gain",
        "created_creature_id", "order_in_editor", "construction",
        "recommended_size_x", "recommended_size_y", "return_percentage",
        "ceiling_height", "torch_height", "torch_intensity", "torch_radius",
    ],
    "traps": [
        "trap_id", "order_in_editor", "gold_cost", "mana_usage", "mana_gen",
        "mana_cost_to_fire", "manuf_to_build", "research_time", "health",
        "health_gain", "charge_time", "recharge_time", "shot_delay",
        "shots_when_armed", "threat", "threat_duration", "trigger",
    ],
    "doors": [
        "door_id", "order_in_editor", "gold_cost", "mana_cost",
        "manuf_to_build", "research_time", "health", "health_gain", "flags",
    ],
    "terrains": [
        "terrain_id", "gold_value", "mana_gain", "max_mana_gain",
        "starting_health", "max_health", "damage", "depth", "light_height",
        "texture_frames",
    ],
    "objects": [
        "object_id", "hp", "mana_value", "mana_gen", "state", "width",
        "height", "speed", "mass", "air_friction", "max_angle",
        "pick_up_priority", "room_capacity",
    ],
}

STRUCT_NAMES = {
    "rooms": ("DK2RoomDef", "kDK2Rooms", "kDK2RoomCount"),
    "traps": ("DK2TrapDef", "kDK2Traps", "kDK2TrapCount"),
    "doors": ("DK2DoorDef", "kDK2Doors", "kDK2DoorCount"),
    "terrains": ("DK2TerrainDef", "kDK2Terrains", "kDK2TerrainCount"),
    "objects": ("DK2ObjectDef", "kDK2Objects", "kDK2ObjectCount"),
}

SECTION_LABEL = {
    "rooms": "Rooms.kwd", "traps": "Traps.kwd", "doors": "Doors.kwd",
    "terrains": "Terrain.kwd", "objects": "Objects.kwd",
}


def field_value(entry, key, default=0):
    """Unwrap a {value, source, confidence} leaf to its raw value."""
    item = entry.get("stats", {}).get(key)
    if isinstance(item, dict):
        return item.get("value", default)
    return item if item is not None else default


def eligible_fields(rows):
    """Fields present in every row whose confidence is high or medium."""
    if not rows:
        return []
    common = None
    for row in rows:
        keys = set()
        for key, leaf in row.get("stats", {}).items():
            if isinstance(leaf, dict):
                confidence = leaf.get("confidence")
                if confidence in ALLOWED_CONFIDENCE and "value" in leaf:
                    keys.add(key)
        common = keys if common is None else (common & keys)
    ordered = [f for f in FIELD_ORDER.get(rows[0].get("_section", ""), []) if f in common]
    ordered += sorted(common.difference(ordered))
    return ordered


def cxx_number(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        text = repr(float(value))
        if "." not in text and "e" not in text and "E" not in text:
            text += ".0"
        return text + "f"
    raise ValueError("unsupported numeric value: %r" % (value,))


def cxx_string(text):
    return '"' + str(text).replace("\\", "\\\\").replace('"', '\\"') + '"'


def field_c_type(rows, field):
    values = [field_value(row, field) for row in rows]
    return "float" if any(isinstance(v, float) for v in values) else "int"


def generate_section(section, rows):
    fields = eligible_fields(rows)
    struct_name, array_name, count_name = STRUCT_NAMES[section]

    lines = []
    lines.append("// %s -- %d records from %s" %
                 (struct_name, len(rows), SECTION_LABEL[section]))
    lines.append("struct %s {" % struct_name)
    lines.append("    const char* name;")
    for field in fields:
        ctype = field_c_type(rows, field)
        lines.append("    %-22s %s;" % (ctype, field))
    lines.append("};")
    lines.append("static const %s %s[] = {" % (struct_name, array_name))

    for row in rows:
        parts = [cxx_string(row["name"])]
        for field in fields:
            parts.append(cxx_number(field_value(row, field)))
        lines.append("    {%s}," % ", ".join(parts))
    lines.append("};")
    lines.append("static const unsigned %s =\n    sizeof(%s) / sizeof(%s[0]);"
                 % (count_name, array_name, array_name))
    return "\n".join(lines), fields


def main():
    with open(STATS_PATH, encoding="utf-8") as handle:
        stats = json.load(handle)

    expected = {"rooms": 26, "traps": 16, "doors": 8,
                "terrains": 41, "objects": 145}
    blocks = []
    summary = []
    for section in ("rooms", "traps", "doors", "terrains", "objects"):
        rows = stats[section]
        if len(rows) != expected[section]:
            raise SystemExit("%s: expected %d records, found %d" %
                             (section, expected[section], len(rows)))
        for row in rows:
            row["_section"] = section  # only used to pick the canonical order
        block, fields = generate_section(section, rows)
        blocks.append(block)
        summary.append("%s=%d(%d fields)" % (section, len(rows), len(fields)))

    header = [
        "// Generated by tools/gen_structures.py from docs/stats.json. Do not edit.",
        "// Source: shipped Data/editor/*.kwd catalogues, decoded by tools/stats_tool.py.",
        "// IDs are the 1-based values stored in the KWD records (see STRUCTURES_USAGE.md).",
        "#pragma once",
        "",
        "\n\n".join(blocks),
        "",
    ]
    with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(header))
    print("generated engine/Structures.generated.hpp: " + " ".join(summary))


if __name__ == "__main__":
    main()
