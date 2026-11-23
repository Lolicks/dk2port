#!/usr/bin/env python3
"""Recover concrete numeric game stats from Data/editor/*.kwd catalogues.

Stdlib only.  The container parser is reused from :mod:`kwd_tool`; this module
adds the *record* layouts taken from the 010 Editor templates in
``research/dk2-tools/Formats/Level`` and cross-checked field-by-field against
the OpenKeeper Java KWD reader (``KwdFile.java`` and the per-type container
classes).

Every layout below is built as an explicit, ordered list of fields.  The
builder computes byte offsets from the previous field's size and *refuses to
run* unless the computed structure size equals ``record_size`` reported by
``kwd_tool`` for the shipped file.  That is the primary guard against an
off-by-one layout silently producing plausible-looking garbage.

Floating-point fields are stored as fixed point, not IEEE-754: OpenKeeper's
``ResourceChunkReader.readIntegerAsFloat()`` divides a signed int32 by
``ConversionUtils.FLOAT = 4096`` (``readShortAsFloat`` uses int16/4096 and
``readIntegerAsDouble`` uses int32/65536).  See docs/format-research/
STATS_RECOVERY.md for the full derivation and the remaining unknowns.

Usage::

    python3 tools/stats_tool.py summary            # human-readable spot check
    python3 tools/stats_tool.py write              # regenerate docs/stats.json
    python3 tools/stats_tool.py creature Imp       # one creature's stats
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT_DIR = HERE.parent
ROOT_DIR = PORT_DIR.parent
DATA_DIR = ROOT_DIR / "Data" / "editor"
OUT_JSON = PORT_DIR / "docs" / "stats.json"

if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from kwd_tool import KwdFile  # noqa: E402  (local import, after sys.path fix)

FLOAT_SCALE = 4096
DOUBLE_SCALE = 65536

BT_DIR = "research/dk2-tools/Formats/Level"
JAVA_MAP = "research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map"


# ---------------------------------------------------------------------------
# Primitive field kinds.  Sizes come from the 010 templates; the decoders come
# from OpenKeeper's ResourceChunkReader where a conversion is involved.
# ---------------------------------------------------------------------------
KIND_SIZE = {
    "u8": 1, "i8": 1, "u16": 2, "i16": 2, "u32": 4, "i32": 4,
    "fixed32": 4, "fixed16": 2, "fixed64": 4,
    "str32": 32, "str64": 64, "art": 84, "unkart": 84,
    "light": 24, "stringid": 24, "color3": 3, "v3": 12,
    "flag4": 4, "attract": 8, "spell8": 24, "resist": 2,
    "jobpref": 12, "jobalt": 8, "footsteps": 192, "bytes8": 8,
}
# Composite kinds expand to these ordered (sub-name, kind) pairs.
COMPOSITE = {
    "attract": (("present", "u32"), ("room_id", "u16"), ("room_size", "u16")),
    "spell8": (("shot_x", "fixed32"), ("shot_y", "fixed32"), ("shot_z", "fixed32"),
               ("unk_1", "u8"), ("play_animation", "u8"), ("unk_2", "u8"),
               ("unk_3", "u8"), ("shot_delay", "fixed32"),
               ("unk_4", "u8"), ("unk_5", "u8"),
               ("creature_spell_id", "u8"), ("level_available", "u8")),
    "resist": (("attack_type", "u8"), ("value", "u8")),
    "jobpref": (("job_type", "u32"), ("mood_change", "u16"),
                ("mana_change", "u16"), ("chance", "u8"),
                ("pad_1", "u8"), ("pad_2", "u8"), ("pad_3", "u8")),
    "jobalt": (("job_type", "u32"), ("mood_change", "u16"), ("mana_change", "u16")),
    "v3": (("x", "fixed32"), ("y", "fixed32"), ("z", "fixed32")),
}


class LayoutError(ValueError):
    """Raised when a declared layout does not match the shipped record size."""


class Field:
    __slots__ = ("name", "kind", "count", "bt_line", "java_lines", "offset", "size")

    def __init__(self, name, kind, count, bt_line, java_lines, offset):
        self.name = name
        self.kind = kind
        self.count = count
        self.bt_line = bt_line
        self.java_lines = java_lines
        self.offset = offset
        self.size = KIND_SIZE[kind] * count

    def source(self, bt_file, java_file=None):
        # Short, unambiguous file names; the full relative directories are
        # recorded once in meta["source_directories"] in stats.json.
        parts = ["%s:%d" % (os.path.basename(bt_file), self.bt_line)]
        for line, file_name in self.java_lines:
            name = file_name or java_file or "KwdFile.java"
            parts.append("%s:%d" % (os.path.basename(name), line))
        return "; ".join(parts)


def f(name, kind, bt_line, java_line=None, count=1, java_file=None):
    """Describe one field (or a homogeneous array of fields)."""
    if java_line is None:
        java_lines = ()
    elif isinstance(java_line, (tuple, list)):
        java_lines = tuple(java_line)
    else:
        java_lines = ((java_line, java_file),)
    return (name, kind, count, bt_line, java_lines)


class Layout:
    """Ordered field list with computed offsets and a hard size check."""

    def __init__(self, bt_file, expected_size, spec):
        self.bt_file = bt_file
        self.expected_size = expected_size
        self.fields = []
        pos = 0
        for name, kind, count, bt_line, java_lines in spec:
            field = Field(name, kind, count, bt_line, java_lines, pos)
            self.fields.append(field)
            pos += field.size
        if pos != expected_size:
            raise LayoutError("%s: computed %d bytes, shipped records are %d bytes"
                              % (bt_file, pos, expected_size))
        self.size = pos

    def by_name(self):
        return {field.name: field for field in self.fields}

    def source(self, name):
        return self.by_name()[name].source(self.bt_file)


# ---------------------------------------------------------------------------
# Layouts.  Field order is copied verbatim from the corresponding .bt struct;
# byte counts for the composites are confirmed by KwdFile.java (ArtResource is
# 84 bytes: KwdFile.java:753-837, Light 24 bytes: KwdFile.java:1623-1643).
# ---------------------------------------------------------------------------
CREATURE_SPEC = (
    f("name", "str32", 243, 1283),
    f("unused_animation_slot", "unkart", 244, 1285),
    # 36 animation ArtResources, KwdFile.java:1286-1321
    f("anim_walk", "art", 245, 1286), f("anim_run", "art", 246, 1287),
    f("anim_dragged", "art", 247, 1288), f("anim_recoil_hff", "art", 248, 1289),
    f("anim_melee1", "art", 249, 1290), f("anim_magic", "art", 250, 1291),
    f("anim_die", "art", 251, 1292), f("anim_happy", "art", 252, 1293),
    f("anim_angry", "art", 253, 1294), f("anim_stunned", "art", 254, 1295),
    f("anim_in_hand", "art", 255, 1296), f("anim_sleep", "art", 256, 1297),
    f("anim_eat", "art", 257, 1298), f("anim_research", "art", 258, 1299),
    f("anim_unused15", "art", 259, 1300), f("anim_dejected", "art", 260, 1301),
    f("anim_torture", "art", 261, 1302), f("anim_unused18", "art", 262, 1303),
    f("anim_drink", "art", 263, 1304), f("anim_idle1", "art", 264, 1305),
    f("anim_recoil_hfb", "art", 265, 1306), f("anim_building", "art", 266, 1307),
    f("anim_pray", "art", 267, 1308), f("anim_fallback", "art", 268, 1309),
    f("anim_elec", "art", 269, 1310), f("anim_electrocute", "art", 270, 1311),
    f("anim_get_up", "art", 271, 1312), f("anim_dance", "art", 272, 1313),
    f("anim_drunk", "art", 273, 1314), f("anim_entrance", "art", 274, 1315),
    f("anim_idle2", "art", 275, 1316), f("anim_special", "art", 276, 1317),
    f("anim_eat_corpse", "art", 277, 1318), f("anim_drunk2", "art", 279, 1319),
    f("anim_loot", "art", 278, 1320), f("anim_unused36", "art", 280, 1321),
    f("icon1", "art", 281, 1323), f("icon2", "art", 282, 1324),
    f("swipe_speed", "u16", 283, 1326),
    f("work_modifier_default", "u32", 284, 1327),
    f("work_modifier_level_increase", "u32", 285, 1328),
    f("order_in_editor", "u8", 286, 1329),
    f("anger_string_general", "u16", 287, 1330),
    f("shot_delay", "fixed32", 288, 1331),
    f("olhi_effect_id", "u16", 289, 1332),
    f("introduction_string_id", "u16", 290, 1333),
    f("perception_range", "fixed32", 291, 1334),
    f("anger_string_lair", "u16", 292, 1335),
    f("anger_string_food", "u16", 293, 1336),
    f("anger_string_pay", "u16", 294, 1337),
    f("anger_string_work", "u16", 295, 1338),
    f("anger_string_slap", "u16", 296, 1339),
    f("anger_string_held", "u16", 297, 1340),
    f("anger_string_lonely", "u16", 298, 1341),
    f("anger_string_hatred", "u16", 299, 1342),
    f("anger_string_torture", "u16", 300, 1343),
    f("translation_sound_category", "str32", 301, 1345),
    f("shuffle_speed", "fixed32", 302, 1346),
    f("clone_creature_id", "u8", 303, 1347),
    f("first_person_gamma_effect", "u8", 304, 1348),
    f("first_person_walk_cycle_scale", "u8", 305, 1349),
    f("intro_camera_path_index", "u8", 306, 1350),
    f("unk_12", "u8", 307, 1351),
    f("portrait", "art", 308, 1352),
    f("light", "light", 309, 1353),
    f("attractions", "attract", 310, 1354, count=2),
    f("first_person_waddle_scale", "fixed32", 311, 1376),
    f("first_person_oscillate_scale", "fixed32", 312, 1377),
    f("spells", "spell8", 313, 1378, count=3),
    f("resistances", "resist", 314, 1398, count=4),
    f("happy_jobs", "jobpref", 315, 1406, count=3),
    f("unhappy_jobs", "jobpref", 316, 1407, count=2),
    f("angry_jobs", "jobpref", 317, 1408, count=3),
    f("hate_jobs", "u32", 318, 1409, count=2),
    f("alternative_jobs", "jobalt", 319, 1414, count=3),
    f("animation_offset", "v3", 320, 1423),
    f("width", "fixed32", 321, 1428),
    f("height", "fixed32", 322, 1429),
    f("mass", "fixed32", 323, 1430),
    f("air_friction", "fixed32", 324, 1431),
    f("eye_height", "fixed32", 325, 1432),
    f("speed", "fixed32", 326, 1433),
    f("run_speed", "fixed32", 327, 1434),
    f("hunger_rate", "fixed32", 328, 1435),
    f("time_awake", "u32", 329, 1436),
    f("time_sleep", "u32", 330, 1437),
    f("distance_can_see", "fixed32", 331, 1438),
    f("distance_can_hear", "fixed32", 332, 1439),
    f("stun_duration", "fixed32", 333, 1440),
    f("guard_duration", "fixed32", 334, 1441),
    f("idle_duration", "fixed32", 335, 1442),
    f("slap_fearless_duration", "fixed32", 336, 1443),
    f("unk_17", "i32", 337, 1444),
    f("ability_creep_percentage", "i32", 338, 1445),
    f("possession_mana_cost", "i16", 339, 1446),
    f("own_land_health_increase", "i16", 340, 1447),
    f("melee_range", "fixed32", 341, 1448),
    f("unk_19", "i32", 342, 1449),
    f("torture_time_to_convert", "fixed32", 343, 1450),
    f("melee_recharge", "fixed32", 344, 1451),
    f("flags", "flag4", 345, 1453),
    f("exp_for_next_level", "u16", 346, 1454),
    f("job_class", "u8", 347, 1455),
    f("fight_style", "u8", 348, 1456),
    f("exp_per_second", "u16", 349, 1457),
    f("exp_per_second_training", "u16", 350, 1458),
    f("research_per_second", "u16", 351, 1459),
    f("manufacture_per_second", "u16", 352, 1460),
    f("hp", "u16", 353, ((1461, None), (2138, "Creature.java"))),
    f("hp_from_chicken", "u16", 354, ((1462, None), (2146, "Creature.java"))),
    f("fear", "u16", 355, ((1463, None), (2154, "Creature.java"))),
    f("threat", "u16", 356, ((1464, None), (2162, "Creature.java"))),
    f("melee_damage", "u16", 357, ((1465, None), (1169, "Creature.java"))),
    f("slap_damage", "u16", 358, 1466),
    f("mana_gen_prayer", "u16", 359, 1467),
    f("unk_20", "u16", 360, 1468),
    f("pay", "u16", 361, ((1469, None), (2186, "Creature.java"))),
    f("max_gold_held", "u16", 362, 1470),
    f("max_angle_change", "u16", 363, 1471),
    f("decompose_value", "u16", 364, 1472),
    f("name_string_id", "u16", 365, 1473),
    f("tooltip_string_id", "u16", 366, 1474),
    f("anger_no_lair", "u16", 367, 1475),
    f("anger_no_food", "u16", 368, 1476),
    f("anger_no_pay", "u16", 369, 1477),
    f("anger_no_work", "u16", 370, 1478),
    f("anger_slap", "u16", 371, 1479),
    f("anger_in_hand", "u16", 372, 1480),
    f("initial_gold_held", "u16", 373, 1481),
    f("entrance_effect_id", "u16", 374, 1482),
    f("general_description_string_id", "u16", 375, 1483),
    f("strength_string_id", "u16", 376, 1484),
    f("weakness_string_id", "u16", 377, 1485),
    f("slap_effect_id", "u16", 378, 1486),
    f("death_effect_id", "u16", 379, 1487),
    f("melee1_swipe", "u8", 380, 1488),
    f("melee2_swipe", "u8", 381, 1489),
    f("melee3_swipe", "u8", 382, 1490),
    f("spell_swipe", "u8", 383, 1491),
    f("first_person_ability1", "u8", 384, 1492),
    f("first_person_ability2", "u8", 385, 1493),
    f("unk_23", "u8", 386, 1494, count=3),
    f("creature_id", "u8", 387, 1499),
    f("score_rating", "u8", 388, 1500),
    f("room_capacity_needed", "u8", 389, 1502),
    f("hunger_fill", "u8", 390, 1505),
    f("unhappy_threshold", "u8", 391, 1506),
    f("melee_attack_type", "u8", 392, 1507),
    f("disarm_failure_percentage", "u8", 393, 1508),
    f("lair_object_id", "u8", 394, 1509),
    f("unk_26", "u8", 395, 1510),
    f("death_fall_direction", "u8", 396, 1511),
    f("unk_27", "u8", 397, 1512),
    f("sound_category", "str32", 398, 1514),
    f("material", "u8", 399, 1515),
    f("first_person_filter", "art", 400, 1516),
    f("satisfy_anger_treat", "i16", 401, 1517),
    f("scale", "fixed32", 402, 1518),
    f("drunk_idle", "art", 403, 1519),
    f("special1_swipe", "u8", 404, 1520),
    f("special2_swipe", "u8", 405, 1521),
    f("first_person_melee", "art", 406, 1522),
    f("sniper_zoom_multiplier", "fixed32", 407, 1523),
    f("torture_hp_change", "i16", 408, 1524),
    f("torture_mood_change", "i16", 409, 1525),
    f("anim_melee2", "art", 410, 1526),
    f("anim_extra43", "art", 411, 1527),
    f("anim_extra44", "art", 412, 1528),
    f("anim_extra45", "art", 413, 1529),
    f("anim_extra46", "art", 414, 1530),
    f("anim_extra47", "art", 415, 1531),
    f("animation_offsets", "v3", 416, 1533, count=7),
    f("anim_walkback", "art", 417, 1543),
    f("footstep_frames", "footsteps", 418, 1544),
    f("anim_pose_frame", "art", 422, 1552),
    f("anim_walk2", "art", 423, 1553),
    f("anim_die_pose", "art", 424, 1554),
    f("unique_name_text_id", "u16", 425, 1555),
    f("hate_jobs_unused", "bytes8", 426, 1556),
    f("ability1_count", "i32", 429, 1561),
    f("ability2_count", "i32", 430, 1562),
    f("unique_resource", "art", 431, 1563),
    # CreatureFlag2: 4 flag bits + 3 unused bytes, KwdFile.java:1573.
    f("flags2", "flag4", 432, 1573),
)

ROOM_SPEC = (
    f("name", "str32", 46, 1040),
    f("gui_icon", "art", 47, 1041), f("editor_icon", "art", 48, 1042),
    f("complete", "art", 49, 1043), f("straight", "art", 50, 1044),
    f("inside_corner", "art", 51, 1045), f("null_resource", "art", 52, 1046),
    f("outside_corner", "art", 53, 1047), f("wall", "art", 54, 1048),
    f("cap", "art", 55, 1049), f("ceiling", "art", 56, 1050),
    f("ceiling_height", "fixed32", 57, 1051),
    f("research_time", "u16", 58, ((1052, None), (141, "Room.java"))),
    f("torch_intensity", "u16", 59, 1053),
    f("flags", "flag4", 60, 1054),
    f("tooltip_string_id", "u16", 61, 1055),
    f("name_string_id", "u16", 62, 1056),
    f("cost", "u16", 63, ((1057, None), (146, "Room.java"))),
    f("fight_effect_id", "u16", 64, 1058),
    f("general_description_string_id", "u16", 65, 1059),
    f("strength_string_id", "u16", 66, 1060),
    f("torch_height", "fixed16", 67, 1061),
    f("room_effects", "u16", 68, 1062, count=8),
    f("room_id", "u8", 69, 1068),
    f("return_percentage", "u8", 70, 1069),
    f("terrain_id", "u8", 71, 1070),
    f("construction", "u8", 72, 1071),
    f("created_creature_id", "u8", 73, 1072),
    f("torch_color", "color3", 74, 1073),
    f("object_ids", "u8", 75, 1074, count=8),
    f("sound_category", "str32", 76, 1081),
    f("order_in_editor", "u8", 77, 1082),
    f("torch_radius", "fixed32", 78, 1083),
    f("torch", "art", 79, 1084),
    f("recommended_size_x", "u8", 80, 1085),
    f("recommended_size_y", "u8", 81, 1086),
    f("health_gain", "i16", 82, ((1087, None), (165, "Room.java"))),
)

TRAP_SPEC = (
    f("name", "str32", 40, 960),
    f("mesh", "art", 41, 961), f("gui_icon", "art", 42, 962),
    f("editor_icon", "art", 43, 963), f("flower_icon", "art", 44, 964),
    f("fire_resource", "art", 45, 965),
    f("height", "fixed32", 46, 966),
    f("recharge_time", "fixed32", 47, ((967, None), (94, "Trap.java"))),
    f("charge_time", "fixed32", 48, 968),
    f("threat_duration", "fixed32", 49, ((969, None), (96, "Trap.java"))),
    f("mana_cost_to_fire", "u32", 50, ((970, None), (97, "Trap.java"))),
    f("idle_effect_delay", "fixed32", 51, 971),
    f("trigger_data", "u32", 52, 972),
    f("shot_data1", "u32", 53, 973), f("shot_data2", "u32", 54, 974),
    f("research_time", "u16", 55, ((975, None), (102, "Trap.java"))),
    f("threat", "u16", 56, ((976, None), (103, "Trap.java"))),
    f("flags", "flag4", 57, 977),
    f("health", "u16", 58, ((978, None), (105, "Trap.java"))),
    f("gold_cost", "u16", 59, ((979, None), (106, "Trap.java"))),
    f("powerless_effect_id", "u16", 60, 980),
    f("idle_effect_id", "u16", 61, 981),
    f("death_effect_id", "u16", 62, 982),
    f("manuf_to_build", "u16", 63, 983),
    f("general_description_string_id", "u16", 64, 984),
    f("strength_string_id", "u16", 65, 985),
    f("weakness_string_id", "u16", 66, 986),
    f("mana_usage", "u16", 67, ((987, None), (114, "Trap.java"))),
    f("mana_gen", "i16", 68, 988),
    f("tooltip_string_id", "u16", 69, 993),
    f("name_string_id", "u16", 70, 994),
    f("shots_when_armed", "u8", 71, 995),
    f("trigger", "u8", 72, 996),
    f("trap_id", "u8", 73, 997),
    f("shot_type_id", "u8", 74, 998),
    f("manuf_crate_object_id", "u8", 75, 999),
    f("sound_category", "str32", 76, 1001),
    f("material", "u8", 77, 1002),
    f("order_in_editor", "u8", 78, 1003),
    f("shot_offset", "v3", 79, 1004),
    f("shot_delay", "fixed32", 80, 1007),
    f("health_gain", "u16", 81, ((1008, None), (128, "Trap.java"))),
)

KEEPER_SPELL_SPEC = (
    f("name", "str32", 51, 1935),
    f("gui_icon", "art", 52, 1936), f("editor_icon", "art", 53, 1937),
    f("unk_1", "u32", 54, 1938),
    f("recharge_time", "fixed32", 55, ((1939, None), (147, "KeeperSpell.java"))),
    f("shot_data1", "u32", 56, 1940), f("shot_data2", "u32", 57, 1941),
    f("research_time", "u16", 58, ((1942, None), (150, "KeeperSpell.java"))),
    f("target_rule", "u8", 59, 1943),
    f("order_in_editor", "u8", 60, 1944),
    f("flags", "flag4", 61, 1945),
    f("unk_2", "u16", 62, 1946),
    f("mana_drain", "u16", 63, ((1947, None), (155, "KeeperSpell.java"))),
    f("tooltip_string_id", "u16", 64, 1948),
    f("name_string_id", "u16", 65, 1949),
    f("general_description_string_id", "u16", 66, 1950),
    f("strength_string_id", "u16", 67, 1951),
    f("weakness_string_id", "u16", 68, 1952),
    f("keeper_spell_id", "u8", 69, 1953),
    f("cast_rule", "u8", 70, 1954),
    f("shot_type_id", "u8", 71, 1955),
    f("sound_category", "str32", 72, 1957),
    f("bonus_r_time", "u16", 73, 1958),
    f("bonus_shot_type_id", "u8", 74, 1959),
    f("bonus_shot_data1", "i32", 75, 1960),
    f("bonus_shot_data2", "i32", 76, 1961),
    f("mana_cost", "i32", 77, ((1962, None), (170, "KeeperSpell.java"))),
    f("bonus_icon", "art", 78, 1963),
    f("sound_category_gui", "str32", 79, 1965),
    f("hand_anim", "u8", 80, 1966),
    f("nogo_hand_anim", "u8", 81, 1967),
)

CREATURE_SPELL_SPEC = (
    f("name", "str32", 33, 1745),
    f("editor_icon", "art", 34, 1746), f("gui_icon", "art", 35, 1747),
    f("shot_data1", "u32", 36, 1748), f("shot_data2", "u32", 37, 1749),
    f("range", "fixed32", 38, 1750),
    f("flags", "flag4", 39, 1751),
    f("combat_points", "u16", 40, 1752),
    f("sound_event", "u16", 41, 1753),
    f("name_string_id", "u16", 42, 1754),
    f("tooltip_string_id", "u16", 43, 1755),
    f("general_description_string_id", "u16", 44, 1756),
    f("strength_string_id", "u16", 45, 1757),
    f("weakness_string_id", "u16", 46, 1758),
    f("creature_spell_id", "u8", 47, 1759),
    f("shot_type_id", "u8", 48, 1760),
    f("alternative_shot_id", "u8", 49, 1761),
    f("alternative_room_id", "u8", 50, 1762),
    f("recharge_time", "fixed32", 51, 1763),
    f("alternative_shot", "u8", 52, 1764),
    f("unused", "u8", 53, 1765, count=27),
)

DOOR_SPEC = (
    f("name", "str32", 24, 894),
    f("mesh", "art", 25, 895), f("gui_icon", "art", 26, 896),
    f("editor_icon", "art", 27, 897), f("flower_icon", "art", 28, 898),
    f("open_resource", "art", 29, 899), f("close_resource", "art", 30, 900),
    f("height", "fixed32", 31, 901),
    f("health_gain", "u16", 32, 902),
    f("unk_1", "u16", 33, 903), f("unk_2", "u32", 34, 904),
    f("research_time", "u16", 35, 905),
    f("material", "u8", 36, 906), f("trap_id", "u8", 37, 907),
    f("flags", "flag4", 38, 908),
    f("health", "u16", 39, 909),
    f("gold_cost", "u16", 40, 910),
    f("unk_4", "u8", 41, 911, count=2),
    f("death_effect_id", "u16", 42, 916),
    f("manuf_to_build", "u32", 43, 917),
    f("mana_cost", "u16", 44, 918),
    f("tooltip_string_id", "u16", 45, 919),
    f("name_string_id", "u16", 46, 920),
    f("general_description_string_id", "u16", 47, 921),
    f("strength_string_id", "u16", 48, 922),
    f("weakness_string_id", "u16", 49, 923),
    f("door_id", "u8", 50, 924), f("order_in_editor", "u8", 51, 925),
    f("manuf_crate_object_id", "u8", 52, 926),
    f("key_object_id", "u8", 53, 927),
    f("sound_category", "str32", 54, 929),
)

OBJECT_SPEC = (
    f("name", "str32", 108, 1667),
    f("mesh", "art", 109, 1668), f("gui_icon", "art", 110, 1669),
    f("in_hand_icon", "art", 111, 1670), f("in_hand_mesh", "art", 112, 1671),
    f("unknown_resource", "art", 113, 1672),
    f("additional_resources", "art", 114, 1673, count=4),
    f("light", "light", 115, 1681),
    f("width", "fixed32", 116, 1682), f("height", "fixed32", 117, 1683),
    f("mass", "fixed32", 118, 1684), f("speed", "fixed32", 119, 1685),
    f("air_friction", "fixed64", 120, 1686),
    f("material", "u8", 121, 1687),
    f("gap_1", "u8", 122, 1688, count=3),
    f("flags", "flag4", 123, 1693),
    f("hp", "u16", 124, 1694), f("max_angle", "u16", 125, 1695),
    f("mana_gen", "u16", 126, 1696), f("mana_value", "u16", 127, 1697),
    f("tooltip_string_id", "u16", 128, 1698),
    f("name_string_id", "u16", 129, 1699),
    f("slap_effect_id", "u16", 130, 1700),
    f("death_effect_id", "u16", 131, 1701),
    f("misc_effect_id", "u16", 132, 1702),
    f("object_id", "u8", 133, 1703), f("state", "u8", 134, 1704),
    f("room_capacity", "u8", 135, 1705),
    f("pick_up_priority", "u8", 136, 1706),
    f("sound_category", "str32", 137, 1708),
)

TERRAIN_SPEC = (
    f("name", "str32", 49, 683),
    f("complete", "art", 50, 684), f("side", "art", 51, 685),
    f("top", "art", 52, 686), f("tagged_top", "art", 53, 687),
    f("string_ids", "stringid", 54, 688),
    f("depth", "fixed32", 55, 689), f("light_height", "fixed32", 56, 690),
    f("flags", "flag4", 57, 691),
    f("damage", "u16", 58, 692),
    f("editor_texture_id", "u16", 59, 693),
    f("unk_1", "u16", 60, 694),
    f("gold_value", "u16", 61, 695),
    f("mana_gain", "u16", 62, 696),
    f("max_mana_gain", "u16", 63, 697),
    f("tooltip_string_id", "u16", 64, 698),
    f("name_string_id", "u16", 65, 699),
    f("max_health_effect_id", "u16", 66, 700),
    f("destroyed_effect_id", "u16", 67, 701),
    f("general_description_string_id", "u16", 68, 702),
    f("strength_string_id", "u16", 69, 703),
    f("weakness_string_id", "u16", 70, 704),
    f("unk_2", "u16", 71, 705, count=16),
    f("wibble_h", "u8", 72, 710),
    f("lean_h", "u8", 73, 711, count=3),
    f("wibble_v", "u8", 74, 716),
    f("lean_v", "u8", 75, 717, count=3),
    f("terrain_id", "u8", 76, 722),
    f("starting_health", "u16", 77, 723),
    f("max_health_type_terrain_id", "u8", 78, 724),
    f("destroyed_type_terrain_id", "u8", 79, 725),
    f("terrain_light", "color3", 80, 726),
    f("texture_frames", "u8", 81, 727),
    f("sound_category", "str32", 82, 729),
    f("max_health", "u16", 83, 730),
    f("ambient_light", "color3", 84, 731),
    f("sound_category_first_person", "str32", 85, 733),
    f("unk_3", "fixed32", 86, 734),
)


CATEGORIES = (
    # (json key, kwd file, expected count, expected record size, bt file, spec,
    #  fields to emit)
    ("creatures", "Creatures", 46, 5449, "creatures_struct.bt", CREATURE_SPEC, ()),
    ("rooms", "Rooms", 26, 1055, "rooms_struct.bt", ROOM_SPEC, ()),
    ("traps", "Traps", 16, 579, "traps_struct.bt", TRAP_SPEC, ()),
    ("keeper_spells", "KeeperSpells", 13, 406, "keeper_spells_struct.bt", KEEPER_SPELL_SPEC, ()),
    ("creature_spells", "CreatureSpells", 41, 266, "creature_spells_struct.bt", CREATURE_SPELL_SPEC, ()),
    ("doors", "Doors", 8, 616, "doors_struct.bt", DOOR_SPEC, ()),
    ("objects", "Objects", 145, 894, "objects_struct.bt", OBJECT_SPEC, ()),
    ("terrains", "Terrain", 41, 552, "terrains_struct.bt", TERRAIN_SPEC, ()),
)


def build_layouts():
    layouts = {}
    for key, _kwd, _count, size, bt_file, spec, _fields in CATEGORIES:
        layouts[key] = Layout(bt_file, size, spec)
    return layouts


LAYOUTS = build_layouts()


# ---------------------------------------------------------------------------
# Value decoding
# ---------------------------------------------------------------------------
def decode_scalar(kind, data, offset):
    if kind == "u8":
        return data[offset]
    if kind == "i8":
        return struct.unpack_from("<b", data, offset)[0]
    if kind == "u16":
        return struct.unpack_from("<H", data, offset)[0]
    if kind == "i16":
        return struct.unpack_from("<h", data, offset)[0]
    if kind == "u32":
        return struct.unpack_from("<I", data, offset)[0]
    if kind == "i32":
        return struct.unpack_from("<i", data, offset)[0]
    if kind == "fixed32":
        return round(struct.unpack_from("<i", data, offset)[0] / FLOAT_SCALE, 6)
    if kind == "fixed16":
        return round(struct.unpack_from("<h", data, offset)[0] / FLOAT_SCALE, 6)
    if kind == "fixed64":
        return round(struct.unpack_from("<i", data, offset)[0] / DOUBLE_SCALE, 6)
    if kind in ("str32", "str64"):
        raw = data[offset:offset + KIND_SIZE[kind]].split(b"\0", 1)[0]
        return raw.decode("latin-1")
    if kind == "flag4":
        return struct.unpack_from("<I", data, offset)[0]
    if kind in ("art", "unkart"):
        raw = data[offset:offset + 64].split(b"\0", 1)[0]
        return raw.decode("latin-1")
    if kind in ("light", "stringid", "color3", "footsteps", "bytes8"):
        return data[offset:offset + KIND_SIZE[kind]].hex()
    raise ValueError("not a scalar kind: %s" % kind)


def decode_field(data, field, bt_file=""):
    if field.kind in COMPOSITE:
        out = []
        stride = KIND_SIZE[field.kind]
        for index in range(field.count):
            item = {}
            sub_offset = 0
            for sub_name, sub_kind in COMPOSITE[field.kind]:
                sub_field = Field(sub_name, sub_kind, 1, field.bt_line,
                                  field.java_lines,
                                  field.offset + index * stride + sub_offset)
                item[sub_name] = entry(decode_scalar(sub_kind, data, sub_field.offset),
                                       field.source(bt_file))
                sub_offset += KIND_SIZE[sub_kind]
            out.append(item)
        return out
    if field.kind in ("u8", "i8", "u16", "i16", "u32", "i32", "fixed32",
                      "fixed16", "fixed64", "flag4"):
        if field.count == 1:
            return decode_scalar(field.kind, data, field.offset)
        return [decode_scalar(field.kind, data, field.offset + i * KIND_SIZE[field.kind])
                for i in range(field.count)]
    if field.kind in ("art", "unkart", "str32", "str64"):
        if field.count == 1:
            return decode_scalar(field.kind, data, field.offset)
        return [decode_scalar(field.kind, data, field.offset + i * KIND_SIZE[field.kind])
                for i in range(field.count)]
    if field.kind in ("light", "stringid", "color3", "footsteps", "bytes8"):
        return decode_scalar(field.kind, data, field.offset)
    raise ValueError("no decoder for %s" % field.kind)


def entry(value, source, confidence="high"):
    return {"value": value, "source": source, "confidence": confidence}


# ---------------------------------------------------------------------------
# Which fields go into stats.json, and how confident we are in each one.
# (field name, confidence)
# ---------------------------------------------------------------------------
CREATURE_FIELDS = (
    ("creature_id", "high"), ("order_in_editor", "high"),
    ("hp", "high"), ("hp_from_chicken", "high"), ("fear", "high"),
    ("threat", "high"), ("melee_damage", "high"), ("slap_damage", "high"),
    ("mana_gen_prayer", "high"), ("pay", "high"), ("max_gold_held", "high"),
    ("max_angle_change", "high"), ("decompose_value", "high"),
    ("anger_no_lair", "medium"), ("anger_no_food", "medium"),
    ("anger_no_pay", "medium"), ("anger_no_work", "medium"),
    ("anger_slap", "medium"), ("anger_in_hand", "medium"),
    ("initial_gold_held", "medium"), ("swipe_speed", "medium"),
    ("exp_for_next_level", "high"), ("exp_per_second", "high"),
    ("exp_per_second_training", "high"), ("research_per_second", "high"),
    ("manufacture_per_second", "high"),
    ("job_class", "medium"), ("fight_style", "medium"),
    ("speed", "high"), ("run_speed", "high"), ("shuffle_speed", "high"),
    ("hunger_rate", "medium"), ("hunger_fill", "medium"),
    ("time_awake", "high"), ("time_sleep", "high"),
    ("distance_can_see", "high"), ("distance_can_hear", "high"),
    ("stun_duration", "high"), ("guard_duration", "high"),
    ("idle_duration", "high"), ("slap_fearless_duration", "high"),
    ("width", "high"), ("height", "high"), ("mass", "medium"),
    ("eye_height", "high"), ("air_friction", "medium"),
    ("perception_range", "medium"), ("melee_range", "high"),
    ("melee_recharge", "high"), ("torture_time_to_convert", "high"),
    ("possession_mana_cost", "high"), ("own_land_health_increase", "high"),
    ("torture_hp_change", "high"), ("torture_mood_change", "high"),
    ("score_rating", "medium"), ("room_capacity_needed", "medium"),
    ("unhappy_threshold", "medium"), ("melee_attack_type", "high"),
    ("disarm_failure_percentage", "medium"), ("lair_object_id", "high"),
    ("first_person_ability1", "high"), ("first_person_ability2", "high"),
    ("melee1_swipe", "medium"), ("melee2_swipe", "medium"),
    ("melee3_swipe", "medium"), ("special1_swipe", "medium"),
    ("special2_swipe", "medium"), ("flags", "high"), ("flags2", "medium"),
    ("scale", "medium"), ("sniper_zoom_multiplier", "medium"),
    ("attractions", "high"), ("resistances", "high"),
    ("happy_jobs", "high"), ("unhappy_jobs", "high"), ("angry_jobs", "high"),
    ("hate_jobs", "high"), ("alternative_jobs", "high"),
    ("spells", "high"),
)

ROOM_FIELDS = (
    ("room_id", "high"), ("cost", "high"), ("research_time", "medium"),
    ("torch_intensity", "medium"), ("health_gain", "high"),
    ("terrain_id", "high"), ("return_percentage", "medium"),
    ("recommended_size_x", "medium"), ("recommended_size_y", "medium"),
    ("created_creature_id", "high"), ("construction", "medium"),
    ("ceiling_height", "medium"), ("torch_height", "medium"),
    ("torch_radius", "medium"), ("order_in_editor", "high"),
)

TRAP_FIELDS = (
    ("trap_id", "high"), ("health", "high"), ("health_gain", "high"),
    ("gold_cost", "high"), ("mana_cost_to_fire", "high"),
    ("mana_usage", "high"), ("mana_gen", "medium"),
    ("recharge_time", "high"), ("charge_time", "high"),
    ("threat_duration", "high"), ("threat", "high"),
    ("research_time", "high"), ("manuf_to_build", "high"),
    ("shots_when_armed", "high"), ("shot_delay", "high"),
    ("trigger", "medium"), ("order_in_editor", "high"),
)

KEEPER_SPELL_FIELDS = (
    ("keeper_spell_id", "high"), ("research_time", "high"),
    ("recharge_time", "high"), ("mana_drain", "high"),
    ("mana_cost", "high"), ("bonus_r_time", "medium"),
    ("cast_rule", "high"), ("target_rule", "high"),
    ("flags", "high"), ("shot_type_id", "medium"),
    ("order_in_editor", "high"),
)

CREATURE_SPELL_FIELDS = (
    ("creature_spell_id", "high"), ("combat_points", "high"),
    ("range", "high"), ("recharge_time", "high"),
    ("shot_type_id", "medium"), ("alternative_shot_id", "medium"),
    ("alternative_room_id", "medium"), ("alternative_shot", "medium"),
)

DOOR_FIELDS = (
    ("door_id", "high"), ("health", "high"), ("health_gain", "high"),
    ("gold_cost", "high"), ("mana_cost", "high"),
    ("research_time", "high"), ("manuf_to_build", "high"),
    ("flags", "high"), ("order_in_editor", "high"),
)

OBJECT_FIELDS = (
    ("object_id", "high"), ("hp", "high"), ("mana_gen", "medium"),
    ("mana_value", "high"), ("max_angle", "medium"),
    ("room_capacity", "medium"), ("pick_up_priority", "medium"),
    ("width", "high"), ("height", "high"), ("mass", "medium"),
    ("speed", "high"), ("air_friction", "medium"), ("state", "high"),
)

TERRAIN_FIELDS = (
    ("terrain_id", "high"), ("damage", "high"), ("gold_value", "high"),
    ("mana_gain", "high"), ("max_mana_gain", "high"),
    ("starting_health", "high"), ("max_health", "high"),
    ("depth", "medium"), ("light_height", "medium"),
    ("texture_frames", "medium"),
)

CATEGORY_FIELDS = {
    "creatures": CREATURE_FIELDS,
    "rooms": ROOM_FIELDS,
    "traps": TRAP_FIELDS,
    "keeper_spells": KEEPER_SPELL_FIELDS,
    "creature_spells": CREATURE_SPELL_FIELDS,
    "doors": DOOR_FIELDS,
    "objects": OBJECT_FIELDS,
    "terrains": TERRAIN_FIELDS,
}

# Creature flag bits, from Creature.java:107-138 (high 16 bits are in flags2,
# Creature.java:155-160, stored at the record end).
CREATURE_FLAG_BITS = {
    "is_worker": (0, 0x00000001), "can_be_picked_up": (0, 0x00000002),
    "can_be_slapped": (0, 0x00000004), "always_flee": (0, 0x00000008),
    "can_walk_on_water": (0, 0x00000010), "can_walk_on_lava": (0, 0x00000020),
    "can_disarm_traps": (0, 0x00000040), "is_evil": (0, 0x00000080),
    "is_immune_to_turncoat": (0, 0x00000200),
    "available_via_portal": (0, 0x00000400),
    "can_fly": (0, 0x00002000), "is_horny": (0, 0x00004000),
    "generate_dead_body": (0, 0x00008000),
    "can_be_hypnotized": (0, 0x00010000),
    "is_immune_to_chicken": (0, 0x00020000),
    "is_fearless": (0, 0x00040000),
    "can_be_electrocuted": (0, 0x00080000),
    "is_unique": (0, 0x08000000),
    "is_male": (0, 0x80000000),
    "immune_to_lightning": (1, 0x01), "is_stone_knight": (1, 0x02),
    "is_emotionless": (1, 0x04), "available_via_hero_portal": (1, 0x08),
}


def load_catalogues():
    catalogues = {}
    for key, name, count, size, bt_file, _spec, _fields in CATEGORIES:
        path = DATA_DIR / (name + ".kwd")
        kwd = KwdFile(str(path))
        if kwd.record_count != count or kwd.record_size != size:
            raise LayoutError("%s: expected %d records of %d bytes, got %d of %d"
                              % (path, count, size, kwd.record_count, kwd.record_size))
        catalogues[key] = kwd
    return catalogues


def _source_for(layout, name):
    field = layout.by_name()[name]
    return field.source(layout.bt_file)


def decode_creature_extras(stats, record, layout, rooms, creature_spells):
    """Named enums, flag booleans, attraction room names and spell names."""
    data = record.data

    melee = stats["melee_attack_type"]["value"]
    stats["melee_attack_type_name"] = entry(
        attack_type_name(melee), _source_for(layout, "melee_attack_type"))

    for key, (which, mask) in CREATURE_FLAG_BITS.items():
        raw = stats["flags" if which == 0 else "flags2"]["value"]
        stats["flag_" + key] = entry(bool(raw & mask), _source_for(
            layout, "flags" if which == 0 else "flags2"), "high")

    room_by_id = {decode_scalar("u8", r.data, layout_room_offset("room_id")):
                  r.name.strip() for r in rooms.records} if rooms else {}
    spell_by_id = {}
    if creature_spells:
        spell_layout = LAYOUTS["creature_spells"]
        for rec in creature_spells.records:
            sid = decode_scalar("u8", rec.data,
                                spell_layout.by_name()["creature_spell_id"].offset)
            spell_by_id[sid] = rec.name.strip()

    for attraction in stats["attractions"]["value"]:
        raw_id = attraction["room_id"]["value"]
        attraction["room_name"] = entry(room_by_id.get(raw_id),
                                        _source_for(layout, "attractions"), "high")
    for spell in stats["spells"]["value"]:
        sid = spell["creature_spell_id"]["value"]
        spell["spell_name"] = entry(spell_by_id.get(sid),
                                    _source_for(layout, "spells"), "high")
    for resistance in stats["resistances"]["value"]:
        resistance["attack_type_name"] = entry(
            attack_type_name(resistance["attack_type"]["value"]),
            _source_for(layout, "resistances"))


def layout_room_offset(name):
    return LAYOUTS["rooms"].by_name()[name].offset


def attack_type_name(value):
    names = (
        "NO_ATTACK", "MELEE_BLADE", "MELEE_BLUNT", "MELEE_BODY",
        "MISTRESS_LIGHTNING", "GAS_CLOUD", "GAS_MISSILE",
        "DARK_ELF_GUIDED_BOLT", "SLOW", "GRENADE", "DRAIN",
        "MISTRESS_HAIL_STORM", "DARK_ELF_ARROW", "HASTE_CREATURE",
        "WARLOCK_HEAL_CREATURE", "MELEE_SCYTHE", "INVULNERABLE",
        "THIEF_INVISIBLE", "KNIVES", "DISRUPTION", "RAISE_DEAD", "WIND",
        "SKELETON_ARMY", "CHICKEN_ARROW", "ELF_ARCHER_ARROW",
        "WIZARD_FIREBALL", "SALAMANDER_FIREBALL", "WIZARD_FIREBOMB_1",
        "WIZARD_FIREBOMB_2", "FAIRY_FREEZE", "ELVEN_ARCHER_GUIDED_BOLT",
        "DARK_ANGEL_HAIL_STORM", "MONK_HEAL_CREATURE", "ROGUE_INVISIBLE",
        "FAIRY_LIGHTNING", "DARK_ANGEL_FIREBOMB", "IMP_TELEPORT",
        "IMP_HASTE", "REAPER_FIREBALL", "SALAMANDER_SPIT", "MAIDEN_WEB",
        "MAIDEN_POISON_SPIT",
    )
    return names[value] if 0 <= value < len(names) else "UNKNOWN_%d" % value


def decode_all(catalogues=None):
    catalogues = catalogues or load_catalogues()
    result = {}
    for key, _name, count, _size, bt_file, _spec, _fields in CATEGORIES:
        layout = LAYOUTS[key]
        records = []
        for record in catalogues[key].records:
            name_field = layout.by_name()["name"]
            name = decode_scalar("str32", record.data, name_field.offset).strip()
            stats = {}
            for field_name, confidence in CATEGORY_FIELDS[key]:
                field = layout.by_name()[field_name]
                stats[field_name] = entry(decode_field(record.data, field, bt_file),
                                          field.source(bt_file), confidence)
            if key == "creatures":
                decode_creature_extras(stats, record, layout,
                                       catalogues.get("rooms"),
                                       catalogues.get("creature_spells"))
            records.append({
                "name": name,
                "index": record.index,
                "stats": stats,
            })
        if len(records) != count:
            raise LayoutError("%s: decoded %d records, expected %d"
                              % (key, len(records), count))
        result[key] = records
    return result


def build_document():
    catalogues = load_catalogues()
    data = decode_all(catalogues)
    meta = {
        "generator": "tools/stats_tool.py",
        "method": "Fixed offsets from the 010 Editor .bt layouts, validated "
                  "against OpenKeeper KwdFile.java read order and against the "
                  "record_size reported by tools/kwd_tool.py.",
        "float_encoding": "int32 / 4096 (ResourceChunkReader.readIntegerAsFloat, "
                          "ConversionUtils.FLOAT); int16 / 4096 (readShortAsFloat); "
                          "int32 / 65536 (readIntegerAsDouble, ConversionUtils.DOUBLE)",
        "inputs": {},
        "source_directories": {"templates": BT_DIR, "java": JAVA_MAP},
        "record_sizes": {},
        "confidence_levels": {
            "high": "template field order/size matches the Java reader and "
                    "decoded values are mutually consistent",
            "medium": "layout is verified, but field semantics, units or a stale "
                      "template comment leave room for interpretation",
        },
    }
    for key, kwd_name, count, size, bt_file, _spec, _fields in CATEGORIES:
        path = "Data/editor/%s.kwd" % kwd_name
        meta["inputs"][key] = path
        meta["record_sizes"][key] = {
            "count": count, "bytes": size, "template": "%s/%s" % (BT_DIR, bt_file),
        }
    return {"meta": meta, **data}


def write_stats(path=None):
    path = Path(path) if path else OUT_JSON
    document = build_document()
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, indent=2, sort_keys=False)
        handle.write("\n")
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("summary", "write", "creature"))
    parser.add_argument("name", nargs="?", help="creature name for the 'creature' command")
    args = parser.parse_args(argv)

    if args.command == "write":
        print(write_stats())
        return 0
    data = decode_all()
    if args.command == "summary":
        for key, _name, count, size, _bt, _spec, _fields in CATEGORIES:
            print("%-16s %3d records x %4d bytes" % (key, len(data[key]), size))
        print("\n%-22s %6s %6s %6s %7s %6s" %
              ("creature", "hp", "speed", "melee", "pay", "hunger"))
        for rec in data["creatures"]:
            s = rec["stats"]
            print("%-22.22s %6d %6.2f %6d %7d %6.2f" % (
                rec["name"], s["hp"]["value"], s["speed"]["value"],
                s["melee_damage"]["value"], s["pay"]["value"],
                s["hunger_rate"]["value"]))
        return 0
    if not args.name:
        parser.error("creature command needs a name")
    for rec in data["creatures"]:
        if rec["name"].lower() == args.name.lower():
            print(json.dumps(rec, ensure_ascii=False, indent=2))
            return 0
    print("no creature named %r" % args.name, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
