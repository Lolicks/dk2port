#include "GridSimulation.h"

#include <algorithm>
#include <chrono>

namespace dk2 {

GridSimulation::GridSimulation(int width, int height)
    : width_(width), height_(height),
      field_(size_t(width * height), 0),
      next_(size_t(width * height), 0),
      accumulator_(size_t(width * height), 0),
      blocked_(size_t(width * height), 0) {
    if (width_ < 2 || height_ < 2) {
        width_ = height_ = 2;
        field_.assign(4, 0);
        next_.assign(4, 0);
        accumulator_.assign(4, 0);
        blocked_.assign(4, 0);
    }
}

int16_t GridSimulation::relax(int16_t current, int16_t accumulator,
                              int16_t north, int16_t south,
                              int16_t east, int16_t west) {
    int sum = int(north) + int(south) + int(east) + int(west);
    int avg = (sum + ((sum >> 31) & 3)) >> 2;
    int value = avg - int(current) + int(accumulator);
    return static_cast<int16_t>(std::max(-0x1000, std::min(0x1000, value)));
}

void GridSimulation::set_blocked(int x, int y, bool blocked) {
    blocked_[size_t(index(x, y))] = blocked ? 1 : 0;
}

void GridSimulation::set_accumulator(int x, int y, int16_t value) {
    accumulator_[size_t(index(x, y))] = value;
}

void GridSimulation::set_field(int x, int y, int16_t value) {
    field_[size_t(index(x, y))] = value;
}

int16_t GridSimulation::field(int x, int y) const {
    return field_[size_t(index(x, y))];
}

void GridSimulation::step() {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int i = index(x, y);
            const bool blocked = blocked_[size_t(i)] != 0;
            if (blocked) {
                next_[size_t(i)] = 0;
                continue;
            }
            const int16_t north = field_[size_t(index(x, y - 1))];
            const int16_t south = field_[size_t(index(x, y + 1))];
            const int16_t east  = field_[size_t(index(x + 1, y))];
            const int16_t west  = field_[size_t(index(x - 1, y))];
            next_[size_t(i)] = relax(field_[size_t(i)], accumulator_[size_t(i)],
                                     north, south, east, west);
        }
    }
    // The original keeps two short grids whose roles alternate.
    field_.swap(next_);
}

std::vector<GridSimulation::Normal> GridSimulation::compute_normals() const {
    std::vector<Normal> out(size_t(width_ * height_));
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int i = index(x, y);
            const int32_t left  = field_[size_t(index(x - 1, y))];
            const int32_t right = field_[size_t(index(x + 1, y))];
            const int32_t n0 = field_[size_t(index(x, y))];
            const int32_t n2 = field_[size_t(index(x, y - 2))];
            out[size_t(i)] = Normal{(left - right) * 4, 0x10000, (n2 - n0) * 4};
        }
    }
    return out;
}

void EngineClock::start() {
    started_ = true;
    origin_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    last_ms_ = origin_ms_;
}

void EngineClock::tick(double now_ms) {
    if (!started_) start();
    const double delta = now_ms - last_ms_;
    if (delta > 0.0) fps_ = static_cast<int>(1000.0 / delta);
    last_ms_ = now_ms;
}

double EngineClock::elapsed_ms() const {
    if (!started_) return 0.0;
    const double now = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return now - origin_ms_;
}

}  // namespace dk2
