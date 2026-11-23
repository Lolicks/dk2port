#include "../engine/DK2.h"
#include "../engine/GridSimulation.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

using namespace dk2;

int main(int argc, char **argv) {
    const char *map = argc > 1 ? argv[1] : "build/Assets/maps/Alcatraz.bin";
    std::ifstream in(map, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    Game game;
    if (!game.load_map(bytes.data(), bytes.size(), nullptr)) {
        std::printf("cannot load %s\n", map);
        return 1;
    }
    for (int i = 0; i < 100; ++i) {
        game.spawn_creature(i % 46, 2 + (i * 7) % (game.state().world.width - 4),
                            2 + (i * 11) % (game.state().world.height - 4), i % 2);
    }
    GameConfig &cfg = game.state().config;

    const int kTicks = 30000;  // ~16 minutes of game time at 30 Hz
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kTicks; ++i) game.step();
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    GridSimulation grid(128, 128);
    for (int i = 0; i < 1024; ++i) grid.set_field(i % 128, (i / 128) % 128, i);
    auto g0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) grid.step();
    auto g1 = std::chrono::steady_clock::now();
    double gms = std::chrono::duration<double, std::milli>(g1 - g0).count();

    std::printf("PROFILE map=%dx%d creatures=%zu\n",
                game.state().world.width, game.state().world.height,
                game.state().creatures.size());
    std::printf("simulation: %d ticks in %.1f ms = %.1f ticks/s (%.2f us/tick)\n",
                kTicks, ms, double(kTicks) * 1000.0 / ms, ms * 1000.0 / kTicks);
    std::printf("grid 128x128: 1000 steps in %.1f ms = %.1f steps/s (%.1f us/step)\n",
                gms, 1000.0 * 1000.0 / gms, gms);
    std::printf("budget at 30 Hz: %.1f%% of one core (wall clock)\n",
                ms / double(kTicks) * 30.0 / 1000.0 * 100.0);
    (void)cfg;
    return 0;
}
