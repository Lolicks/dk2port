# STATS_RECOVERY — real numeric stats from `Data/editor/*.kwd`

All numbers in `docs/stats.json` were decoded from the shipped KWD catalogues; none
were typed in by hand and none were copied from a wiki.  This document records how
the field offsets were derived, what was cross-checked, and what remains unknown.

## 1. Inputs

| Artefact | Role |
|---|---|
| `Data/editor/Creatures.kwd` et al. (repo root) | 8 catalogues, 46 creatures |
| `port/tools/kwd_tool.py` | already-existing KWD container reader: root `[tag u32, version=4, size u32]`, chunks `[tag u32, byte_len u32, payload]`, `root+1` info chunk (28 B, record count at offset 0), `root+2` data chunk, fixed-size records, `Record.name` = first 28 ASCII bytes |
| `port/research/dk2-tools/Formats/Level/*.bt` | 010 Editor field order, field names, enum semantics |
| `port/research/OpenKeeper/.../map/KwdFile.java` | authoritative sequential reader; confirms field size and order |
| `port/research/OpenKeeper/.../map/{Creature,Room,Trap,KeeperSpell}.java` | field semantics/getters |
| `port/docs/game_data.json`, `port/docs/balance.json` | name lists and the numbers quoted from the shipped manual |

Record geometry, verified by `kwd_tool` and asserted by the layout builder:

| Category | KWD | Records | Record bytes | Template |
|---|---|---:|---:|---|
| creatures | Creatures.kwd | 46 | 5449 | `creatures_struct.bt` |
| rooms | Rooms.kwd | 26 | 1055 | `rooms_struct.bt` |
| traps | Traps.kwd | 16 | 579 | `traps_struct.bt` |
| keeper spells | KeeperSpells.kwd | 13 | 406 | `keeper_spells_struct.bt` |
| creature spells | CreatureSpells.kwd | 41 | 266 | `creature_spells_struct.bt` |
| doors | Doors.kwd | 8 | 616 | `doors_struct.bt` |
| objects | Objects.kwd | 145 | 894 | `objects_struct.bt` |
| terrain | Terrain.kwd | 41 | 552 | `terrains_struct.bt` |

## 2. Offset derivation

`stats_tool.py` declares each record as an ordered list of fields.  For every field
it stores the template file and line, and (where available) the corresponding Java
read line.  The `Layout` constructor accumulates field sizes, assigns offsets, and
**fails unless the computed total equals `kwd_tool`'s `record_size`**.  All eight
layouts pass this check, which rules out a systematic off-by-one in field order.

Primitive/composite widths (from the templates, confirmed by Java):

* `ArtResource` = **84 bytes**: `name[64] + flags u32 + data[12] + type/startAf/endAf/sometimesOne u8×4`. OpenKeeper states this explicitly at `KwdFile.java:753-754`; the read routine is `KwdFile.java:759-837`. The `reader.skipBytes(4)` at `KwdFile.java:829` covers the last four bytes already peeked at `KwdFile.java:768-775`, so it does not add four more.
* `Light` = 24 bytes (`KwdFile.java:1623-1643`), `StringId` = 24 bytes (`KwdFile.java:854-870`), `ColorRGB` = 3 (`KwdFile.java:845-847`).
* Creature composites: `Attraction` 8 (`creatures_struct.bt:88-92`), `Spell` 24 (`:94-105`), `Resistance` 2 (`:152-155`), `JobPreference` 12 (`:157-165`), `JobAlternative` 8 (`:167-171`), `Vector3f` 12.
* Enum/bitfield widths: `JobType`/`AttractionType` are `<uint>` (`creatures_struct.bt:10`, `Creature.java:309-326`, `KwdFile.java:1357`), `AttackType`/`Swipe`/`SpecialAbility` are `<ubyte>` (`:42`, `:69`, `:107`), `CreatureFlag` is a full u32 (`creatures_struct.bt:188-222`, `KwdFile.java:1452-1453`), `RoomFlag`/`TrapFlag`/`DoorFlag`/`ObjectFlag`/`TerrainFlag`/`KeeperSpellFlag`/`CreatureSpellFlag` occupy 4 bytes including padding.

### Floating point is fixed point

The templates write `float32`/`float16`, but the bytes are **not** IEEE-754:

* `ResourceChunkReader.java:115-117`: `readIntegerAsFloat() = buffer.getInt() / ConversionUtils.FLOAT`
* `ResourceChunkReader.java:135-137`: `readShortAsFloat() = buffer.getShort() / ConversionUtils.FLOAT`
* `ResourceChunkReader.java:100-102`: `readIntegerAsDouble() = buffer.getInt() / ConversionUtils.DOUBLE`
* `ConversionUtils.java:38-39`: `FLOAT = 4096f`, `DOUBLE = 65536f`

Examples from the shipped file: Imp `speed` raw `0x1D70` = 7536 → `1.839844` tiles/s
(`creatures_struct.bt:326`, `KwdFile.java:1433`); Bile Demon `0x0666` = 1638 →
`0.399902`. Reading the same bytes as IEEE floats yields ~`1e-41`, i.e. nonsense.

### Creature field table (selected)

All offsets are computed by the layout builder; the template hex comments agree for
the anchor fields.  `Template` cites the field definition; `Java` cites the read.

| Field | Offset | Template | Java | Imp | Bile Demon | Black Knight |
|---|---:|---|---|---:|---:|---:|
| `name` | 0x0000 | `creatures_struct.bt:243` | `KwdFile.java:1283` | Imp | Bile Demon | Black Knight |
| `swipe_speed` | 0x0CEC | `:283` | `:1326` | — | — | — |
| `perception_range` | 0x0D01 | `:291` | `:1334` | (decoded) | (decoded) | (decoded) |
| `attractions[2]` | 0x0DAC | `:310` | `:1354-1375` | none | Workshop, Hatchery 25 | Combat Pit |
| `spells[3]` | 0x0DC4 | `:313` | `:1378-1396` | Imp Haste, Imp Teleport | Gas Cloud, Gas Missile | none |
| `resistances[4]` | 0x0E0C | `:314` | `:1398-1404` | none | 100 vs guided bolt | none |
| `happy_jobs[3]` | 0x0E14 | `:315` | `:1406`, `:1607-1621` | Explore 100% | Train 40%, Manufacture 60% | Train 95%, Guard 5% |
| `width` | 0x0EA0 | `:321` | `:1428` | (decoded) | (decoded) | (decoded) |
| `speed` | 0x0EB4 | `:326` | `:1433`, `Creature.java:1986` | 1.839844 | 0.399902 | 0.5 |
| `run_speed` | 0x0EB8 | `:327` | `:1434` | 1.839844 | 0.5625 | 0.9 |
| `hunger_rate` | 0x0EBC | `:328` | `:1435`, `Creature.java:2002` | 32767 | 220 | 200 |
| `time_awake/time_sleep` | 0x0EC0/0x0EC4 | `:329-330` | `:1436-1437`, `Creature.java:2010` | 32767/0 | 270/9 | 300/9 |
| `flags` | 0x0EFC | `:345` | `:1453`, `Creature.java:105-138` | worker+evil | evil | evil |
| `hp` | 0x0F0C | `:353` | `:1461`, `Creature.java:1906,2138` | 500 | 6000 | 4000 |
| `fear/threat` | 0x0F10/0x0F12 | `:355-356` | `:1463-1464` | 5/2 | 300/120 | 533/213 |
| `melee_damage` | 0x0F14 | `:357` | `:1465`, `Creature.java:532,1169` | 30 | 525 | 400 |
| `pay` | 0x0F1C | `:361` | `:1469`, `Creature.java:1911,2186` | 0 | 600 | 750 |
| `first_person_ability1/2` | 0x0F46 | `:384-385` | `:1492-1493` | 0/0 | 1/0 | 0/0 |
| `creature_id` | 0x0F4B | `:387` | `:1499` | 1 | 3 | 22 |
| `flags2` | 0x1545 | `:432` | `:1573`, `Creature.java:155-160` | 0 | 0 | 0 |

Full field lists with per-value `source`/`confidence` are in `docs/stats.json`.

### Other categories (selected)

| Category | Fields (template → Java) |
|---|---|
| rooms | `cost` `rooms_struct.bt:63` → `KwdFile.java:1057` / `Room.java:146`; `room_id` `:69` → `:1068` / `Room.java:152`; `health_gain` `:82` → `:1087` / `Room.java:165` |
| traps | `health` `traps_struct.bt:58` → `KwdFile.java:978` / `Trap.java:105`; `health_gain` `:81` → `:1008` / `Trap.java:128`; `recharge_time` `:47` → `:967` / `Trap.java:94` |
| keeper spells | `research_time` `keeper_spells_struct.bt:58` → `KwdFile.java:1942` / `KeeperSpell.java:150`; `mana_cost` `:77` → `:1962` / `KeeperSpell.java:170` |
| creature spells | `range` `creature_spells_struct.bt:38` → `KwdFile.java:1750`; `combat_points` `:40` → `:1752` |
| doors | `health` `doors_struct.bt:39` → `KwdFile.java:909`; `gold_cost` `:40` → `:910` |
| objects | `hp` `objects_struct.bt:124` → `KwdFile.java:1694`; `mana_value` `:127` → `:1697` |
| terrain | `starting_health` `terrains_struct.bt:77` → `KwdFile.java:723`; `max_health` `:83` → `:730` |

## 3. Cross-checks that actually passed

1. **Record geometry** — all 8 computed layout sizes equal the shipped record sizes
   (table in §1).  `LayoutTests` asserts this plus 13 creature anchor offsets and
   three room offsets.
2. **Manual room costs** — 11 of the documented values in `docs/balance.json` match
   the decoded `cost` field exactly: Treasury 200, Lair 300, Hatchery 300, Training
   Room 500, Combat Pit 750, Torture Chamber 1500, Prison 750, Graveyard 2000,
   Wooden Bridge 200, Stone Bridge 500, Guard Room 600.
3. **Manual creature requirements** (`docs/balance.json`, derived from the manual):
   * Bile Demon → Workshop + Hatchery with `room_size = 25` (the documented 5×5).
   * Bile Demon spells → `Gas Cloud`, `Gas Missile` (decoded from the 3 spell slots).
   * Warlock → Library, `Warlock Fireball`, `Warlock Heal Creature`, `Warlock Firebomb`.
   * Black Knight → Combat Pit; Imp → `Imp Haste` + `Imp Teleport` (balance.json note).
4. **Relative plausibility** — Imp `hp=500`, fastest common worker (`speed=1.84`);
   Bile Demon `hp=6000` but `speed=0.40`; Black Knight `threat=533` vs Imp `2`;
   Dark Angel `melee_damage=840` vs Warlock `130`; Jack `hp=240`, `speed=1.90`;
   Unique Dark Angel `hp=8000`.  All 46 creatures have nonzero HP and speed.
5. **Names** — 46/46 creature, 16/16 trap, 13/13 keeper spell, 41/41 creature
   spell, 8/8 door names equal `game_data.json` (room names match after truncating
   our full 32-byte field to the 28 bytes used by `kwd_tool.Record.name`).
6. **Reproducibility** — `stats_tool.build_document() == docs/stats.json` (tested).

## 4. Remaining unknowns and deliberate non-claims

* **Stale template comment.** `creatures_struct.bt:375` annotates
  `generalDescriptionStringId` as `f3c`, but the field order (and Java read order)
  puts it at `0xF38`; `strength` is `0xF3A`, `weakness` `0xF3C`,
  `slapEffectId` `0xF3E` (matching the comment at `:378`).  We follow the field
  order, which is the only layout that reproduces the 5449-byte record.
* **Room `research_time`** decodes to 40899 for Treasury/Lair/Portal, while the
  next u16 (180/160/128) looks like a research time.  Java reads the fields in that
  order (`KwdFile.java:1052-1053`) and `Room.java:141` documents the first as
  `researchTime`; confidence is therefore `medium`.  We do not swap them.
* **Manual disagreements left as-is:** Temple (manual 750, KWD 3000), Casino
  (manual 2000, KWD 750), Library (manual 1500, KWD 600), Portal (manual 750, KWD
  0).  These are recorded, not "corrected"; they may reflect a different manual
  edition or a different cost concept.
* **`hunger_rate = 32767`** for Imp/Skeleton/Vampire/Horny/Stone Knight is the raw
  value (fixed point 32767.0).  It plausibly encodes "does not starve", but the
  game-side interpretation was not verified; confidence `medium`.
* **Fields the templates mark as unused by the game** (`mana_gen` in traps at
  `traps_struct.bt:68` and objects at `objects_struct.bt:126`, `swipe_speed`,
  anger string IDs etc.) are decoded faithfully but marked `medium`; a decoded
  value is not a claim about gameplay effect.
* **Embedded-creature variant.** `creatures_struct.bt:433-439` appends 80 bytes and
  moves `flags2` for creatures embedded in globals (record size ≥ 5537).  The
  shipped `Creatures.kwd` records are 5449 bytes, so that branch does not apply.
* **Room IDs are 1-based.** `roomId` in attraction records is the value of the
  room's own `room_id` field at offset 914 (`Room.java:152`), not the record index:
  id 4 = Hatchery (index 3), id 10 = Work Shop (index 9), id 16 = Combat Pit
  (index 15).  Empty slots store 0 and are reported as `null` names.
* **No numeric creature stat table exists** in the shipped `manual.txt` or
  `reference_card.txt`; the manual only describes abilities.  Therefore the
  creature cross-check is against OpenKeeper's parser, relative plausibility and
  the documented room/spell/attraction requirements — not against invented
  "card values".
* **Every JSON leaf carries `source` and `confidence`** (`high` = template order
  agrees with the Java reader and the data is mutually consistent; `medium` =
  layout verified, semantics/units or a stale comment remain uncertain).  Unknown
  fields were omitted rather than guessed; this includes game-side meanings of
  the `unk_*` fields, which are not emitted at all.

## 5. How to reproduce

```bash
cd port
python3 tools/stats_tool.py summary          # all 46 creature stats at a glance
python3 tools/stats_tool.py write            # regenerate docs/stats.json
python3 tools/stats_tool.py creature 'Bile Demon'
python3 -m unittest tests.test_stats -v
```

The tests cover layout/record-size agreement, anchor offsets, fixed-point decoding,
record counts, name consistency with `game_data.json`, source-line resolvability
for every JSON leaf, plausible ranges, key-creature checks, manual cross-checks and
byte-for-byte reproducibility of `stats.json`.
