#include "DK2.h"
#include "Balance.generated.hpp"
#include "CreatureStats.generated.hpp"
#include "MissionRules.generated.hpp"
#include "MissionTriggers.generated.hpp"
#include "SpellData.generated.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>

namespace dk2 {

namespace {
const char* kTerrainFallback = "Unknown";
const char* kTerrainNames[] = {
    "Impenetrable Rock", "Rock", "Dirt Path", "Water", "Lava", "Gold",
    "Gems", "Claimed Path", "Reinforced Wall", "Treasury", "Lair", "Portal",
    "Hatchery", "Dungeon Heart", "Library", "Training Room", "Wooden Bridge",
    "Guard Room", "Work Shop", "Prison", "Torture Chamber", "Graveyard",
    "Temple", "Scavenger Room", "Behemoth Pit",
};

struct SaveHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t world_w, world_h;
    uint32_t creature_count, room_count;
};
constexpr uint32_t kSaveMagic = 0x324b3244;  // "DK2S"
constexpr uint32_t kSaveVersion = 2;
}  // namespace

const char* terrain_name(unsigned id) {
    return id < sizeof(kTerrainNames) / sizeof(kTerrainNames[0])
               ? kTerrainNames[id] : kTerrainFallback;
}

bool terrain_walkable(TerrainId id) {
    switch (id) {
        case TerrainId::DirtPath:
        case TerrainId::Gold:
        case TerrainId::Gems:
        case TerrainId::ClaimedPath:
        case TerrainId::Treasury:
        case TerrainId::Lair:
        case TerrainId::Portal:
        case TerrainId::Hatchery:
        case TerrainId::DungeonHeart:
        case TerrainId::Library:
        case TerrainId::TrainingRoom:
        case TerrainId::WoodenBridge:
        case TerrainId::GuardRoom:
        case TerrainId::Workshop:
        case TerrainId::Prison:
        case TerrainId::TortureChamber:
        case TerrainId::Graveyard:
        case TerrainId::Temple:
        case TerrainId::ScavengerRoom:
        case TerrainId::BehemothPit:
            return true;
        default:
            return false;
    }
}

bool terrain_is_room(TerrainId id) {
    unsigned v = static_cast<unsigned>(id);
    return v >= static_cast<unsigned>(TerrainId::Treasury) &&
           v <= static_cast<unsigned>(TerrainId::BehemothPit);
}

bool World::load_binary(const uint8_t* data, size_t size, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (!data || size < 16) return fail("map too short");
    uint32_t magic = uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
                     (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    if (magic != kMapMagic) return fail("bad map magic");
    uint32_t version, w, h;
    std::memcpy(&version, data + 4, 4);
    std::memcpy(&w, data + 8, 4);
    std::memcpy(&h, data + 12, 4);
    if (version != 1 || w == 0 || h == 0 || w > 512 || h > 512)
        return fail("unsupported map header");
    const size_t need = 16 + size_t(w) * h * 4;
    if (size < need) return fail("truncated tile array");
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    tiles.resize(size_t(w) * h);
    for (size_t i = 0; i < tiles.size(); ++i) {
        uint32_t word;
        std::memcpy(&word, data + 16 + i * 4, 4);
        tiles[i] = Tile::from_word(word);
    }
    return true;
}

void Economy::recalculate(const World& world) {
    float cap = 0;
    int count = 0;
    for (const Tile& t : world.tiles) {
        if (t.terrain == static_cast<uint8_t>(TerrainId::Treasury)) {
            ++count;
            cap += 4000.0f;
        }
    }
    treasures = count;
    gold_capacity = cap;
    if (gold > gold_capacity) gold = gold_capacity;
}

float Economy::deposit(float amount) {
    if (amount <= 0) return 0;
    float accepted = std::min(amount, std::max(0.0f, gold_capacity - gold));
    gold += accepted;
    return accepted;
}

bool Economy::withdraw(float amount) {
    if (amount < 0 || amount > gold) return false;
    gold -= amount;
    return true;
}

void CameraState::clamp(const World& world) {
    zoom = std::max(0.35f, std::min(3.0f, zoom));
    pitch = std::max(0.15f, std::min(1.45f, pitch));
    if (world.width > 0 && world.height > 0) {
        center_x = std::max(0.0f, std::min(float(world.width - 1), center_x));
        center_y = std::max(0.0f, std::min(float(world.height - 1), center_y));
    }
}

Game::Game() {
    state_.camera.center_x = 24;
    state_.camera.center_y = 24;
}

bool Game::load_map(const uint8_t* data, size_t size, std::string* error) {
    if (!state_.world.load_binary(data, size, error)) return false;
    state_.creatures.clear();
    state_.rooms.clear();
    state_.elapsed = 0;
    state_.tick_count = 0;
    state_.victory = state_.defeat = false;
    next_creature_id_ = 1;
    fired_actions_.clear();
    state_.economy.gold = 0;
    state_.economy.recalculate(state_.world);
    state_.mana = 1000.0f;
    state_.research.assign(kDK2SpellCount, 0.0f);
    state_.cooldowns.assign(kDK2SpellCount, 0.0f);
    state_.spells_known.assign(kDK2SpellCount, 0);
    // Sight Of Evil and Possession start known, as in the original campaign.
    if (kDK2SpellCount > 0) state_.spells_known[0] = 1;
    if (kDK2SpellCount > 1) state_.spells_known[1] = 1;
    state_.camera.center_x = state_.world.width * 0.5f;
    state_.camera.center_y = state_.world.height * 0.5f;
    state_.camera.clamp(state_.world);
    return true;
}

void Game::load_catalog(const std::vector<std::string>& creatures,
                        const std::vector<std::string>& rooms,
                        const std::vector<std::string>& spells) {
    state_.known_creatures = creatures;
    state_.known_rooms = rooms;
    state_.known_spells = spells;
}

void Game::set_mission(const std::string& name) {
    state_.mission_name = name;
}

int Game::spawn_creature(int species, int x, int y, int team) {
    if (state_.creatures.size() >= kMaxCreatures) return -1;
    if (!state_.world.walkable(x, y)) return -1;
    Creature c;
    c.id = next_creature_id_++;
    c.species = species;
    c.team = team;
    c.x = float(x) + 0.5f;
    c.y = float(y) + 0.5f;
    c.max_hp = 100.0f;
    c.damage = state_.config.attack_per_second;
    c.speed = state_.config.creature_speed;
    if (species >= 0 && species < static_cast<int>(kDK2CreatureStatCount)) {
        const DK2CreatureStat& s = kDK2CreatureStats[species];
        c.max_hp = float(s.hp);
        c.damage = s.melee_damage;
        c.speed = s.speed;
        c.sight = s.sight_range;
        c.melee_range = std::max(0.5f, s.melee_range * 4.0f);
        c.fear = s.fear;
        c.max_gold = float(s.max_gold_held);
    }
    c.hp = c.max_hp;
    state_.creatures.push_back(c);
    return c.id;
}

bool Game::build_room(int type, int x, int y, int owner) {
    Tile* tile = state_.world.at(x, y);
    if (!tile) return false;
    if (!terrain_walkable(static_cast<TerrainId>(tile->terrain))) return false;
    for (const Room& r : state_.rooms)
        if (r.x == x && r.y == y) return false;
    Room room;
    room.x = x;
    room.y = y;
    room.owner = owner;
    TerrainId terrain = TerrainId::Treasury;
    int cost = -1;
    if (type >= 0 && type < static_cast<int>(state_.known_rooms.size())) {
        const std::string& wanted = state_.known_rooms[static_cast<size_t>(type)];
        for (unsigned i = 0; i < sizeof(kTerrainNames) / sizeof(kTerrainNames[0]); ++i) {
            std::string a = wanted, b = kTerrainNames[i];
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a == b) { terrain = static_cast<TerrainId>(i); break; }
        }
        for (size_t i = 0; i < kDK2RoomCostCount; ++i) {
            std::string a = wanted, b = kDK2RoomCosts[i].name;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a == b) { cost = kDK2RoomCosts[i].gold; break; }
        }
    }
    // Costs are quoted from the shipped manual; -1 means "not documented".
    if (cost > 0 && !state_.economy.withdraw(float(cost))) return false;
    room.type = static_cast<int>(terrain);  // consumers compare TerrainId
    state_.rooms.push_back(room);
    tile->terrain = static_cast<uint8_t>(terrain);
    tile->owner = static_cast<uint8_t>(owner);
    state_.economy.recalculate(state_.world);
    return true;
}

bool Game::cast_spell(int spell_index, int x, int y) {
    if (spell_index < 0 || spell_index >= static_cast<int>(kDK2SpellCount))
        return false;
    if (spell_index >= static_cast<int>(state_.spells_known.size()) ||
        !state_.spells_known[spell_index]) return false;
    if (spell_index < static_cast<int>(state_.cooldowns.size()) &&
        state_.cooldowns[spell_index] > 0.0f) return false;
    const DK2SpellData& spell = kDK2Spells[spell_index];
    if (state_.mana < float(spell.mana_cost)) return false;

    Tile* tile = state_.world.at(x, y);
    if (!tile) return false;
    state_.mana -= float(spell.mana_cost);
    if (spell_index < static_cast<int>(state_.cooldowns.size()))
        state_.cooldowns[spell_index] = spell.recharge_time;

    /* Effects use the real spell id from KeeperSpells.kwd. */
    switch (spell.id) {
        case 1:  /* Sight Of Evil - reveal a 5x5 area */
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (Tile* t = state_.world.at(x + dx, y + dy))
                        t->flags |= 0x80;
            break;
        case 4:  /* Create Imp */
            spawn_creature(0, x, y, 0);
            break;
        case 10: /* Heal - restore a friendly creature nearby */
            for (Creature& c : state_.creatures) {
                if (c.team != 0 || c.state == CreatureState::Dead) continue;
                if (std::fabs(c.x - (x + 0.5f)) < 2.0f &&
                    std::fabs(c.y - (y + 0.5f)) < 2.0f) {
                    c.hp = std::min(c.max_hp, c.hp + c.max_hp * 0.5f);
                }
            }
            break;
        case 6:  /* Thunderbolt - damage nearby enemies */
        case 9:  /* Inferno */
        case 8:  /* Tremor */
            for (Creature& c : state_.creatures) {
                if (c.team == 0 || c.state == CreatureState::Dead) continue;
                if (std::fabs(c.x - (x + 0.5f)) < 3.0f &&
                    std::fabs(c.y - (y + 0.5f)) < 3.0f)
                    c.hp -= c.max_hp * (spell.id == 9 ? 0.8f : 0.4f);
                if (c.hp <= 0) { c.hp = 0; c.state = CreatureState::Dead; }
            }
            break;
        case 11: /* Chicken - cripple an enemy */
            for (Creature& c : state_.creatures) {
                if (c.team != 0 && std::fabs(c.x - x) < 2.0f &&
                    std::fabs(c.y - y) < 2.0f) {
                    c.hp = std::min(c.hp, c.max_hp * 0.25f);
                    c.speed *= 0.5f;
                }
            }
            break;
        case 13: /* Create Gold */
            state_.economy.deposit(1000.0f);
            break;
        case 3:  /* Call To Arms - raise friendly mood */
            for (Creature& c : state_.creatures)
                if (c.team == 0) c.mood = std::min(100.0f, c.mood + 20.0f);
            break;
        default: break;
    }
    return true;
}

bool Game::research_spell(int spell_index) {
    if (spell_index < 0 || spell_index >= static_cast<int>(kDK2SpellCount))
        return false;
    if (spell_index >= static_cast<int>(state_.spells_known.size()) ||
        state_.spells_known[spell_index]) return false;
    int libraries = 0;
    for (const Room& r : state_.rooms)
        if (r.type == static_cast<int>(TerrainId::Library)) ++libraries;
    if (libraries == 0 || static_cast<int>(state_.research.size()) <= spell_index)
        return false;
    state_.research[spell_index] += kTickSeconds * float(libraries);
    if (state_.research[spell_index] >= kDK2Spells[spell_index].research_time) {
        state_.spells_known[spell_index] = 1;
        return true;
    }
    return false;
}

void Game::update_magic() {
    int hearts = 0;
    for (const Tile& t : state_.world.tiles)
        if (t.terrain == static_cast<uint8_t>(TerrainId::DungeonHeart)) ++hearts;
    state_.mana = std::min(state_.mana_max,
                           state_.mana + 20.0f * kTickSeconds * float(1 + hearts));
    for (float& cd : state_.cooldowns)
        if (cd > 0.0f) cd = std::max(0.0f, cd - kTickSeconds);
    for (unsigned i = 0; i < kDK2SpellCount; ++i)
        if (i < state_.spells_known.size() && !state_.spells_known[i])
            research_spell(static_cast<int>(i));
}

bool Game::find_path(int sx, int sy, int tx, int ty, std::vector<int>& out) const {
    out.clear();
    const World& world = state_.world;
    if (!world.walkable(sx, sy) || !world.walkable(tx, ty)) return false;
    const int n = world.width * world.height;
    const int start = world.index(sx, sy), goal = world.index(tx, ty);
    if (start == goal) return true;
    std::vector<float> g(n, std::numeric_limits<float>::max());
    std::vector<int> parent(n, -1);
    std::vector<uint8_t> closed(n, 0);
    auto heuristic = [&](int a) {
        int ax = a % world.width, ay = a / world.width;
        int dx = std::abs(ax - tx), dy = std::abs(ay - ty);
        return float(dx + dy);
    };
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    g[start] = 0;
    open.push({heuristic(start), start});
    int expanded = 0;
    while (!open.empty() && expanded++ < kMaxPathNodes) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == goal) break;
        int cx = cur % world.width, cy = cur / world.width;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                if (!world.walkable(cx + dx, cy + dy)) continue;
                if (dx && dy && (!world.walkable(cx + dx, cy) ||
                                 !world.walkable(cx, cy + dy)))
                    continue;
                int nb = world.index(cx + dx, cy + dy);
                float step = (dx && dy) ? 1.41421356f : 1.0f;
                float ng = g[cur] + step;
                if (ng < g[nb]) {
                    g[nb] = ng;
                    parent[nb] = cur;
                    open.push({ng + heuristic(nb), nb});
                }
            }
        }
    }
    if (parent[goal] < 0) return false;
    std::vector<int> rev;
    for (int cur = goal; cur != start && cur >= 0; cur = parent[cur])
        rev.push_back(cur);
    std::reverse(rev.begin(), rev.end());
    out = std::move(rev);
    return true;
}

static bool find_room_tile(const std::vector<Room>& rooms,
                           int type, int from_x, int from_y,
                           int& out_x, int& out_y) {
    long best = -1;
    int bx = -1, by = -1;
    for (const Room& room : rooms) {
        if (room.type != type) continue;
        long d = (long)(room.x - from_x) * (room.x - from_x) +
                 (long)(room.y - from_y) * (room.y - from_y);
        if (best < 0 || d < best) { best = d; bx = room.x; by = room.y; }
    }
    if (bx < 0) return false;
    out_x = bx; out_y = by;
    return true;
}

void Game::update_creature(Creature& c) {
    if (c.state == CreatureState::Dead) return;
    const World& world = state_.world;
    int cx = std::max(0, std::min(world.width - 1, int(c.x)));
    int cy = std::max(0, std::min(world.height - 1, int(c.y)));
    const float dt = kTickSeconds;

    /* --- combat: use the creature's real sight and reach ---------------- */
    Creature* enemy = nullptr;
    float best_d2 = c.sight * c.sight;
    for (Creature& other : state_.creatures) {
        if (other.team == c.team || other.state == CreatureState::Dead) continue;
        float dx = other.x - c.x, dy = other.y - c.y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= c.melee_range * c.melee_range) {
            enemy = &other;
            best_d2 = d2;
            break;
        }
        if (d2 < best_d2) { best_d2 = d2; enemy = &other; }
    }
    if (enemy) {
        if (c.fear > 20.0f && enemy->max_hp > c.max_hp * 2.0f) {
            c.job = CreatureJob::Flee;
            c.state = CreatureState::Walking;
            c.target_x = cx - int((enemy->x - c.x));
            c.target_y = cy - int((enemy->y - c.y));
            c.mood = std::max(0.0f, c.mood - 5.0f * dt);
        } else {
            c.job = CreatureJob::Fight;
            c.state = CreatureState::Fighting;
            enemy->hp -= c.damage * dt;
            enemy->mood = std::max(0.0f, enemy->mood - 4.0f * dt);
            if (enemy->hp <= 0.0f) {
                enemy->hp = 0.0f;
                enemy->state = CreatureState::Dead;
                c.mood = std::min(100.0f, c.mood + 6.0f);
            }
        }
    }

    /* --- needs ---------------------------------------------------------- */
    c.hunger = std::min(100.0f, c.hunger + state_.config.hunger_rate * dt);
    c.fatigue = std::min(100.0f, c.fatigue + state_.config.fatigue_rate * dt);
    if (c.hunger > 70.0f || c.fatigue > 70.0f)
        c.mood = std::max(0.0f, c.mood - 2.0f * dt);
    else
        c.mood = std::min(100.0f, c.mood + 0.2f * dt);

    /* --- job selection -------------------------------------------------- */
    if (c.job != CreatureJob::Fight) {
        if (c.gold_carried > 0.0f) {
            c.job = CreatureJob::HaulGold;
        } else if (c.hunger > 60.0f) {
            c.job = CreatureJob::Eat;
        } else if (c.fatigue > 75.0f) {
            c.job = CreatureJob::Sleep;
        } else if (c.job == CreatureJob::Idle || c.job == CreatureJob::Flee) {
            /* Artisan species prefer the workshop, thinkers the library. */
            int wanted = (c.species % 3 == 0)
                ? static_cast<int>(TerrainId::Workshop)
                : static_cast<int>(TerrainId::Library);
            int tx, ty;
            if (find_room_tile(state_.rooms, wanted, cx, cy, tx, ty))
                c.job = (wanted == static_cast<int>(TerrainId::Library))
                    ? CreatureJob::Research : CreatureJob::Train;
            else
                c.job = CreatureJob::Guard;
        }
    }

    /* --- target tile for the selected job ------------------------------- */
    if (c.target_x < 0 && c.path.empty()) {
        int tx = cx, ty = cy;
        bool have_target = false;
        switch (c.job) {
            case CreatureJob::HaulGold:
                have_target = find_room_tile(state_.rooms,
                    static_cast<int>(TerrainId::Treasury), cx, cy, tx, ty);
                break;
            case CreatureJob::Eat:
                have_target = find_room_tile(state_.rooms,
                    static_cast<int>(TerrainId::Hatchery), cx, cy, tx, ty);
                break;
            case CreatureJob::Sleep:
                have_target = find_room_tile(state_.rooms,
                    static_cast<int>(TerrainId::Lair), cx, cy, tx, ty);
                break;
            case CreatureJob::Train:
                have_target = find_room_tile(state_.rooms,
                    static_cast<int>(TerrainId::TrainingRoom), cx, cy, tx, ty);
                break;
            case CreatureJob::Research:
                have_target = find_room_tile(state_.rooms,
                    static_cast<int>(TerrainId::Library), cx, cy, tx, ty);
                break;
            default: break;
        }
        if (have_target) {
            std::vector<int> path;
            if (find_path(cx, cy, tx, ty, path)) {
                c.path = std::move(path);
                c.path_index = 0;
                c.state = CreatureState::Walking;
            }
        }
    }

    /* --- movement ------------------------------------------------------- */
    if (c.path_index < static_cast<int>(c.path.size())) {
        int target = c.path[c.path_index];
        float tx = float(target % world.width) + 0.5f;
        float ty = float(target / world.width) + 0.5f;
        float dx = tx - c.x, dy = ty - c.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        float move = c.speed * dt;
        if (dist <= move || dist < 0.0001f) {
            c.x = tx; c.y = ty;
            ++c.path_index;
        } else {
            c.x += dx / dist * move;
            c.y += dy / dist * move;
        }
    } else if (c.state == CreatureState::Walking) {
        c.state = CreatureState::Idle;
        c.job = CreatureJob::Idle;
        c.path.clear();
        c.target_x = c.target_y = -1;
    }

    /* --- job effects at destination ------------------------------------- */
    const Tile* tile = world.at(cx, cy);
    if (c.job == CreatureJob::Eat &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::Hatchery)) {
        c.hunger = std::max(0.0f, c.hunger - 25.0f * dt);
        c.mood = std::min(100.0f, c.mood + 1.0f * dt);
    }
    if (c.job == CreatureJob::Sleep &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::Lair)) {
        c.fatigue = std::max(0.0f, c.fatigue - 35.0f * dt);
    }
    if (c.job == CreatureJob::Train &&
        tile && tile->terrain == static_cast<uint8_t>(TerrainId::TrainingRoom)) {
        c.damage += 0.02f * dt;
    }
    if (tile && tile->terrain == static_cast<uint8_t>(TerrainId::Gold) &&
        c.gold_carried < c.max_gold && c.hunger < 80.0f) {
        c.gold_carried += state_.config.mining_rate * dt;
        c.job = CreatureJob::Mine;
        if (c.gold_carried >= c.max_gold) {
            c.target_x = c.target_y = -1;
            c.path.clear();
        }
    }
    if (c.gold_carried > 0.0f) {
        for (const Room& room : state_.rooms) {
            if (room.type != static_cast<int>(TerrainId::Treasury)) continue;
            if (std::abs(float(room.x) - c.x) < 1.0f &&
                std::abs(float(room.y) - c.y) < 1.0f) {
                c.gold_carried -= state_.economy.deposit(c.gold_carried);
                break;
            }
        }
    }
}

static const DK2TriggerNode* find_node(const DK2MissionTriggers& set, unsigned id) {
    for (unsigned i = 0; i < set.count; ++i)
        if (set.nodes[i].id == id) return &set.nodes[i];
    return nullptr;
}

static bool condition_true(const DK2TriggerNode& n, const GameState& s) {
    int keeper = 0;
    for (const Creature& c : s.creatures)
        if (c.team == 0 && c.state != CreatureState::Dead) ++keeper;
    switch (n.type) {
        case 1:  return s.elapsed > 2.0f;  /* FLAG: no flag model yet */
        case 2:  return n.value <= 0.0f || s.elapsed >= n.value;  /* TIMER */
        case 19: return float(keeper) >= n.value;                 /* PLAYER_CREATURES */
        case 25: return float(s.rooms.size()) >= n.value;         /* PLAYER_ROOMS */
        case 30: return s.economy.gold >= n.value;                /* PLAYER_GOLD */
        case 34: return s.elapsed >= n.value;                     /* LEVEL_TIME */
        case 47: {
            for (const Creature& c : s.creatures)
                if (c.team == 1 && c.state == CreatureState::Dead) return true;
            return false;
        }
        case 72: return s.elapsed > 2.0f;                         /* GUI_TRANSITION_ENDS */
        default: return false;
    }
}

void Game::evaluate_triggers() {
    const DK2MissionTriggers* set = nullptr;
    for (unsigned i = 0; i < kDK2MissionTriggerCount; ++i) {
        if (state_.mission_name == kDK2MissionTriggers[i].map) {
            set = &kDK2MissionTriggers[i];
            break;
        }
    }
    if (!set) return;

    for (unsigned i = 0; i < set->count; ++i) {
        const DK2TriggerNode& cond = set->nodes[i];
        if (cond.is_action) continue;

        /* idNext is a priority/first-match OR chain; idChild is the action. */
        bool satisfied = condition_true(cond, state_);
        unsigned next = cond.next;
        for (int depth = 0; !satisfied && next != 0 && next != 65535 && depth < 32; ++depth) {
            const DK2TriggerNode* nx = find_node(*set, next);
            if (!nx || nx->is_action) break;
            satisfied = condition_true(*nx, state_);
            next = nx->next;
        }
        if (!satisfied || cond.child == 0 || cond.child == 65535) continue;
        const DK2TriggerNode* action = find_node(*set, cond.child);
        if (!action || !action->is_action) continue;
        if (fired_actions_.count(action->id)) continue;
        fired_actions_.insert(action->id);

        switch (action->type) {
            case 10: state_.victory = true; break;
            case 11: state_.defeat = true; break;
            case 12: break;
            case 13: break;
            case 1: {
                int x = state_.world.width / 2, y = state_.world.height / 2;
                spawn_creature(0, x, y, 1);
                break;
            }
            default: break;
        }
    }
}

void Game::step() {
    if (spell_cooldown_left_ > 0)
        spell_cooldown_left_ = std::max(0.0f, spell_cooldown_left_ - kTickSeconds);
    evaluate_triggers();
    update_magic();
    for (Creature& c : state_.creatures) update_creature(c);
    state_.elapsed += kTickSeconds;
    ++state_.tick_count;
    int keeper_alive = 0, enemies_alive = 0;
    for (const Creature& c : state_.creatures) {
        if (c.state == CreatureState::Dead) continue;
        if (c.team == 0) ++keeper_alive; else ++enemies_alive;
    }
    if (keeper_alive == 0 && !state_.creatures.empty()) state_.defeat = true;

    const DK2MissionRule* rule = nullptr;
    for (unsigned i = 0; i < kDK2MissionRuleCount; ++i) {
        if (state_.mission_name == kDK2MissionRules[i].map) {
            rule = &kDK2MissionRules[i];
            break;
        }
    }
    bool rule_has_requirement = rule && (rule->required_gold > 0 ||
        rule->required_creatures > 0 || rule->required_rooms > 0 ||
        rule->enemy_must_die != 0);
    if (rule && rule_has_requirement) {
        bool gold_ok = state_.economy.gold >= rule->required_gold;
        bool creatures_ok = float(keeper_alive) >= rule->required_creatures;
        bool rooms_ok = float(state_.rooms.size()) >= rule->required_rooms;
        bool time_ok = state_.elapsed >= rule->min_time;
        bool enemy_ok = rule->enemy_must_die ? (enemies_alive == 0) : true;
        if (gold_ok && creatures_ok && rooms_ok && time_ok && enemy_ok)
            state_.victory = true;
        if (rule->min_time > 0 && state_.elapsed > rule->min_time * 6.0f &&
            !state_.victory)
            state_.defeat = true;
    } else {
        if (enemies_alive == 0 && keeper_alive > 0 && state_.elapsed > 30.0f)
            state_.victory = true;
    }
}

bool Game::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    SaveHeader h{};
    h.magic = kSaveMagic;
    h.version = kSaveVersion;
    h.world_w = uint32_t(state_.world.width);
    h.world_h = uint32_t(state_.world.height);
    h.creature_count = uint32_t(state_.creatures.size());
    h.room_count = uint32_t(state_.rooms.size());
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    for (const Tile& t : state_.world.tiles) {
        uint32_t word = t.to_word();
        out.write(reinterpret_cast<const char*>(&word), 4);
    }
    for (const Creature& c : state_.creatures) {
        out.write(reinterpret_cast<const char*>(&c.id), 4);
        out.write(reinterpret_cast<const char*>(&c.species), 4);
        out.write(reinterpret_cast<const char*>(&c.team), 4);
        int state = static_cast<int>(c.state);
        out.write(reinterpret_cast<const char*>(&state), 4);
        out.write(reinterpret_cast<const char*>(&c.x), 4);
        out.write(reinterpret_cast<const char*>(&c.y), 4);
        out.write(reinterpret_cast<const char*>(&c.hp), 4);
        out.write(reinterpret_cast<const char*>(&c.max_hp), 4);
        out.write(reinterpret_cast<const char*>(&c.hunger), 4);
        out.write(reinterpret_cast<const char*>(&c.fatigue), 4);
        out.write(reinterpret_cast<const char*>(&c.gold_carried), 4);
    }
    for (const Room& r : state_.rooms) {
        out.write(reinterpret_cast<const char*>(&r.type), 4);
        out.write(reinterpret_cast<const char*>(&r.x), 4);
        out.write(reinterpret_cast<const char*>(&r.y), 4);
        out.write(reinterpret_cast<const char*>(&r.owner), 4);
        out.write(reinterpret_cast<const char*>(&r.integrity), 4);
    }
    out.write(reinterpret_cast<const char*>(&state_.economy.gold), 4);
    out.write(reinterpret_cast<const char*>(&state_.elapsed), 4);
    uint64_t ticks = state_.tick_count;
    out.write(reinterpret_cast<const char*>(&ticks), 8);
    return out.good();
}

bool Game::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    SaveHeader h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (!in || h.magic != kSaveMagic || h.version != kSaveVersion) return false;
    state_.world.width = int(h.world_w);
    state_.world.height = int(h.world_h);
    state_.world.tiles.resize(size_t(h.world_w) * h.world_h);
    for (Tile& t : state_.world.tiles) {
        uint32_t word;
        in.read(reinterpret_cast<char*>(&word), 4);
        t = Tile::from_word(word);
    }
    state_.creatures.resize(h.creature_count);
    for (Creature& c : state_.creatures) {
        in.read(reinterpret_cast<char*>(&c.id), 4);
        in.read(reinterpret_cast<char*>(&c.species), 4);
        in.read(reinterpret_cast<char*>(&c.team), 4);
        int state;
        in.read(reinterpret_cast<char*>(&state), 4);
        c.state = static_cast<CreatureState>(state);
        in.read(reinterpret_cast<char*>(&c.x), 4);
        in.read(reinterpret_cast<char*>(&c.y), 4);
        in.read(reinterpret_cast<char*>(&c.hp), 4);
        in.read(reinterpret_cast<char*>(&c.max_hp), 4);
        in.read(reinterpret_cast<char*>(&c.hunger), 4);
        in.read(reinterpret_cast<char*>(&c.fatigue), 4);
        in.read(reinterpret_cast<char*>(&c.gold_carried), 4);
    }
    state_.rooms.resize(h.room_count);
    for (Room& r : state_.rooms) {
        in.read(reinterpret_cast<char*>(&r.type), 4);
        in.read(reinterpret_cast<char*>(&r.x), 4);
        in.read(reinterpret_cast<char*>(&r.y), 4);
        in.read(reinterpret_cast<char*>(&r.owner), 4);
        in.read(reinterpret_cast<char*>(&r.integrity), 4);
    }
    in.read(reinterpret_cast<char*>(&state_.economy.gold), 4);
    in.read(reinterpret_cast<char*>(&state_.elapsed), 4);
    in.read(reinterpret_cast<char*>(&state_.tick_count), 8);
    state_.economy.recalculate(state_.world);
    next_creature_id_ = 1;
    for (const Creature& c : state_.creatures)
        next_creature_id_ = std::max(next_creature_id_, c.id + 1);
    return in.good();
}

void Game::on_pan(float dx, float dy) {
    state_.camera.center_x -= dx / std::max(0.05f, state_.camera.zoom);
    state_.camera.center_y -= dy / std::max(0.05f, state_.camera.zoom);
    state_.camera.clamp(state_.world);
}

void Game::on_zoom(float factor) {
    state_.camera.zoom *= factor;
    state_.camera.clamp(state_.world);
}

void Game::on_rotate(float delta) {
    state_.camera.yaw += delta;
    state_.camera.clamp(state_.world);
}

void Game::select_at(float wx, float wy) {
    selected_creature = -1;
    float best = 1.5f * 1.5f;
    for (const Creature& c : state_.creatures) {
        float dx = c.x - wx, dy = c.y - wy;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            selected_creature = c.id;
        }
    }
}

}  // namespace dk2
