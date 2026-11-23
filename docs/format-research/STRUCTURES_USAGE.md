# STRUCTURES_USAGE — recovered catalogue data in the engine

This document describes the generated `engine/Structures.generated.hpp`, the standalone
API in `engine/Structures.{h,cpp}`, and the exact places in `engine/DK2.cpp` where the
data should replace the current manual tables. Nothing in `DK2.cpp` was modified here;
all wiring is left to the integrator.

## 1. Provenance and regeneration

```
python3 tools/gen_structures.py        # reads docs/stats.json -> engine/Structures.generated.hpp
```

`docs/stats.json` was decoded from the shipped `Data/editor/*.kwd` catalogues by
`tools/stats_tool.py`; the offsets, cross-checks and confidence policy are recorded in
`docs/format-research/STATS_RECOVERY.md`. The generator is deterministic (stdlib only)
and emits one flat C-style array per catalogue. Re-running it produces byte-identical
output (verified by SHA-256). Do not edit the generated header by hand.

Record counts asserted by the generator and checked by the self-test:

| Table | Records | Source |
|---|---:|---|
| `kDK2Rooms` / `DK2RoomDef` | 26 | `Rooms.kwd` |
| `kDK2Traps` / `DK2TrapDef` | 16 | `Traps.kwd` |
| `kDK2Doors` / `DK2DoorDef` | 8 | `Doors.kwd` |
| `kDK2Terrains` / `DK2TerrainDef` | 41 | `Terrain.kwd` |
| `kDK2Objects` / `DK2ObjectDef` | 145 | `Objects.kwd` |

Every leaf in `stats.json` carries `source` and `confidence`:

* **high** — template field order/size matches the Java reader and the decoded values
  are mutually consistent.
* **medium** — layout is verified, but field semantics, units or a stale template
  comment leave room for interpretation (e.g. room `research_time`, trap/object
  `mana_gen`, terrain `depth`/`light_height`/`texture_frames`, several object fields).

The generator emits a field only when it is present in **every** record of the section
and its confidence is high or medium. No low-confidence, unknown or `unk_*` fields exist
in `stats.json`; all fields of all five sections qualify and are emitted. Medium
confidence means "use with the caveat below", not "unreliable number".

## 2. Field mapping

### `DK2RoomDef` (26)
`name`, `room_id`, `terrain_id`, `cost`, `research_time`, `health_gain`,
`created_creature_id`, `order_in_editor`, `construction`, `recommended_size_x`,
`recommended_size_y`, `return_percentage`, `ceiling_height`, `torch_height`,
`torch_intensity`, `torch_radius`.

`cost` is **high** confidence: 11 of the manual prices in `docs/balance.json` match it
exactly (Treasury 200, Lair/Hatchery 300, Training Room 500, Combat Pit 750, ...).
Where the manual disagrees (Temple 750 vs KWD 3000, Casino 2000 vs KWD 750, Library
1500 vs KWD 600, Portal 750 vs KWD 0) the KWD value is emitted **as decoded**; this is
not a correction of the manual. `research_time` is **medium**: it decodes to 40899 for
the first 19 rooms, while the following u16 (180/160/128) looks like a plausible
research time. `STATS_RECOVERY.md` §4 deliberately keeps the decoded order.

### `DK2TrapDef` (16)
`name`, `trap_id`, `order_in_editor`, `gold_cost`, `mana_usage`, `mana_gen`,
`mana_cost_to_fire`, `manuf_to_build`, `research_time`, `health`, `health_gain`,
`charge_time`, `recharge_time`, `shot_delay`, `shots_when_armed`, `threat`,
`threat_duration`, `trigger`.

All **high** except `mana_gen` and `trigger` (**medium**). `mana_gen` is marked unused
by the game in `traps_struct.bt:68`.

### `DK2DoorDef` (8)
`name`, `door_id`, `order_in_editor`, `gold_cost`, `mana_cost`, `manuf_to_build`,
`research_time`, `health`, `health_gain`, `flags` (all **high**).
`flags` is a decoded bitfield; the *interpretation* of its bits is inferred from the
records (Secret Door 32, Barricade 64, Hero Barricade 192 = 64|128, Portcullis 128) and
is exposed through `door_is_secret()` / `door_is_barricade()` / `door_is_hero()`.

### `DK2TerrainDef` (41)
`name`, `terrain_id`, `gold_value`, `mana_gain`, `max_mana_gain`, `starting_health`,
`max_health`, `damage`, `depth`, `light_height`, `texture_frames`.

`gold_value`, `mana_gain`, `max_mana_gain`, `starting_health`, `max_health` and `damage`
are **high**; `depth`, `light_height`, `texture_frames` are **medium**.
`gold_value` is 3000 for Gold and Gems and 0 elsewhere; `mana_gain` is 100 for Claimed
Vault (id 32) and 1 for Claimed Path / Reinforced Wall.

### `DK2ObjectDef` (145)
`name`, `object_id`, `hp`, `mana_value`, `mana_gen`, `state`, `width`, `height`, `speed`,
`mass`, `air_friction`, `max_angle`, `pick_up_priority`, `room_capacity`.

**High**: `object_id`, `hp`, `mana_value`, `state`, `width`, `height`, `speed`.
**Medium**: `mana_gen` (unused per template), `mass`, `air_friction`, `max_angle`,
`pick_up_priority`, `room_capacity`.

## 3. Identifiers: 1-based KWD vs 0-based engine

`*_id` fields are the **1-based** values stored in the KWD records. `DK2.cpp`'s
`TerrainId` enum is **0-based** (`ImpenetrableRock = 0`, `Gold = 5`, ...), so a
conversion is required at the boundary:

```cpp
const DK2TerrainDef* def = dk2::structures::terrain_for_engine_id(tile->terrain);
// or: int stats_id = dk2::structures::engine_terrain_to_stats_id(tile->terrain);
```

`stats_terrain_to_engine_id()` / `engine_terrain_to_stats_id()` return `-1` for out of
range input. The engine enum only names ids 0..24; stats ids 26..41 (Stone Bridge, Fog
of War, Hero gates, ...) have no `TerrainId` enumerator and should be handled as raw
integers until the enum is extended.

Rooms are also 1-based and are **not** the same as `TerrainId`: room `Treasury` has
`room_id == 1`, `terrain_id == 10`, while the engine's `TerrainId::Treasury == 9`.
Use `room_terrain_id()` only to cross-reference the two catalogues.

## 4. Derived values (read this before wiring)

The KWD terrain record contains a `TerrainFlag` bitfield that `stats_tool.py` does not
emit (unknown semantics). Consequently:

* `terrain_is_minable()` is derived directly from the generated data:
  `gold_value > 0` (true only for Gold and Gems). It means "yields resources when
  mined", not "can be dug through".
* `terrain_is_walkable()` uses a small curated id list in `Structures.cpp`
  (`kWalkableStatsTerrainIds`) that mirrors the traversable cases of the existing
  `terrain_walkable()` switch in `DK2.cpp`, shifted by +1, plus Stone Bridge (26) as
  an inferred walkable bridge. It is a migration aid, not a recovered flag. Replace
  or extend the list if the authoritative flags are decoded later.

Name lookups (`*_by_name`) ignore case and all non-alphanumeric characters, so
`"Work Shop"`, `"workshop"` and `"Work-Shop"` all resolve to room id 10. No two names
within a table collide after this normalization (checked by `validate_all()`).

## 5. Public API (signatures)

`engine/Structures.h`, namespace `dk2::structures`, no dependency on `DK2.h`:

```cpp
size_t room_count(), trap_count(), door_count(), terrain_count(), object_count();
const DK2RoomDef*    room_by_index(size_t),    * room_by_id(int),    * room_by_name(const char*);
const DK2TrapDef*    trap_by_index(size_t),    * trap_by_id(int),    * trap_by_name(const char*);
const DK2DoorDef*    door_by_index(size_t),    * door_by_id(int),    * door_by_name(const char*);
const DK2TerrainDef* terrain_by_index(size_t), * terrain_by_id(int), * terrain_by_name(const char*);
const DK2ObjectDef*  object_by_index(size_t),  * object_by_id(int),  * object_by_name(const char*);

int   room_gold_cost(const DK2RoomDef*), room_research_time(...), room_health_gain(...);
int   room_terrain_id(...), room_created_creature_id(...), room_recommended_width(...), room_recommended_height(...);
int   trap_gold_cost(...), trap_health(...), trap_research_time(...), trap_manufacture(...);
int   trap_mana_usage(...), trap_mana_cost_to_fire(...), trap_shots_when_armed(...), trap_trigger(...);
float trap_charge_time(...), trap_recharge_time(...), trap_shot_delay(...);
int   door_gold_cost(...), door_mana_cost(...), door_health(...), door_research_time(...);
int   door_manufacture(...), door_flags(...);
bool  door_is_secret(...), door_is_barricade(...), door_is_hero(...);
int   object_hp(...), object_mana_value(...), object_state(...);
float object_width(...), object_height(...), object_speed(...);

int   terrain_gold_value(const DK2TerrainDef*), terrain_mana_gain(...), terrain_max_health(...);
int   terrain_starting_health(...), terrain_damage(...);
bool  terrain_is_walkable(const DK2TerrainDef*), terrain_is_minable(const DK2TerrainDef*);
bool  terrain_is_walkable_id(int), terrain_is_minable_id(int);
int   stats_terrain_to_engine_id(int), engine_terrain_to_stats_id(int);
const DK2TerrainDef* terrain_for_engine_id(int);

struct BuildCost { int gold, mana, manufacture; };
BuildCost trap_build_cost(const DK2TrapDef*), door_build_cost(const DK2DoorDef*);
bool trap_placement_valid(const DK2TrapDef*, bool tile_walkable, const BuildCost& available);
bool door_placement_valid(const DK2DoorDef*, bool tile_walkable, const BuildCost& available);

bool validate_rooms(char*, size_t), validate_traps(char*, size_t), validate_doors(char*, size_t);
bool validate_terrains(char*, size_t), validate_objects(char*, size_t), validate_all(char*, size_t);
int structures_self_test();  // 0 == pass
```

All scalar getters return `0`/`false` for a null definition, so call sites need no null
branch beyond the lookup itself.

## 6. Suggested integration points in `engine/DK2.cpp`

Line numbers refer to the current `DK2.cpp` as of this document.

1. **Includes (top, after line 1-6):** add
   `#include "Structures.h"` (which already includes the generated header). Add
   `engine/Structures.cpp` to whatever compiles `DK2.cpp` (e.g. `verify.sh:10`). After
   loading, call `dk2::structures::validate_all(error, sizeof(error))` once during
   start-up to catch table drift.

2. **`terrain_walkable()` (44-70):** replace the hard-coded `switch` with
   `return dk2::structures::terrain_is_walkable_id(
        dk2::structures::engine_terrain_to_stats_id(static_cast<int>(id)));`
   This is the single point where the 25-id switch becomes data-driven.

3. **`terrain_name()` (39-42) and `kTerrainNames` (20-27):** derive from the 41-row
   table via `terrain_for_engine_id()` / `terrain_by_id()->name`; keep the fallback for
   ids outside the table. This also exposes names for stats ids 26..41.

4. **`Economy::recalculate()` (106-118):** the `cap += 4000.0f` per Treasury tile is a
   manual constant. The recovered data has no explicit "gold capacity per room"; the
   closest values are `terrain_gold_value()` (Gold/Gems tile value, 3000) and room
   `cost`. Decide the capacity policy in the engine; the API can supply the raw
   `gold_value` and room `cost` inputs instead of the magic number.

5. **`Game::build_room()` (210-245):** lines 230-235 currently look up
   `kDK2RoomCosts` (17 manual prices) and line 238 withdraws that cost. Replace the
   lookup with `room_by_name(state_.known_rooms[type].c_str())` and
   `room_gold_cost(def)`; use `room_terrain_id(def)` converted with
   `stats_terrain_to_engine_id()` for the tile terrain instead of the name-to-
   `kTerrainNames` loop at 224-229. This removes both manual tables.

6. **`Game::research_spell()` (315-331):** line 326 compares against
   `kDK2Spells[].research_time` (spell catalog), which is a different table. Room
   `research_time` from `DK2RoomDef` (medium confidence, mostly 40899) can be used for
   room unlock pacing — do **not** substitute it for spell research time without
   first resolving the 40899 sentinel described in §2.

7. **`Game::update_magic()` (333-344):** line 338 hard-codes `20.0f` mana per heart
   per second. `terrain_mana_gain()` gives the per-tile mana gain for Claimed Path,
   Reinforced Wall and Claimed Vault (100); it is a per-tile rate from the KWD and a
   better-informed input than the constant, but the time scaling remains an engine
   policy decision.

8. **Mining (561-569):** line 561 checks `TerrainId::Gold` only. Use
   `terrain_is_minable_id()` on the converted tile id so Gems is handled too, and
   `terrain_gold_value()` for the per-tile value when crediting `gold_carried`.

9. **Trap / door placement (new code, near `build_room()`):** there are no trap or door
   objects in `GameState` yet. When added, resolve the definition with
   `trap_by_name()` / `door_by_name()`, check
   `trap_placement_valid(def, world.walkable(x, y), available_cost)`, then charge
   `trap_build_cost()` / `door_build_cost()`. Also use `trap_health()` /
   `door_health()` for durability, `trap_mana_cost_to_fire()` for firing and
   `door_is_secret()` for hidden-door behaviour. Nothing in the current engine
   consumes traps, doors or objects, so this is net-new wiring.

10. **Objects (new code):** `object_by_name()` / `object_by_id()` plus `object_hp()`,
    `object_mana_value()`, `object_width/height/speed()` can drive pickups and dropped
    items. This is also net-new; no current call site exists.

## 7. Self-test

```bash
cd port
clang++ -std=c++17 -O2 -Wall -Wextra -Iengine -DDK2_STRUCTURES_SELFTEST \
  -o build/structures_selftest engine/Structures.cpp
./build/structures_selftest
```

The guard defines `main()`; without it, `Structures.cpp` compiles as a normal
translation unit. `structures_self_test()` verifies all five counts (26/16/8/41/145)
and known values: Treasury cost 200, Gas Trap cost 600 / HP 3000, Wooden Door cost 500 /
HP 2000, terrain Gold `gold_value` 3000, Claimed Vault `mana_gain` 100, object Gold
HP 128, the 1-based ↔ 0-based terrain conversion, case/punctuation-insensitive
lookup, placement rules and `validate_all()`.
