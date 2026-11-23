#include "Structures.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dk2 {
namespace structures {

namespace {

const unsigned kExpectedRoomCount = 26;
const unsigned kExpectedTrapCount = 16;
const unsigned kExpectedDoorCount = 8;
const unsigned kExpectedTerrainCount = 41;
const unsigned kExpectedObjectCount = 145;

// Derived walkability table, keyed by 1-based KWD terrain id.  The recovered
// Terrain.kwd flags bitfield was not emitted into stats.json; these ids mirror
// the traversable TerrainId values in DK2.cpp (0-based) shifted by +1, with
// Stone Bridge (26) added as the obvious walkable bridge.  See USAGE doc.
const int kWalkableStatsTerrainIds[] = {
    3,   // Dirt Path
    6,   // Gold
    7,   // Gems
    8,   // Claimed Path
    10, 11, 12, 13, 14, 15, 16,  // Treasury .. Training Room
    17,  // Wooden Bridge
    18, 19, 20, 21, 22, 23, 24, 25,  // Guard Room .. Behemoth Pit
    26,  // Stone Bridge (inferred)
};

// Door flag bits inferred from the decoded values:
//   Secret Door 32, Barricade 64, Hero Barricade 192 (64|128), Portcullis 128.
const int kDoorFlagSecret = 32;
const int kDoorFlagBarricade = 64;
const int kDoorFlagHero = 128;

std::string normalize_name(const char* name) {
    std::string out;
    if (!name) return out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out.push_back(static_cast<char>(c));
    }
    return out;
}

void write_error(char* error, size_t error_size, const char* message) {
    if (!error || error_size == 0) return;
    size_t n = std::strlen(message);
    if (n >= error_size) n = error_size - 1;
    std::memcpy(error, message, n);
    error[n] = '\0';
}

void write_errorf(char* error, size_t error_size, const char* prefix, unsigned value) {
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%s %u", prefix, value);
    write_error(error, error_size, buffer);
}

template <typename T>
bool validate_table(const T* table, unsigned count, unsigned expected,
                    int (*id_of)(const T*), const char* label,
                    char* error, size_t error_size) {
    if (!table) {
        write_error(error, error_size, "null table");
        return false;
    }
    if (count != expected) {
        write_errorf(error, error_size, "wrong record count for", count);
        return false;
    }
    std::vector<unsigned char> seen_id(count + 1, 0);
    std::vector<std::string> seen_name;
    seen_name.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const int id = id_of(&table[i]);
        if (id <= 0 || static_cast<unsigned>(id) > count) {
            write_errorf(error, error_size, "out-of-range id in", static_cast<unsigned>(id));
            return false;
        }
        const unsigned uid = static_cast<unsigned>(id);
        if (seen_id[uid] || uid != i + 1) {
            write_errorf(error, error_size, "duplicate/non-contiguous id in", uid);
            return false;
        }
        seen_id[uid] = 1;

        const std::string key = normalize_name(table[i].name);
        if (key.empty()) {
            write_errorf(error, error_size, "empty name at index", i);
            return false;
        }
        for (size_t n = 0; n < seen_name.size(); ++n) {
            if (seen_name[n] == key) {
                write_error(error, error_size, "duplicate normalized name in table");
                return false;
            }
        }
        seen_name.push_back(key);
    }
    (void)label;
    return true;
}

int room_id_of(const DK2RoomDef* d) { return d->room_id; }
int trap_id_of(const DK2TrapDef* d) { return d->trap_id; }
int door_id_of(const DK2DoorDef* d) { return d->door_id; }
int terrain_id_of(const DK2TerrainDef* d) { return d->terrain_id; }
int object_id_of(const DK2ObjectDef* d) { return d->object_id; }

bool contains_int(const int* values, size_t count, int value) {
    for (size_t i = 0; i < count; ++i)
        if (values[i] == value) return true;
    return false;
}

// The generated structs all expose a distinct <kind>_id field, so id lookup is
// implemented per type below rather than through one generic template.

}  // namespace

// ---------------------------------------------------------------------------
// Counts and indexed access
// ---------------------------------------------------------------------------
size_t room_count() { return kDK2RoomCount; }
size_t trap_count() { return kDK2TrapCount; }
size_t door_count() { return kDK2DoorCount; }
size_t terrain_count() { return kDK2TerrainCount; }
size_t object_count() { return kDK2ObjectCount; }

const DK2RoomDef* room_by_index(size_t i) {
    return i < kDK2RoomCount ? &kDK2Rooms[i] : nullptr;
}
const DK2TrapDef* trap_by_index(size_t i) {
    return i < kDK2TrapCount ? &kDK2Traps[i] : nullptr;
}
const DK2DoorDef* door_by_index(size_t i) {
    return i < kDK2DoorCount ? &kDK2Doors[i] : nullptr;
}
const DK2TerrainDef* terrain_by_index(size_t i) {
    return i < kDK2TerrainCount ? &kDK2Terrains[i] : nullptr;
}
const DK2ObjectDef* object_by_index(size_t i) {
    return i < kDK2ObjectCount ? &kDK2Objects[i] : nullptr;
}

// ---------------------------------------------------------------------------
// Lookup by id (1-based KWD id)
// ---------------------------------------------------------------------------
const DK2RoomDef* room_by_id(int id) {
    if (id <= 0 || id > static_cast<int>(kDK2RoomCount)) return nullptr;
    return &kDK2Rooms[id - 1];
}
const DK2TrapDef* trap_by_id(int id) {
    if (id <= 0 || id > static_cast<int>(kDK2TrapCount)) return nullptr;
    return &kDK2Traps[id - 1];
}
const DK2DoorDef* door_by_id(int id) {
    if (id <= 0 || id > static_cast<int>(kDK2DoorCount)) return nullptr;
    return &kDK2Doors[id - 1];
}
const DK2TerrainDef* terrain_by_id(int id) {
    if (id <= 0 || id > static_cast<int>(kDK2TerrainCount)) return nullptr;
    return &kDK2Terrains[id - 1];
}
const DK2ObjectDef* object_by_id(int id) {
    if (id <= 0 || id > static_cast<int>(kDK2ObjectCount)) return nullptr;
    return &kDK2Objects[id - 1];
}

// ---------------------------------------------------------------------------
// Lookup by normalized name
// ---------------------------------------------------------------------------
template <typename T>
static const T* by_name(const T* table, unsigned count, const char* name) {
    if (!table || !name) return nullptr;
    const std::string wanted = normalize_name(name);
    if (wanted.empty()) return nullptr;
    for (unsigned i = 0; i < count; ++i)
        if (normalize_name(table[i].name) == wanted) return &table[i];
    return nullptr;
}

const DK2RoomDef* room_by_name(const char* name) {
    return by_name(kDK2Rooms, kDK2RoomCount, name);
}
const DK2TrapDef* trap_by_name(const char* name) {
    return by_name(kDK2Traps, kDK2TrapCount, name);
}
const DK2DoorDef* door_by_name(const char* name) {
    return by_name(kDK2Doors, kDK2DoorCount, name);
}
const DK2TerrainDef* terrain_by_name(const char* name) {
    return by_name(kDK2Terrains, kDK2TerrainCount, name);
}
const DK2ObjectDef* object_by_name(const char* name) {
    return by_name(kDK2Objects, kDK2ObjectCount, name);
}

// ---------------------------------------------------------------------------
// Scalar accessors
// ---------------------------------------------------------------------------
int room_gold_cost(const DK2RoomDef* d) { return d ? d->cost : 0; }
int room_research_time(const DK2RoomDef* d) { return d ? d->research_time : 0; }
int room_health_gain(const DK2RoomDef* d) { return d ? d->health_gain : 0; }
int room_terrain_id(const DK2RoomDef* d) { return d ? d->terrain_id : 0; }
int room_created_creature_id(const DK2RoomDef* d) {
    return d ? d->created_creature_id : 0;
}
int room_recommended_width(const DK2RoomDef* d) {
    return d ? d->recommended_size_x : 0;
}
int room_recommended_height(const DK2RoomDef* d) {
    return d ? d->recommended_size_y : 0;
}

int trap_gold_cost(const DK2TrapDef* d) { return d ? d->gold_cost : 0; }
int trap_health(const DK2TrapDef* d) { return d ? d->health : 0; }
int trap_research_time(const DK2TrapDef* d) { return d ? d->research_time : 0; }
int trap_manufacture(const DK2TrapDef* d) { return d ? d->manuf_to_build : 0; }
int trap_mana_usage(const DK2TrapDef* d) { return d ? d->mana_usage : 0; }
int trap_mana_cost_to_fire(const DK2TrapDef* d) {
    return d ? d->mana_cost_to_fire : 0;
}
int trap_shots_when_armed(const DK2TrapDef* d) {
    return d ? d->shots_when_armed : 0;
}
int trap_trigger(const DK2TrapDef* d) { return d ? d->trigger : 0; }
float trap_charge_time(const DK2TrapDef* d) { return d ? d->charge_time : 0.0f; }
float trap_recharge_time(const DK2TrapDef* d) { return d ? d->recharge_time : 0.0f; }
float trap_shot_delay(const DK2TrapDef* d) { return d ? d->shot_delay : 0.0f; }

int door_gold_cost(const DK2DoorDef* d) { return d ? d->gold_cost : 0; }
int door_mana_cost(const DK2DoorDef* d) { return d ? d->mana_cost : 0; }
int door_health(const DK2DoorDef* d) { return d ? d->health : 0; }
int door_research_time(const DK2DoorDef* d) { return d ? d->research_time : 0; }
int door_manufacture(const DK2DoorDef* d) { return d ? d->manuf_to_build : 0; }
int door_flags(const DK2DoorDef* d) { return d ? d->flags : 0; }
bool door_is_secret(const DK2DoorDef* d) {
    return d && (d->flags & kDoorFlagSecret) != 0;
}
bool door_is_barricade(const DK2DoorDef* d) {
    return d && (d->flags & kDoorFlagBarricade) != 0;
}
bool door_is_hero(const DK2DoorDef* d) {
    return d && (d->flags & kDoorFlagHero) != 0;
}

int object_hp(const DK2ObjectDef* d) { return d ? d->hp : 0; }
int object_mana_value(const DK2ObjectDef* d) { return d ? d->mana_value : 0; }
int object_state(const DK2ObjectDef* d) { return d ? d->state : 0; }
float object_width(const DK2ObjectDef* d) { return d ? d->width : 0.0f; }
float object_height(const DK2ObjectDef* d) { return d ? d->height : 0.0f; }
float object_speed(const DK2ObjectDef* d) { return d ? d->speed : 0.0f; }

// ---------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------
int terrain_gold_value(const DK2TerrainDef* d) { return d ? d->gold_value : 0; }
int terrain_mana_gain(const DK2TerrainDef* d) { return d ? d->mana_gain : 0; }
int terrain_max_health(const DK2TerrainDef* d) { return d ? d->max_health : 0; }
int terrain_starting_health(const DK2TerrainDef* d) {
    return d ? d->starting_health : 0;
}
int terrain_damage(const DK2TerrainDef* d) { return d ? d->damage : 0; }

bool terrain_is_walkable_id(int id) {
    return contains_int(kWalkableStatsTerrainIds,
                        sizeof(kWalkableStatsTerrainIds) / sizeof(int), id);
}
bool terrain_is_minable_id(int id) {
    const DK2TerrainDef* def = terrain_by_id(id);
    return def && def->gold_value > 0;
}
bool terrain_is_walkable(const DK2TerrainDef* d) {
    return d && terrain_is_walkable_id(d->terrain_id);
}
bool terrain_is_minable(const DK2TerrainDef* d) {
    return d && d->gold_value > 0;
}

int stats_terrain_to_engine_id(int stats_id) {
    if (stats_id < 1 || stats_id > static_cast<int>(kDK2TerrainCount)) return -1;
    return stats_id - 1;
}
int engine_terrain_to_stats_id(int engine_id) {
    if (engine_id < 0 || engine_id + 1 > static_cast<int>(kDK2TerrainCount)) return -1;
    return engine_id + 1;
}
const DK2TerrainDef* terrain_for_engine_id(int engine_id) {
    return terrain_by_id(engine_terrain_to_stats_id(engine_id));
}

// ---------------------------------------------------------------------------
// Placement rules
// ---------------------------------------------------------------------------
BuildCost trap_build_cost(const DK2TrapDef* d) {
    BuildCost cost = {0, 0, 0};
    if (d) {
        cost.gold = d->gold_cost;
        cost.manufacture = d->manuf_to_build;
    }
    return cost;
}
BuildCost door_build_cost(const DK2DoorDef* d) {
    BuildCost cost = {0, 0, 0};
    if (d) {
        cost.gold = d->gold_cost;
        cost.mana = d->mana_cost;
        cost.manufacture = d->manuf_to_build;
    }
    return cost;
}

bool trap_placement_valid(const DK2TrapDef* d, bool tile_walkable,
                          const BuildCost& available) {
    if (!d || !tile_walkable) return false;
    const BuildCost cost = trap_build_cost(d);
    return available.gold >= cost.gold &&
           available.mana >= cost.mana &&
           available.manufacture >= cost.manufacture;
}
bool door_placement_valid(const DK2DoorDef* d, bool tile_walkable,
                          const BuildCost& available) {
    if (!d || !tile_walkable) return false;
    const BuildCost cost = door_build_cost(d);
    return available.gold >= cost.gold &&
           available.mana >= cost.mana &&
           available.manufacture >= cost.manufacture;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
bool validate_rooms(char* error, size_t error_size) {
    return validate_table(kDK2Rooms, kDK2RoomCount, kExpectedRoomCount,
                          room_id_of, "rooms", error, error_size);
}
bool validate_traps(char* error, size_t error_size) {
    return validate_table(kDK2Traps, kDK2TrapCount, kExpectedTrapCount,
                          trap_id_of, "traps", error, error_size);
}
bool validate_doors(char* error, size_t error_size) {
    return validate_table(kDK2Doors, kDK2DoorCount, kExpectedDoorCount,
                          door_id_of, "doors", error, error_size);
}
bool validate_terrains(char* error, size_t error_size) {
    return validate_table(kDK2Terrains, kDK2TerrainCount, kExpectedTerrainCount,
                          terrain_id_of, "terrains", error, error_size);
}
bool validate_objects(char* error, size_t error_size) {
    return validate_table(kDK2Objects, kDK2ObjectCount, kExpectedObjectCount,
                          object_id_of, "objects", error, error_size);
}

bool validate_all(char* error, size_t error_size) {
    if (!validate_rooms(error, error_size) ||
        !validate_traps(error, error_size) ||
        !validate_doors(error, error_size) ||
        !validate_terrains(error, error_size) ||
        !validate_objects(error, error_size))
        return false;

    const size_t count = sizeof(kWalkableStatsTerrainIds) / sizeof(int);
    for (size_t i = 0; i < count; ++i) {
        if (!terrain_by_id(kWalkableStatsTerrainIds[i])) {
            write_error(error, error_size, "walkable id not present in terrain table");
            return false;
        }
    }
    for (int id = 1; id <= static_cast<int>(kDK2TerrainCount); ++id) {
        if (engine_terrain_to_stats_id(stats_terrain_to_engine_id(id)) != id) {
            write_error(error, error_size, "terrain id conversion did not round-trip");
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int structures_self_test() {
    int failures = 0;
#define DK2_STRUCTURES_CHECK(expr)                                            \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::printf("FAIL: %s (line %d)\n", #expr, __LINE__);             \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

    DK2_STRUCTURES_CHECK(room_count() == 26);
    DK2_STRUCTURES_CHECK(trap_count() == 16);
    DK2_STRUCTURES_CHECK(door_count() == 8);
    DK2_STRUCTURES_CHECK(terrain_count() == 41);
    DK2_STRUCTURES_CHECK(object_count() == 145);

    const DK2RoomDef* treasury = room_by_name("treasury");
    DK2_STRUCTURES_CHECK(treasury && treasury->room_id == 1);
    DK2_STRUCTURES_CHECK(room_gold_cost(treasury) == 200);
    DK2_STRUCTURES_CHECK(room_by_id(1) == treasury);
    DK2_STRUCTURES_CHECK(room_by_name("work-shop") &&
                         room_by_name("work-shop")->room_id == 10);

    const DK2TrapDef* gas = trap_by_id(1);
    DK2_STRUCTURES_CHECK(gas && std::strcmp(gas->name, "Gas Trap") == 0);
    DK2_STRUCTURES_CHECK(trap_gold_cost(gas) == 600);
    DK2_STRUCTURES_CHECK(trap_health(gas) == 3000);
    DK2_STRUCTURES_CHECK(trap_by_name("boulder trap") &&
                         trap_by_name("boulder trap")->trap_id == 2);

    const DK2DoorDef* wood = door_by_name("wooden door");
    DK2_STRUCTURES_CHECK(wood && wood->door_id == 1);
    DK2_STRUCTURES_CHECK(door_gold_cost(wood) == 500);
    DK2_STRUCTURES_CHECK(door_health(wood) == 2000);
    DK2_STRUCTURES_CHECK(door_is_secret(door_by_name("secret door")));

    const DK2TerrainDef* gold_terrain = terrain_by_id(6);
    DK2_STRUCTURES_CHECK(gold_terrain &&
                         std::strcmp(gold_terrain->name, "Gold") == 0);
    DK2_STRUCTURES_CHECK(terrain_gold_value(gold_terrain) == 3000);
    DK2_STRUCTURES_CHECK(terrain_is_minable(gold_terrain));
    DK2_STRUCTURES_CHECK(!terrain_is_walkable(terrain_by_id(1)));
    DK2_STRUCTURES_CHECK(terrain_mana_gain(terrain_by_id(32)) == 100);
    DK2_STRUCTURES_CHECK(stats_terrain_to_engine_id(6) == 5);
    DK2_STRUCTURES_CHECK(engine_terrain_to_stats_id(5) == 6);
    DK2_STRUCTURES_CHECK(terrain_for_engine_id(5) == gold_terrain);

    const DK2ObjectDef* gold_object = object_by_id(1);
    DK2_STRUCTURES_CHECK(gold_object && object_hp(gold_object) == 128);
    DK2_STRUCTURES_CHECK(object_by_name("spell book") &&
                         object_by_name("spell book")->object_id == 4);

    BuildCost available = {1000, 100, 10000};
    DK2_STRUCTURES_CHECK(trap_placement_valid(gas, true, available));
    DK2_STRUCTURES_CHECK(!trap_placement_valid(gas, false, available));
    DK2_STRUCTURES_CHECK(!trap_placement_valid(gas, true, BuildCost{1000, 100, 100}));
    DK2_STRUCTURES_CHECK(door_placement_valid(wood, true, available));
    DK2_STRUCTURES_CHECK(validate_all(nullptr, 0));

#undef DK2_STRUCTURES_CHECK
    return failures;
}

}  // namespace structures
}  // namespace dk2

#ifdef DK2_STRUCTURES_SELFTEST
int main() {
    const int failures = dk2::structures::structures_self_test();
    if (failures == 0) {
        std::printf("structures self-test: PASS\n");
        return 0;
    }
    std::printf("structures self-test: %d failure(s)\n", failures);
    return 1;
}
#endif
