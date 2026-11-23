#pragma once

#include <cstdint>
#include <vector>

namespace dk2 {

// Reconstructed from FUN_00448d80 (Ghidra, DKII.EXE @0x00448d80).
// The original runs this grid pass when `timeGetTime() - last > 0x31`, i.e.
// at most once per 49 ms (~20 Hz).  Coordinates wrap with `& (size-1)`, so
// the grid dimensions are powers of two.  The exact physical meaning of the
// field (water/lava surface is the leading hypothesis) is NOT proven.
class GridSimulation {
public:
    GridSimulation(int width, int height);

    // Exact arithmetic from the decompiled loop:
    //   sum   = n + s + e + w
    //   avg   = (sum + ((sum >> 31) & 3)) >> 2
    //   value = clamp(avg - current + accumulator, -0x1000, +0x1000)
    static int16_t relax(int16_t current, int16_t accumulator,
                         int16_t north, int16_t south,
                         int16_t east, int16_t west);

    void set_blocked(int x, int y, bool blocked);
    void set_accumulator(int x, int y, int16_t value);
    void set_field(int x, int y, int16_t value);
    int16_t field(int x, int y) const;

    void step();

    // Gradient/normal pass from the same function: three int32 per cell.
    // gx = (left - right) * 4, gy = 0x10000, gz = (north2 - north0) * 4.
    struct Normal { int32_t gx, gy, gz; };
    std::vector<Normal> compute_normals() const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_, height_;
    std::vector<int16_t> field_, next_, accumulator_;
    std::vector<uint8_t> blocked_;
    int index(int x, int y) const {
        return (y & (height_ - 1)) * width_ + (x & (width_ - 1));
    }
};

// Reconstructed from FUN_005b5e00 / FUN_00575700 / FUN_00555b10:
// high-resolution counter with a millisecond fallback, FPS = 1000 / delta.
class EngineClock {
public:
    void start();
    void tick(double now_ms);
    double elapsed_ms() const;
    int fps() const { return fps_; }

private:
    double origin_ms_ = 0;
    bool started_ = false;
    double last_ms_ = 0;
    int fps_ = 0;
};

}  // namespace dk2
