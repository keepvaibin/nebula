// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/motion_signals.h"

#include <cstdio>
#include <iterator>
#include <limits>
#include <set>

using namespace galaxy::motion;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void test_category() {
    const std::uint32_t c = encode_category(static_cast<std::uint8_t>(ScenePhase::ImageEffect), kCategoryDrawType, 0x2Du);
    check(category_phase(c) == static_cast<std::uint8_t>(ScenePhase::ImageEffect), "phase round trip");
    check((c & 0xFFu) == 0x2Du && ((c >> 8u) & 0xFFu) == kCategoryDrawType, "source and type");
    check(category_phase(kNoCategory) == 0xFFu, "no phase");
}

void test_particle_age() {
    check(particle_age_continues(5u, 6u), "one step");
    check(particle_age_continues(5u, 5u), "paused");
    check(!particle_age_continues(5u, 0u), "restart");
    check(!particle_age_continues(5u, 7u), "jump");
}

}  // namespace

int main() {
    test_category();
    test_particle_age();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("motion signals: ok\n");
    return 0;
}
