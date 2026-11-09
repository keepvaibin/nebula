// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/raw_mouse.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Raw mouse model must not include Windows.h
#endif
#include <iostream>
#include <stdexcept>
int main() {
    using galaxy::input::RawMouseAccumulator;
    const auto check = [](bool value) { if (!value) throw std::runtime_error("raw motion invariant"); };
    RawMouseAccumulator mouse;
    mouse.relative(20, -8); mouse.relative(-20, 8);
    auto sample = mouse.sample();
    check(sample.x == 0 && sample.y == 0 && sample.moved);
    check(!mouse.sample().moved);
    mouse.relative(0, 0); check(!mouse.sample().moved);
    mouse.relative(4, 9); mouse.clear(); check(!mouse.sample().moved);
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    check(RawMouseAccumulator::add(max - 1, 2) == max);
    check(RawMouseAccumulator::add(min + 1, -2) == min);
    check(RawMouseAccumulator::add(max, -1) == max - 1);
    check(RawMouseAccumulator::add(min, 1) == min + 1);
    std::cout << "Opposing deltas retain activity; sample/focus clear; int64 saturation pass\n";
}
