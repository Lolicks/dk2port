#pragma once

// Standalone read-only API over the recovered DK2 catalogue data.
// No dependency on DK2.h -- include this (and the generated header) anywhere.
// All ids are the 1-based values stored in the shipped KWD records.
#include "Structures.generated.hpp"

#include <cstddef>

namespace dk2 {
namespace structures {

// ---------------------------------------------------------------------------
// Table access.  Every lookup returns a pointer into a static table (never an
// owned allocation) or nullptr when no record matches.  Names are matched
// case-insensitively with punctuation/whitespace removed, so "Work Shop",
// "workshop" and "Work-Shop" all resolve to the same record.
// ---------------------------------------------------------------------------
size_t room_count();
size_t trap_count();
size_t door_count();
size_t terrain_count();
size_t object_count();

const DK2RoomDef*    room_by_index(size_t index);
const DK2TrapDef*    trap_by_index(size_t index);
const DK2DoorDef*    door_by_index(size_t index);
const DK2TerrainDef* terrain_by_index(size_t index);
const DK2ObjectDef*  object_by_index(size_t index);

const DK2RoomDef*    room_by_id(int id);      // room_id, 1-based
const DK2TrapDef*    trap_by_id(int id);      // trap_id, 1-based
const DK2DoorDef*    door_by_id(int id);      // door_id, 1-based
const DK2TerrainDef* terrain_by_id(int id);   // terrain_id, 1-based
const DK2ObjectDef*  object_by_id(int id);    // object_id, 1-based

const DK2RoomDef*    room_by_name(const char* name);
const DK2TrapDef*    trap_by_name(const char* name);
const DK2DoorDef*    door_by_name(const char* name);
const DK2TerrainDef* terrain_by_name(const char* name);
const DK2ObjectDef*  object_by_name(const char* name);

// ---------------------------------------------------------------------------
// Rooms / traps / doors / objects: scalar value accessors.
// All return 0 / false for a null definition so call sites stay branch-light.
// ---------------------------------------------------------------------------
int room_gold_cost(const DK2RoomDef* def);
int room_research_time(const DK2RoomDef* def);
int room_health_gain(const DK2RoomDef* def);
int room_terrain_id(const DK2RoomDef* def);      // 1-based KWD terrain id
int room_created_creature_id(const DK2RoomDef* def);
int room_recommended_width(const DK2RoomDef* def);
int room_recommended_height(const DK2RoomDef* def);

int trap_gold_cost(const DK2TrapDef* def);
int trap_health(const DK2TrapDef* def);
int trap_research_time(const DK2TrapDef* def);
int trap_manufacture(const DK2TrapDef* def);
int trap_mana_usage(const DK2TrapDef* def);
int trap_mana_cost_to_fire(const DK2TrapDef* def);
int trap_shots_when_armed(const DK2TrapDef* def);
int trap_trigger(const DK2TrapDef* def);
float trap_charge_time(const DK2TrapDef* def);
float trap_recharge_time(const DK2TrapDef* def);
float trap_shot_delay(const DK2TrapDef* def);

int door_gold_cost(const DK2DoorDef* def);
int door_mana_cost(const DK2DoorDef* def);
int door_health(const DK2DoorDef* def);
int door_research_time(const DK2DoorDef* def);
int door_manufacture(const DK2DoorDef* def);
int door_flags(const DK2DoorDef* def);
bool door_is_secret(const DK2DoorDef* def);     // flags & 32
bool door_is_barricade(const DK2DoorDef* def);  // flags & 64
bool door_is_hero(const DK2DoorDef* def);       // flags & 128

int object_hp(const DK2ObjectDef* def);
int object_mana_value(const DK2ObjectDef* def);
int object_state(const DK2ObjectDef* def);
float object_width(const DK2ObjectDef* def);
float object_height(const DK2ObjectDef* def);
float object_speed(const DK2ObjectDef* def);

// ---------------------------------------------------------------------------
// Terrain.
// ---------------------------------------------------------------------------
int terrain_gold_value(const DK2TerrainDef* def);  // direct from KWD field
int terrain_mana_gain(const DK2TerrainDef* def);   // direct from KWD field
int terrain_max_health(const DK2TerrainDef* def);
int terrain_starting_health(const DK2TerrainDef* def);
int terrain_damage(const DK2TerrainDef* def);

// The KWD terrain flags bitfield was not emitted into stats.json, so
// walkability uses the documented curated set below (see STRUCTURES_USAGE.md).
// Minability is derived directly from gold_value > 0 (Gold and Gems).
bool terrain_is_walkable(const DK2TerrainDef* def);
bool terrain_is_minable(const DK2TerrainDef* def);
bool terrain_is_walkable_id(int stats_terrain_id);
bool terrain_is_minable_id(int stats_terrain_id);

// DK2.cpp's TerrainId enum is 0-based; KWD terrain ids are 1-based.
// Conversion is valid for stats ids in [1, kDK2TerrainCount] and returns -1
// otherwise. terrain_for_engine_id() is the inverse lookup.
int stats_terrain_to_engine_id(int stats_terrain_id);
int engine_terrain_to_stats_id(int engine_terrain_id);
const DK2TerrainDef* terrain_for_engine_id(int engine_terrain_id);

// ---------------------------------------------------------------------------
// Placement rules.  BuildCost is the resource requirement to build one item;
// "available" is the caller's current balance.  tile_walkable must reflect the
// target tile (traps/doors go on walkable, claimed floor in the original).
// ---------------------------------------------------------------------------
struct BuildCost {
    int gold;
    int mana;
    int manufacture;
};

BuildCost trap_build_cost(const DK2TrapDef* def);
BuildCost door_build_cost(const DK2DoorDef* def);

bool trap_placement_valid(const DK2TrapDef* def, bool tile_walkable,
                          const BuildCost& available);
bool door_placement_valid(const DK2DoorDef* def, bool tile_walkable,
                          const BuildCost& available);

// ---------------------------------------------------------------------------
// Validation.  Each function returns true when its table is internally
// consistent (counts, unique/contiguous 1-based ids, non-empty unique names).
// A diagnostic is copied into error[0..error_size-1] on failure when supplied.
// validate_all() also checks the derived walkable set and id conversions.
// ---------------------------------------------------------------------------
bool validate_rooms(char* error, size_t error_size);
bool validate_traps(char* error, size_t error_size);
bool validate_doors(char* error, size_t error_size);
bool validate_terrains(char* error, size_t error_size);
bool validate_objects(char* error, size_t error_size);
bool validate_all(char* error, size_t error_size);

// Returns the number of failed checks (0 == all passed).  Defines main() only
// when compiled with -DK2_STRUCTURES_SELFTEST; otherwise it is a normal
// function that engine tests may call directly.
int structures_self_test();

}  // namespace structures
}  // namespace dk2
