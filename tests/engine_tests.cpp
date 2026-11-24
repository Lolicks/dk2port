#include "../engine/DK2.h"
#include "../engine/GridSimulation.h"
#include "../engine/SaveFile.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace dk2;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main(int argc, char** argv) {
    std::string map_path = argc > 1 ? argv[1] : "build/Assets/maps/Alcatraz.bin";
    auto bytes = read_file(map_path);
    CHECK(!bytes.empty());
    if (bytes.empty()) return 1;

    Game game;
    std::string error;
    CHECK(game.load_map(bytes.data(), bytes.size(), &error));
    CHECK(error.empty());
    CHECK(game.state().world.width > 0);
    CHECK(game.state().world.height > 0);
    std::printf("map: %dx%d, %zu tiles\n", game.state().world.width,
                game.state().world.height, game.state().world.tiles.size());

    size_t walkable = 0;
    for (const Tile& t : game.state().world.tiles) {
        if (terrain_walkable(static_cast<TerrainId>(t.terrain))) ++walkable;
    }
    CHECK(walkable > 0);
    std::printf("walkable tiles: %zu\n", walkable);

    int sx = -1, sy = -1, tx = -1, ty = -1;
    const World& w = game.state().world;
    for (int y = 0; y < w.height && tx < 0; ++y) {
        for (int x = 0; x < w.width; ++x) {
            if (!w.walkable(x, y)) continue;
            if (sx < 0) { sx = x; sy = y; continue; }
            if (std::abs(x - sx) + std::abs(y - sy) > 8) { tx = x; ty = y; break; }
        }
    }
    CHECK(sx >= 0 && tx >= 0);
    if (sx >= 0 && tx >= 0) {
        std::vector<int> path;
        bool found = game.find_path(sx, sy, tx, ty, path);
        std::printf("path %d,%d -> %d,%d: %s (%zu nodes)\n", sx, sy, tx, ty,
                    found ? "ok" : "none", path.size());
        CHECK(found);
        CHECK(!path.empty());

        int id = game.spawn_creature(0, sx, sy);
        CHECK(id > 0);
        /* Give the creature a Hatchery at the target so the job system
           drives it there through A* (as the original needs system does). */
        Room hatch;
        hatch.type = static_cast<int>(TerrainId::Hatchery);
        hatch.x = tx; hatch.y = ty;
        game.state().rooms.push_back(hatch);
        game.state().creatures[0].hunger = 100.0f;
        LoopDriver loop(game);
        for (int i = 0; i < 1200; ++i) loop.advance(1.0f / 60.0f);
        CHECK(game.state().tick_count > 550);
        CHECK(game.state().tick_count < 650);
        std::printf("ticks after 20s at 60fps: %llu, creature at %.2f,%.2f\n",
                    (unsigned long long)game.state().tick_count,
                    game.state().creatures[0].x, game.state().creatures[0].y);
    }

    int treasury_count = 0;
    for (const Tile& t : w.tiles)
        if (t.terrain == static_cast<uint8_t>(TerrainId::Treasury))
            ++treasury_count;
    std::printf("treasury tiles: %d, capacity %.0f\n", treasury_count,
                game.state().economy.gold_capacity);
    CHECK(treasury_count > 0);
    CHECK(game.state().economy.gold_capacity > 0);
    CHECK(game.state().economy.deposit(100.0f) == 100.0f);
    CHECK(game.state().economy.withdraw(50.0f));
    CHECK(!game.state().economy.withdraw(100000.0f));

    game.load_catalog({"Imp", "Bile Demon"}, {"Treasury"}, {"Sight Of Evil"});
    CHECK(game.state().known_creatures.size() == 2);
    game.state().mana = 100000.0f;  // enough for Sight Of Evil (5000)
    if (sx >= 0) {
        game.state().economy.gold = 1000;
        bool built = game.build_room(0, sx, sy, 1);
        std::printf("build Treasury at %d,%d: %d, gold %.0f\n",
                    sx, sy, int(built), game.state().economy.gold);
        CHECK(built);
        CHECK(std::fabs(game.state().economy.gold - 800.f) < 0.01f);
    }
    float mana_before = game.state().mana;
    CHECK(game.cast_spell(0, sx >= 0 ? sx : 0, sy >= 0 ? sy : 0));
    CHECK(game.state().mana < mana_before);
    std::printf("magic: Sight Of Evil cost=%.0f mana now=%.0f\n",
                mana_before - game.state().mana, game.state().mana);
    CHECK(!game.cast_spell(99, 0, 0));  // invalid spell index
    if (game.state().spells_known.size() > 6) {
        game.state().spells_known[6] = 1;  // Turncoat: recharge 60 s
        CHECK(game.cast_spell(6, sx >= 0 ? sx : 0, sy >= 0 ? sy : 0));
        CHECK(game.state().cooldowns[6] > 0.0f);
        CHECK(!game.cast_spell(6, sx >= 0 ? sx : 0, sy >= 0 ? sy : 0));
        std::printf("magic: Turncoat cooldown=%.1fs\n", game.state().cooldowns[6]);
    }

    // Combat: a keeper creature and an invader at the same tile must resolve.
    if (sx >= 0 && tx >= 0) {
        Game combat;
        CHECK(combat.load_map(bytes.data(), bytes.size(), nullptr));
        int keeper = combat.spawn_creature(0, sx, sy, 0);
        int hero = combat.spawn_creature(7, sx, sy, 1);
        CHECK(keeper > 0 && hero > 0);
        for (int i = 0; i < 600; ++i) combat.step();
        CHECK(combat.state().creatures[1].state == CreatureState::Dead ||
              combat.state().creatures[0].state == CreatureState::Dead);
        std::printf("combat: keeper hp %.1f, hero hp %.1f, victory=%d\n",
                    combat.state().creatures[0].hp,
                    combat.state().creatures[1].hp,
                    int(combat.state().victory));
    }

    game.set_mission("level1");
    const std::string save_path = "/tmp/dk2_engine_test.sav";
    CHECK(game.save(save_path));
    Game restored;
    CHECK(restored.load(save_path));
    CHECK(restored.state().world.width == game.state().world.width);
    CHECK(restored.state().creatures.size() == game.state().creatures.size());
    CHECK(std::fabs(restored.state().economy.gold - game.state().economy.gold) < 0.01f);
    CHECK(std::fabs(restored.state().mana - game.state().mana) < 0.01f);
    CHECK(restored.state().spells_known == game.state().spells_known);
    CHECK(restored.state().mission_name == "level1");
    CHECK(restored.state().creatures[0].job == game.state().creatures[0].job);
    std::remove(save_path.c_str());

    game.on_pan(10.f, -5.f);
    game.on_zoom(2.f);
    game.on_rotate(0.2f);
    CHECK(game.state().camera.zoom <= 3.0f);
    CHECK(game.state().camera.center_x >= 0.f);

    // Real stats recovered from Creatures.kwd via dk2-tools templates.
    {
        Game real;
        CHECK(real.load_map(bytes.data(), bytes.size(), nullptr));
        int imp = real.spawn_creature(0, sx >= 0 ? sx : 1, sy >= 0 ? sy : 1, 0);
        CHECK(imp > 0);
        if (imp > 0) {
            const Creature &c = real.state().creatures[0];
            CHECK(c.max_hp == 500);
            CHECK(c.damage == 30);
            CHECK(std::fabs(c.speed - 1.839844f) < 0.001f);
            std::printf("stats: Imp hp=%.0f dmg=%.0f speed=%.4f\n",
                        c.max_hp, c.damage, c.speed);
        }
        real.set_mission("level1");
        real.state().economy.gold = 12000;
        real.state().economy.gold_capacity = 100000;
        for (int i = 0; i < 3; ++i)
            real.spawn_creature(0, sx >= 0 ? sx : 1, sy >= 0 ? sy : 1, 0);
        CHECK(real.build_room(0, sx >= 0 ? sx : 1, sy >= 0 ? sy : 1, 0));
        for (int i = 0; i < 300; ++i) real.step();
        CHECK(real.state().victory);
        std::printf("mission level1: victory=%d gold=%.0f t=%.1f\n",
                    int(real.state().victory), real.state().economy.gold,
                    real.state().elapsed);
    }

    // Custom DK2Port save container
    {
        const std::string sp = "/tmp/dk2_container_test.sav";
        std::string error;
        save::SaveInfo meta;
        meta.timestamp = 1234567;
        meta.tick = 9876;
        meta.elapsed = 321.5f;
        meta.creature_count = 7;
        strncpy(meta.mission, "level1", 31);
        const uint8_t payload[] = "DK2S-payload-for-container-test";
        CHECK(save::write_container(sp, payload, sizeof(payload), meta, &error));
        save::SaveInfo read;
        CHECK(save::read_info(sp, &read, &error));
        CHECK(read.valid);
        CHECK(read.container_version == save::kContainerVersion);
        CHECK(read.tick == 9876);
        CHECK(read.creature_count == 7);
        CHECK(std::string(read.mission) == "level1");
        std::string extracted = sp + ".out";
        CHECK(save::extract_payload(sp, extracted, &read, &error));
        std::ifstream ef(extracted, std::ios::binary);
        std::vector<char> got((std::istreambuf_iterator<char>(ef)),
                              std::istreambuf_iterator<char>());
        CHECK(std::string(got.data(), got.size()) == std::string((const char*)payload, sizeof(payload)));
        ef.close();

        // Corrupt one payload byte -> CRC must fail.
        FILE* f = fopen(sp.c_str(), "r+b");
        if (f) {
            fseek(f, -2, SEEK_END);
            fputc('X', f);
            fclose(f);
        }
        CHECK(!save::extract_payload(sp, extracted, &read, &error));
        CHECK(!error.empty());
        std::remove(extracted.c_str());
        std::remove(sp.c_str());
        std::printf("save container: crc/metadata/corruption OK\n");
    }

    // Grid simulation reconstructed from Ghidra FUN_00448d80.
    CHECK(GridSimulation::relax(0, 0, 4, 4, 4, 4) == 4);
    CHECK(GridSimulation::relax(0, 0, -1, 0, 0, 0) == 0);
    CHECK(GridSimulation::relax(0, 0, 30000, 30000, 30000, 30000) == 0x1000);
    CHECK(GridSimulation::relax(0, 0, -30000, -30000, -30000, -30000) == -0x1000);
    GridSimulation grid(8, 8);
    grid.set_field(0, 0, 100);
    grid.step();
    CHECK(grid.field(0, 0) != 100);
    CHECK(grid.field(7, 0) != 0);  // toroidal wrap
    grid.set_blocked(1, 1, true);
    grid.set_field(1, 1, 500);
    grid.step();
    CHECK(grid.field(1, 1) == 0);
    auto normals = grid.compute_normals();
    CHECK(normals.size() == 64);
    CHECK(normals[0].gy == 0x10000);
    std::printf("grid: relax/wrap/block/normals OK\n");

    EngineClock clock;
    clock.start();
    clock.tick(1000);
    clock.tick(1016);
    CHECK(clock.fps() == 62);
    CHECK(clock.elapsed_ms() >= 0.0);

    if (failures == 0) std::printf("ENGINE TESTS: OK\n");
    else std::printf("ENGINE TESTS: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
