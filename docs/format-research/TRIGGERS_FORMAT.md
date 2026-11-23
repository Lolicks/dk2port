# Dungeon Keeper 2 `*Triggers.kld` format — decoded trigger chains

Reverse-engineered from the 66 shipped `*Triggers.kld` files and two format
authorities.  All integers are little-endian.  All byte offsets are absolute
file offsets unless marked as offsets inside a record.  Generated/verified with
`port/tools/trigger_tool.py`; machine-readable output in `port/docs/triggers.json`;
assertions in `port/tests/test_triggers.py`.

## 1. Authorities and what each one establishes

| Authority | Scope | Citations |
|---|---|---|
| ArchDemons 010 template | record skeleton, `ActionType` 1..51, `TargetType` 0..75, tags 213/214 | `port/research/dk2-tools/Formats/Level/triggers_struct.bt:10-62`, `:65-142`, `:145-148`, `:150-172` |
| OpenKeeper `KwdFile.readTriggers` | actual payload decode of every `tdata[8]` variant | `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/KwdFile.java:2373-2870` |
| OpenKeeper enums | `ComparisonType`, `MakeType`, flag bits, button ids | `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/TriggerGeneric.java:31-57`, `TriggerAction.java:100-153`, `:260-288` |
| OpenKeeper `Trigger` | meaning of `id`, `idNext`, `idChild`, `repeatTimes` | `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/Trigger.java:32-35`, `:89-103`, `:111-123` |
| Existing KWD research | container anomaly in the trigger files | `port/docs/KWD_FORMAT.md:191-198`, `:248-249`, `:285` |

The 010 template declares `TriggerGeneric = 213` and `TriggerAction = 214` as
`uint` tags (`triggers_struct.bt:145-148`); OpenKeeper uses the same constants
as `short` (`KwdFile.java:94-95`).  These are the binary chunk tags `0xD5` and
`0xD6` observed in every file.

**Confidence rule.** Byte reads (tag, size, `tdata`, ids, type, repeat) are
HIGH.  Name resolution and field layouts are MEDIUM because they come from an
independent implementation of the format, not from the shipped executable;
every observed type has an explicit case there (0 records used a fallback).
Chain ancestry is MEDIUM: the links are read from bytes, but the logical
AND/OR meaning of `idNext`/`idChild` is not established (UNKNOWN) and is never
asserted.  Numeric text ids are kept as raw integers; string-table resolution is
UNKNOWN and omitted.

## 2. Container layout

All 66 files share this exact shape (observed, HIGH):

```
+----------------------------------------------------------+ 0x0000
| root tag u32 = 0xD2 | version u32 = 4 | declared size u32 |
+----------------------------------------------------------+ 0x000C
| chunk 0xD3: [tag u32][size u32 = 32][32-byte info payload]|
+----------------------------------------------------------+ 0x0034
| chunk 0xD4: [tag u32][declared size u32]                  |
+----------------------------------------------------------+ 0x003C
| record[0] [tag u32][size u32 = 16][16-byte payload]       |
| record[1] ...                                            |
| ... continues to EOF                                     |
+----------------------------------------------------------+
```

Evidence: `Data/editor/maps/level1Triggers.kld` starts
`d2 00 00 00 04 00 00 00 2c 40 00 00 d3 00 00 00 20 00 00 00 64 01 00 00 46 01 00 00 ...`:
root `0xD2`, version 4, declared size `0x402C = 16428` = file size; info chunk
`0xD3` size 32; first two info words `0x164 = 356` (conditions) and `0x146 =
326` (actions); chunk `0xD4` at `0x34`; first nested `0xD5` at `0x3C`.

The `0xD4` declared size does **not** bound the record array; the array runs to
EOF.  This anomaly is already documented in `port/docs/KWD_FORMAT.md:191-198`.
The tool walks top-level chunks to find `0xD3`/`0xD4`, then reads fixed 24-byte
nested records while `tag ∈ {0xD5,0xD6}` and `size == 16`.

Validation over all 66 files (HIGH):

* root tag `0xD2`, version 4, declared size == file size: 66/66;
* info chunk `0xD3` (size 32) at `0x0C`, data chunk `0xD4` at `0x34`: 66/66;
* info condition/action counts == parsed `0xD5`/`0xD6` counts: 66/66;
* totals: **3753** conditions + **5408** actions = **9161** records;
* 6 maps are empty: `Small`, `Spiral`, `Swiss Cheese`, `The Deep End`,
  `U Turn`, `Warrens` (info counts are 0 there too).

## 3. `TriggerData` record — 24 bytes

From `triggers_struct.bt:150-172`, corroborated by OpenKeeper's read order
(`KwdFile.java:2383-2402`, `:2607-2611`, `:2616-2621`, `:2847-2851`).

| Abs. in record | Size | Type | Field | Meaning | Confidence |
|---:|---:|---|---|---|---|
| `+0x00` | 4 | u32 | `tag` | `0xD5` = condition, `0xD6` = action | HIGH |
| `+0x04` | 4 | u32 | `size` | always `16` = bytes after tag/size | HIGH |
| `+0x08` | 8 | bytes | `tdata[8]` | type-dependent payload (§5–§6) | HIGH (bytes), MEDIUM (fields) |
| `+0x10` | 2 | u16 | `id` | node id | HIGH |
| `+0x12` | 2 | u16 | `idNext` | sibling / next node id | HIGH |
| `+0x14` | 2 | u16 | `idChild` | child node id; `0` = none | HIGH |
| `+0x16` | 1 | u8 | `type` | `TargetType` if `0xD5`, `ActionType` if `0xD6` | HIGH |
| `+0x17` | 1 | u8 | `repeatTimes` | repeat count; `255` = always (`Trigger.java:35`) | HIGH |

OpenKeeper reads `type` and `repeatTimes` *first* by skipping `size - 2` bytes
from the post-header position (`KwdFile.java:2395-2400`, `:2616-2621`), then rewinds
and reads the payload, then ids (`KwdFile.java:2607-2611`, `:2847-2851`).  With
`size = 16` that places `type` at record `+0x16`, matching the template exactly.

### 3.1 ID spaces and links

Over all 9161 records (HIGH): every `0xD5` id is `< 0x8000` (1..~3600) and every
`0xD6` id is `>= 0x8000` (0x8000..0xFFFF).  Condition `idChild` values such as
`0xFFF8` therefore point at the action whose `id == 0xFFF8`; the values are
plain u16 link targets in one shared id space, not errors.  `idNext`/`idChild`
may cross kinds (e.g. an action's `idNext` pointing at a condition), so the
tool resolves links across both arrays.

`hasNext()`/`hasChildren()` treat `0` as "none" (`Trigger.java:111-123`);
`idChild == 0` occurs 5432 times, `idNext == 0` 4829 times.  `repeatTimes`
histogram: `1` ×6875, `0` ×2040, `255` ×232, `2` ×5, `30` ×5, `10` ×4.

## 4. Enums

### 4.1 `ActionType` (u8 at `+0x16` of a `0xD6` record)

Full source: `triggers_struct.bt:10-62`; OpenKeeper's subset:
`TriggerAction.java:33-98`.  Names in the tool are the 010 template's names.
OpenKeeper omits 19, 39, 40, 41; none of those appear in the shipped files.

| # | Name | # | Name | # | Name |
|---:|---|---:|---|---:|---|
| 1 | CREATE_CREATURE | 18 | SET_ALLIANCE | 35 | REMOVE_FROM_MAP |
| 2 | CREATE_PORTAL_GEM | 19 | MODIFY_CREATURE_POOL | 36 | SET_FIGHT_FLAG |
| 3 | SEND_TO_AP | 20 | ATTACH_PORTAL_GEM | 37 | SET_PORTAL_STATUS |
| 4 | DISPLAY_OBJECTIVE | 21 | FORCE_FIRST_PERSON | 38 | SET_WIDESCREEN_MODE |
| 5 | INFORMATION | 22 | ALTER_TERRAIN_TYPE | 39 | CREATE_DOOR |
| 6 | MAKE | 23 | SET_TIME_LIMIT | 40 | CREATE_TRAP |
| 7 | FLAG | 24 | PLAY_SPEECH | 41 | CREATE_EFFECT |
| 8 | INITIALIZE_TIMER | 25 | DISPLAY_TEXT_STRING | 42 | MAKE_OBJECTIVE |
| 9 | FLASH_BUTTON | 26 | ZOOM_TO_ACTION_POINT | 43 | ZOOM_TO |
| 10 | WIN_GAME | 27 | ROTATE_AROUND_ACTION_POINT | 44 | SET_CREATURE_MOODS |
| 11 | LOSE_GAME | 28 | GENERATE_CREATURE | 45 | SET_SYSTEM_MESSAGES |
| 12 | WIN_SUBOBJECTIVE | 29 | MAKE_HUNGRY | 46 | DISPLAY_SLAB_OWNER |
| 13 | LOSE_SUBOBJECTIVE | 30 | SHOW_HEALTH_FLOWER | 47 | DISPLAY_NEXT_ROOM_TYPE |
| 14 | CREATE_HERO_PARTY | 31 | FOLLOW_CAMERA_PATH | 48 | TOGGLE_EFFECT_GENERATOR |
| 15 | SET_OBJECTIVE | 32 | COLLAPSE_HERO_GATE | 49 | CHANGE_ROOM_OWNER |
| 16 | FLASH_ACTION_POINT | 33 | ALTER_SPEED | 50 | SET_SLAPS_LIMIT |
| 17 | REVEAL_ACTION_POINT | 34 | SET_MUSIC_LEVEL | 51 | SET_TIMER_SPEECH |

### 4.2 `TargetType` (u8 at `+0x16` of a `0xD5` record)

Full source: `triggers_struct.bt:65-142`; OpenKeeper's subset:
`TriggerGeneric.java:59-164`.  OpenKeeper omits 38, 48, 50, 51, 66, 75; none of
those appear in the shipped files.

| # | Name | # | Name | # | Name |
|---:|---|---:|---|---:|---|
| 0 | NONE | 26 | PLAYER_ROOM_SIZE | 52 | PLAYER_CREATURES_AT_LEVEL |
| 1 | FLAG | 27 | PLAYER_DOORS | 53 | GUI_BUTTON_PRESSED |
| 2 | TIMER | 28 | PLAYER_TRAPS | 54 | CREATURE_HUNGER_SATED |
| 3 | CREATURE_CREATED | 29 | PLAYER_KEEPER_SPELL | 55 | CREATURE_PICKS_UP_PORTAL_GEM |
| 4 | CREATURE_KILLED | 30 | PLAYER_GOLD | 56 | PLAYER_DUNGEON_BREACHED |
| 5 | CREATURE_SLAPPED | 31 | PLAYER_GOLD_MINED | 57 | PLAYER_ENEMY_BREACHED |
| 6 | CREATURE_ATTACKED | 32 | PLAYER_MANA | 58 | PLAYER_CREATURE_PICKED_UP |
| 7 | CREATURE_IMPRISONED | 33 | PLAYER_DESTROYS | 59 | PLAYER_CREATURE_DROPPED |
| 8 | CREATURE_TORTURED | 34 | LEVEL_TIME | 60 | PLAYER_CREATURE_SLAPPED |
| 9 | CREATURE_CONVERTED | 35 | LEVEL_CREATURES | 61 | PLAYER_CREATURE_SACKED |
| 10 | CREATURE_CLAIMED | 36 | CREATURE_HEALTH | 62 | AP_TAG_PART_OF |
| 11 | CREATURE_ANGRY | 37 | CREATURE_GOLD_HELD | 63 | CREATURE_SACKED |
| 12 | CREATURE_AFRAID | 38 | NOT_USED | 64 | PARTY_MEMBERS_INCAPACITATED |
| 13 | CREATURE_STEALS | 39 | AP_CONGREGATE_IN | 65 | CREATURE_PICKED_UP |
| 14 | CREATURE_LEAVES | 40 | AP_CLAIM_PART_OF | 66 | PLAYER_CREATURE_INTRODUCED |
| 15 | CREATURE_STUNNED | 41 | AP_CLAIM_ALL_OF | 67 | LEVEL_PLAYED |
| 16 | CREATURE_DYING | 42 | AP_SLAB_TYPES | 68 | PLAYER_ROOM_FURNITURE |
| 17 | DOOR_DESTROYED | 43 | PARTY_CREATED | 69 | AP_TAG_ALL_OF |
| 18 | OBJECT_CLAIMED | 44 | PARTY_MEMBERS_KILLED | 70 | AP_POSESSED_CREATURE_ENTERS |
| 19 | PLAYER_CREATURES | 45 | PARTY_MEMBERS_CAPTURED | 71 | PLAYER_SLAPS |
| 20 | PLAYER_HAPPY_CREATURES | 46 | LEVEL_PAY_DAY | 72 | GUI_TRANSITION_ENDS |
| 21 | PLAYER_ANGRY_CREATURES | 47 | PLAYER_KILLED | 73 | PLAYER_CREATURES_GROUPED |
| 22 | PLAYER_CREATURES_KILLED | 48 | PLAYER_PORTAL_GEM_IN_HAND | 74 | PLAYER_CREATURES_DYING |
| 23 | PLAYER_KILLS_CREATURES | 49 | CREATURE_EXPERIENCE_LEVEL | 75 | PLAYER_GEMS_COLLECTED |
| 24 | PLAYER_ROOM_SLABS | 50 | CREATURE_ENTERS_AP | | |
| 25 | PLAYER_ROOMS | 51 | PARTY_ENTERS_AP | | |

### 4.3 Auxiliary enums

* `ComparisonType` 0 NONE, 1 LESS_THAN, 2 LESS_OR_EQUAL_TO, 3 EQUAL_TO,
  4 GREATER_THAN, 5 GREATER_OR_EQUAL_TO, 6 NOT_EQUAL_TO —
  `TriggerGeneric.java:31-57`.
* `MakeType` 0 MISC_BUTTON, 1 ROOM, 2 CREATURE, 3 DOOR, 4 TRAP, 5 KEEPER_SPELL —
  `TriggerAction.java:100-127`.  Used by action `MAKE` (`KwdFile.java:2686`),
  action `FLASH_BUTTON` (`:2758`) and condition `GUI_BUTTON_PRESSED` (`:2577-2578`).
* Flag bitmasks of `FlagTargetValueActionType`: `0x002` VALUE, `0x004` TARGET,
  `0x008` EQUAL, `0x010` PLUS, `0x020` MINUS — `TriggerAction.java:129-153`.
  The action `FLAG` field at `tdata+1` is additionally documented as
  `Equal = 12 | Plus = 20 | Minus = 36` in `KwdFile.java:2679`; the tool leaves
  it as a raw integer.
* `ButtonType` ids 241..255 — `TriggerAction.java:260-288`.  It is a distinct
  enum from `MakeType`; the tool only uses `MakeType` where OpenKeeper does.

## 5. `tdata[8]` layouts — conditions (`0xD5`)

Each layout is read from `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/KwdFile.java:2403-2605` at the cited lines. `-:N` means N bytes
that OpenKeeper checks are zero (`readAndCheckNull`) or skips; they are kept as
`reserved_bytes` in the JSON. `c` = `ComparisonType`, `m` = `MakeType`.

| Target type (id) | tdata layout (offsets inside `tdata`) | Source |
|---|---|---|
| AP_CONGREGATE_IN (39), AP_POSESSED_CREATURE_ENTERS (70) | `+0 c`, `+1 player_id`, `+2 target_id`, `+3 target_type`, `+4 value:u32` | `KwdFile.java:2404-2411` |
| AP_SLAB_TYPES (42) | `+0 c`, `+1 player_id`, `+2 terrain_id`, `+3 -:1`, `+4 value:u32` | `:2413-2419` |
| AP_TAG_PART_OF (62), AP_TAG_ALL_OF (69), AP_CLAIM_PART_OF (40), AP_CLAIM_ALL_OF (41) | `+0 c`, `+1 player_id`, `+2 -:2`, `+4 value:u32` | `:2421-2431` |
| PLAYER_DUNGEON_BREACHED (56), PLAYER_ENEMY_BREACHED (57) | `+0 player_id`, `+1 -:7` | `:2433-2437` |
| PLAYER_KILLED (47) | `+0 player_id`, `+1 -:3`, `+4 value:u32` | `:2439-2443` |
| PLAYER_CREATURE_PICKED_UP (58), SLAPPED (60), SACKED (61) | `+0 creature_id`, `+1 -:7` | `:2445-2450` |
| PLAYER_CREATURE_DROPPED (59) | `+0 creature_id`, `+1 room_id`, `+2 -:6` | `:2452-2456` |
| PLAYER_CREATURES (19), PLAYER_HAPPY_CREATURES (20) | `+0 c`, `+1 creature_id`, `+2 flag`, `+3 player_id`, `+4 value:u32` | `:2458-2465` |
| PLAYER_ANGRY_CREATURES (21) | `+0 c`, `+1 creature_id`, `+2 -:2`, `+4 value:u32` | `:2467-2472` |
| PLAYER_CREATURES_KILLED (22), PLAYER_KILLS_CREATURES (23) | `+0 c`, `+1 target_id`, `+2 flag`, `+3 player_id`, `+4 value:u32` | `:2474-2481` |
| PLAYER_ROOMS (25), PLAYER_ROOM_SLABS (24), PLAYER_ROOM_SIZE (26), PLAYER_ROOM_FURNITURE (68) | `+0 c`, `+1 room_id`, `+2 flag`, `+3 player_id`, `+4 value:u32` | `:2483-2492` |
| PLAYER_DOORS (27), PLAYER_TRAPS (28), PLAYER_KEEPER_SPELL (29), PLAYER_DESTROYS (33) | `+0 c`, `+1 target_id`, `+2 flag`, `+3 player_id`, `+4 value:u32` | `:2494-2503` |
| PLAYER_SLAPS (71), PLAYER_GOLD (30), PLAYER_GOLD_MINED (31), PLAYER_MANA (32), PLAYER_CREATURES_GROUPED (73), PLAYER_CREATURES_DYING (74) | `+0 c`, `+1 -:1`, `+2 flag`, `+3 player_id`, `+4 value:u32` | `:2505-2517` |
| PLAYER_CREATURES_AT_LEVEL (52) | `+0 c`, `+1 target_id`, `+2 flag`, `+3 player_id`, `+4 value:u32` | `:2519-2526` |
| LEVEL_PAY_DAY (46), CREATURE_KILLED (4), SLAPPED (5), ATTACKED (6), IMPRISONED (7), TORTURED (8), CONVERTED (9), CLAIMED (10), ANGRY (11), AFRAID (12), STEALS (13), LEAVES (14), STUNNED (15), DYING (16), GUI_TRANSITION_ENDS (72), CREATURE_PICKED_UP (65), SACKED (63), PICKS_UP_PORTAL_GEM (55), HUNGER_SATED (54), PARTY_CREATED (43) | `+0 -:8` | `:2528-2549` |
| CREATURE_CREATED (3) | `+0 -:4`, `+4 value:u32` | `:2551-2554` |
| LEVEL_PLAYED (67), PARTY_MEMBERS_CAPTURED (45), CREATURE_EXPERIENCE_LEVEL (49), CREATURE_GOLD_HELD (37), CREATURE_HEALTH (36), LEVEL_TIME (34), LEVEL_CREATURES (35) | `+0 c`, `+1 -:3`, `+4 value:u32` | `:2556-2566` |
| PARTY_MEMBERS_KILLED (44), PARTY_MEMBERS_INCAPACITATED (64) | `+0 c`, `+1 unknown`, `+2 -:2`, `+4 value:u32` | `:2568-2574` |
| GUI_BUTTON_PRESSED (53) | `+0 target_type:m`, `+1 target_id`, `+2 -:2`, `+4 value:u32` | `:2576-2582` |
| FLAG (1) | `+0 c`, `+1 flag_id`, `+2 flag`, `+3 value_id`, `+4 value:u32` | `:2584-2590` |
| TIMER (2) | `+0 c`, `+1 timer_id`, `+2 flag`, `+3 value_id`, `+4 value:u32` | `:2592-2598` |
| any other type | `+0 -:8` (OpenKeeper logs "Unsupported Type of TriggerGeneric") | `:2600-2603` |

`flag`/`value_id` above are *not* guessed: OpenKeeper names them `flag` and
`targetId` and comments `0x1 = Value, !0x1 = Flag` / `timerId` respectively
(`:2584-2598`).  The tool uses `flag_id`, `timer_id`, `value_id` for readability
and keeps every raw value.

## 6. `tdata[8]` layouts — actions (`0xD6`)

Source for every row: `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/KwdFile.java:2624-2845`.

| Action type (id) | tdata layout | Source |
|---|---|---|
| ALTER_TERRAIN_TYPE (22) | `+0 terrain_id`, `+1 player_id`, `+2 -:2`, `+4 pos_x:u16`, `+6 pos_y:u16` | `KwdFile.java:2626-2632` |
| COLLAPSE_HERO_GATE (32) | `+0 -:4`, `+4 pos_x:u16`, `+6 pos_y:u16` | `:2634-2638` |
| CHANGE_ROOM_OWNER (49) | `+0 -:1`, `+1 player_id`, `+2 -:2`, `+4 pos_x:u16`, `+6 pos_y:u16` | `:2640-2646` |
| SET_ALLIANCE (18) | `+0 player_one_id`, `+1 player_two_id`, `+2 available`, `+3 -:5` | `:2648-2653` |
| SET_CREATURE_MOODS (44), SET_SYSTEM_MESSAGES (45), SET_TIMER_SPEECH (51), SET_WIDESCREEN_MODE (38), ALTER_SPEED (33), SET_FIGHT_FLAG (36), SET_PORTAL_STATUS (37), DISPLAY_SLAB_OWNER (46) | `+0 available`, `+1 -:7` | `:2655-2664`, `:2692-2698` |
| SET_SLAPS_LIMIT (50) | `+0 -:4`, `+4 value:u32` | `:2666-2669` |
| INITIALIZE_TIMER (8), SET_TIME_LIMIT (23) | `+0 timer_id`, `+1 -:3`, `+4 value:u32` | `:2671-2675`, `:2744-2748` |
| FLAG (7) | `+0 flag_id`, `+1 flag`, `+2 -:2`, `+4 value:u32` | `:2677-2682` |
| MAKE (6) | `+0 player_id`, `+1 type:m`, `+2 target_id`, `+3 available`, `+4 -:4` | `:2684-2690` |
| DISPLAY_NEXT_ROOM_TYPE (47), MAKE_OBJECTIVE (42), ZOOM_TO_ACTION_POINT (26) | `+0 target_id`, `+1 -:7` | `:2700-2705` |
| DISPLAY_OBJECTIVE (4) | `+0 objective_id:u32`, `+4 action_point_id`, `+5 -:3` | `:2707-2711` |
| PLAY_SPEECH (24) | `+0 speech_id:u32`, `+4 text`, `+5 introduction`, `+6 path_id:u16` | `:2713-2718` |
| DISPLAY_TEXT_STRING (25) | `+0 text_id:u32`, `+4 -:4` | `:2720-2724` |
| ATTACH_PORTAL_GEM (20), MAKE_HUNGRY (29), REMOVE_FROM_MAP (35), ZOOM_TO (43), WIN_GAME (10), LOSE_GAME (11), FORCE_FIRST_PERSON (21), LOSE_SUBOBJECTIVE (13), WIN_SUBOBJECTIVE (12) | `+0 -:8` (no parameters) | `:2726-2736` |
| SET_MUSIC_LEVEL (34), SHOW_HEALTH_FLOWER (30) | `+0 value:u32`, `+4 -:4` | `:2738-2742` |
| FOLLOW_CAMERA_PATH (31) | `+0 path_id`, `+1 action_point_id`, `+2 available`, `+3 -:5` | `:2750-2755` |
| FLASH_BUTTON (9) | `+0 type:m`, `+1 target_id`, `+2 available`, `+3 -:1`, `+4 value:u32` | `:2757-2763` |
| FLASH_ACTION_POINT (16) | `+0 action_point_id`, `+1 available`, `+2 -:2`, `+4 value:u32` | `:2765-2770` |
| REVEAL_ACTION_POINT (17) | `+0 action_point_id`, `+1 available`, `+2 -:6` | `:2772-2776` |
| ROTATE_AROUND_ACTION_POINT (27) | `+0 action_point_id`, `+1 available`, `+2 angle:u16`, `+4 time:u32` | `:2778-2783` |
| CREATE_CREATURE (1) | `+0 creature_id`, `+1 player_id`, `+2 level`, `+3 flag`, `+4 pos_x:u16`, `+6 pos_y:u16` | `:2785-2792` |
| SET_OBJECTIVE (15) | `+0 player_id`, `+1 type`, `+2 -:2`, `+4 action_point_id:u32` | `:2794-2799` |
| CREATE_HERO_PARTY (14) | `+0 party_id`, `+1 type`, `+2 -:2`, `+4 action_point_id`, `+5 -:3` | `:2801-2807` |
| TOGGLE_EFFECT_GENERATOR (48) | `+0 generator_id`, `+1 available`, `+2 -:6` | `:2809-2813` |
| GENERATE_CREATURE (28) | `+0 creature_id`, `+1 level`, `+2 -:6` | `:2815-2819` |
| INFORMATION (5) | `+0 information_id:u32`, `+4 action_point_id`, `+5 -:3` | `:2821-2825` |
| SEND_TO_AP (3) | `+0 -:4`, `+4 action_point_id`, `+5 -:3` | `:2827-2831` |
| CREATE_PORTAL_GEM (2) | `+0 object_id`, `+1 player_id`, `+2 -:2`, `+4 pos_x:u16`, `+6 pos_y:u16` | `:2833-2839` |
| any other type | `+0 -:8` | `:2841-2844` |

Text-reference fields are `objective_id` (DISPLAY_OBJECTIVE), `text_id`
(DISPLAY_TEXT_STRING), `speech_id` (PLAY_SPEECH) and `information_id`
(INFORMATION).  The tool reports them as integers; mapping them to `.str`
entries is not supported by the cited sources and is therefore omitted.

## 7. Cross-check with missions and concrete win/lose conditions

`docs/missions.json` supplies the campaign category, order and enemy name
(`port/docs/missions.json`, generated by `port/tools/mission_tool.py:227-359`).
The enemy string is a recovered UTF-16LE field, not inferred.

Totals from `docs/triggers.json`: 66 maps, 60 with triggers, 37 with ≥1
`WIN_GAME` action, 27 with ≥1 `LOSE_GAME` action.  All 24 campaign maps have a
`WIN_GAME` action; 21 of them also have `LOSE_GAME`.  `WIN_GAME` occurs 87 times
across the catalog; `LOSE_GAME` is frequent in the timed/secret maps.

Examples where the data itself supports a concrete objective chain (ancestor
conditions reached through reverse `child`/`next` links; **combination semantics
UNKNOWN**, so read these as "conditions present in the chain", not as a proved
AND formula):

| Mission (enemy from `missions.json`) | Action | Conditions in the ancestor chain (raw decoded values) |
|---|---|---|
| `level1` (Antonius) | WIN_GAME id 65458, direct parent condition 204 via `child` | TIMER(12) ≥ 6; PLAYER_CREATURES ≥ 3 (any creature, any player); PLAYER_ROOMS(3) ≥ 1; PLAYER_GOLD ≥ 12000; FLAG(20) == 1 |
| `level5` (Constantine) | WIN_GAME id 65533; LOSE_GAME id 65503 | FLAG(9) == 1; PLAYER_KILLED player_id=1; LEVEL_TIME ≥ 0 — the same PLAYER_KILLED/LEVEL_TIME pair precedes LOSE_GAME |
| `level7` (Sigmund) | WIN_GAME id 65524; LOSE_GAME id 65516 | FLAG(9) == 1; PLAYER_KILLED player_id=0; LEVEL_TIME ≥ 0 |
| `Level14` (Tiberius) | WIN_GAME id 65480; LOSE_GAME id 65479 | FLAG(11) == 1; PLAYER_KILLED player_id=0; PLAYER_CREATURES creature_id=27 == 1; FLAG(12)/FLAG(3) == 1; LEVEL_TIME ≥ 0 |
| `Level15a` (Volstag) | WIN_GAME id 65510 via `child` of condition 18; LOSE_GAME id 65509 | FLAG(9) == 1; PLAYER_KILLED player_id=1; LEVEL_TIME ≥ 0 |
| `Level16` (Pureheart) | WIN_GAME id 65463; LOSE_GAME id 65462 | WIN: FLAG(126) == 1, LEVEL_TIME ≥ 0. LOSE: PLAYER_KILLED player_id=0 value=1, FLAG(126) == 1 |
| `Level20` (Reginald) | WIN_GAME id 65459 | FLAG(19) == 2, FLAG(18/14/9/10/11) == 1, PLAYER_KILLED player_id=1, FLAG(8) > 9, LEVEL_TIME ≥ 0 |
| `Level10` (Ronin) | LOSE_GAME ids 65337/65332 | FLAG(19)/FLAG(20) == 1; PLAYER_KILLED player_id=0; several LEVEL_TIME thresholds (790, 1040, 1400) |

The `PLAYER_KILLED` layout is `player_id:u8, -:3, value:u32`
(`KwdFile.java:2439-2443`), so the `value` printed for id 47 is a raw u32 whose
semantics the source marks `FIXME unknown`; it is reported, never interpreted.
`LEVEL_TIME ≥ 0` is the literal decoded comparison/value pair; the source gives
no reason why zero is used as a threshold.

## 8. Reproduce

Commands run from `/home/kali/Desktop/rea/Dungeon Keeper 2/port`:

```
python3 tools/trigger_tool.py list level1
python3 tools/trigger_tool.py json level1 --out /tmp/level1.json
python3 tools/trigger_tool.py catalog --out docs/triggers.json
python3 -m pytest tests/test_triggers.py -v
```

`catalog` regenerated `docs/triggers.json` byte-for-byte on repeated runs
(deterministic ordering from `docs/game_data.json`; no timestamps in output).
The tool reads the shipped files on every invocation; nothing is hard-coded
from a previous run.  `python3 -m py_compile tools/trigger_tool.py` succeeds.

## 9. Known limits

* The logical role of `idNext` vs `idChild` (AND/OR/gating) is not proved; the
  tool reports links and ancestry, not a Boolean objective formula.
* `repeatTimes` values other than 0/1/255 are retained raw (5, 10, 30 occur).
* Numeric text ids are not resolved to `.str` entries.
* `type`/`flag` fields that OpenKeeper marks `FIXME unknown` are kept raw.
* The 010 template's `MODIFY_CREATURE_POOL`/`CREATE_DOOR`/`CREATE_TRAP`/
  `CREATE_EFFECT` and the TargetType gaps have no case in OpenKeeper; no shipped
  record uses them, so no fallback guess was needed (0 default-layout records).
