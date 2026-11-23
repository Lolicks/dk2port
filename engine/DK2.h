#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dk2 {

constexpr uint32_t kMapMagic = 0x4d324b44;  // "DK2M" read as little-endian u32
constexpr float kTickSeconds = 1.0f / 30.0f;
constexpr int kMaxCreatures = 512;
constexpr int kMaxPathNodes = 8192;

enum class TerrainId : uint8_t {
    ImpenetrableRock = 0, Rock = 1, DirtPath = 2, Water = 3, Lava = 4,
    Gold = 5, Gems = 6, ClaimedPath = 7, ReinforcedWall = 8, Treasury = 9,
    Lair = 10, Portal = 11, Hatchery = 12, DungeonHeart = 13, Library = 14,
    TrainingRoom = 15, WoodenBridge = 16, GuardRoom = 17, Workshop = 18,
    Prison = 19, TortureChamber = 20, Graveyard = 21, Temple = 22,
    ScavengerRoom = 23, BehemothPit = 24, Unknown = 255,
};

bool terrain_walkable(TerrainId id);
bool terrain_is_room(TerrainId id);
const char* terrain_name(unsigned id);

// One 32-bit KLD tile: [0]=terrain id, [1]=surface state,
// [2]=owner/claim, [3]=flags.  This layout was derived from the shipped maps.
struct Tile {
    uint8_t terrain;
    uint8_t surface;
    uint8_t owner;
    uint8_t flags;

    static Tile from_word(uint32_t word) {
        return Tile{static_cast<uint8_t>(word),
                    static_cast<uint8_t>(word >> 8),
                    static_cast<uint8_t>(word >> 16),
                    static_cast<uint8_t>(word >> 24)};
    }
    uint32_t to_word() const {
        return uint32_t(terrain) | (uint32_t(surface) << 8) |
               (uint32_t(owner) << 16) | (uint32_t(flags) << 24);
    }
};

struct World {
    int width = 0;
    int height = 0;
    std::string name;
    std::vector<Tile> tiles;

    bool load_binary(const uint8_t* data, size_t size, std::string* error = nullptr);
    const Tile* at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return nullptr;
        return &tiles[static_cast<size_t>(y) * width + x];
    }
    Tile* at(int x, int y) {
        return const_cast<Tile*>(static_cast<const World*>(this)->at(x, y));
    }
    int index(int x, int y) const { return y * width + x; }
    bool walkable(int x, int y) const {
        const Tile* t = at(x, y);
        return t && terrain_walkable(static_cast<TerrainId>(t->terrain));
    }
};

enum class CreatureState { Idle, Walking, Mining, Fighting, Eating, Sleeping, Dead };
enum class CreatureJob { Idle, Sleep, Eat, Mine, HaulGold, Train, Research, Guard, Fight, Flee };

struct Creature {
    int id = 0;
    int species = 0;
    int team = 0;  // 0 = keeper's dungeon, 1 = invading hero
    CreatureState state = CreatureState::Idle;
    float x = 0, y = 0;
    float hp = 100, max_hp = 100;
    float damage = 12;
    float speed = 2.0f;
    float sight = 4.0f;
    float melee_range = 1.0f;
    float fear = 0.0f;
    CreatureJob job = CreatureJob::Idle;
    float mood = 100.0f;
    float hunger = 0, fatigue = 0;
    float gold_carried = 0;
    float max_gold = 100.0f;
    int target_x = -1, target_y = -1;
    int path_index = 0;
    std::vector<int> path;
};

struct Room {
    int type = 0;
    int x = 0, y = 0;
    int owner = 0;
    float integrity = 100;
};

struct Economy {
    float gold = 0;
    float gold_capacity = 0;
    int treasures = 0;
    void recalculate(const World& world);
    float deposit(float amount);
    bool withdraw(float amount);
};

struct GameConfig {
    float creature_speed = 2.0f;
    float mining_rate = 4.0f;
    float hunger_rate = 0.35f;
    float fatigue_rate = 0.25f;
    float spell_cooldown = 3.0f;
    float attack_per_second = 12.0f;
    float combat_range = 1.1f;
    bool values_are_reconstructed = true;
};

struct CameraState {
    float center_x = 0, center_y = 0;
    float zoom = 1.0f;
    float yaw = 0.0f;
    float pitch = 0.6f;
    void clamp(const World& world);
};

struct GameState {
    World world;
    std::vector<Creature> creatures;
    std::vector<Room> rooms;
    std::vector<std::string> known_creatures;
    std::vector<std::string> known_rooms;
    std::vector<std::string> known_spells;
    Economy economy;
    float mana = 1000.0f;
    float mana_max = 10000.0f;
    std::vector<float> research;      // per-spell progress
    std::vector<float> cooldowns;     // per-spell remaining seconds
    std::vector<uint8_t> spells_known;
    CameraState camera;
    GameConfig config;
    float elapsed = 0;
    uint64_t tick_count = 0;
    std::string mission_name;
    bool victory = false;
    bool defeat = false;
};

class Game {
public:
    Game();
    bool load_map(const uint8_t* data, size_t size, std::string* error = nullptr);
    void load_catalog(const std::vector<std::string>& creatures,
                      const std::vector<std::string>& rooms,
                      const std::vector<std::string>& spells);
    void set_mission(const std::string& name);
    int spawn_creature(int species, int x, int y, int team = 0);
    bool build_room(int type, int x, int y, int owner);
    bool cast_spell(int spell_index, int x, int y);
    bool research_spell(int spell_index);
    void update_magic();
    bool find_path(int sx, int sy, int tx, int ty, std::vector<int>& out) const;
    void step();
    void evaluate_triggers();

    GameState& state() { return state_; }
    const GameState& state() const { return state_; }

    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // Platform layer receives these events; Metal/UIKit adapter implements them.
    void on_pan(float dx, float dy);
    void on_zoom(float factor);
    void on_rotate(float delta);
    void select_at(float wx, float wy);

    int selected_creature = -1;

private:
    GameState state_;
    int next_creature_id_ = 1;
    float spell_cooldown_left_ = 0;
    std::unordered_set<unsigned> fired_actions_;
    void update_creature(Creature& c);
};

// Fixed-timestep driver shared by iOS, macOS and the headless test harness.
class LoopDriver {
public:
    explicit LoopDriver(Game& game) : game_(game) {}
    void advance(float delta_seconds) {
        accumulator_ += delta_seconds > 0.25f ? 0.25f : delta_seconds;
        while (accumulator_ >= kTickSeconds) {
            game_.step();
            accumulator_ -= kTickSeconds;
        }
    }
    float alpha() const { return accumulator_ / kTickSeconds; }
private:
    Game& game_;
    float accumulator_ = 0;
};

}  // namespace dk2
