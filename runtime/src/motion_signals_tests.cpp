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

void test_epochs() {
    EpochTable table;
    check(table.get(0x80001000u) == 0u && table.size() == 0u, "untouched objects have epoch 0");
    check(table.bump(0x80001000u) == 1u, "first bump");
    check(table.bump(0x80001000u) == 2u, "second bump");
    check(table.get(0x80001000u) == 2u, "epoch kept");
    check(table.get(0x80002000u) == 0u, "other objects unaffected");
    check(table.bump(0u) == 0u && table.get(0u) == 0u && table.size() == 1u, "null object ignored");
}

void test_epoch_reset_changes_every_identity() {
    EpochTable table;
    const std::uint32_t kept = 0x81000000u;
    const std::uint16_t before_kept = table.get(kept);
    std::uint32_t object = 0x80000000u;
    while (table.resets() == 0u) {
        table.bump(object);
        object += 4u;
    }
    check(table.size() == 1u, "a full table restarts with the bumped object");
    check(table.get(kept) != before_kept, "the salt changes untouched objects too");
    check(table.get(object - 4u) == static_cast<std::uint16_t>((1u + table.get(kept)) & EpochTable::kMask),
          "the bumped object is one past the salt");
}

void test_epochs_spread() {
    EpochTable table;
    std::set<std::uint16_t> seen;
    for (std::uint32_t i = 0; i < 1000u; ++i) {
        table.bump(0x80400000u + i * 0x40u);
    }
    for (std::uint32_t i = 0; i < 1000u; ++i) {
        seen.insert(table.get(0x80400000u + i * 0x40u));
    }
    check(seen.size() == 1u && *seen.begin() == 1u, "1000 objects keep their own epochs");
    check(table.size() == 1000u, "1000 entries");
}

void test_category() {
    const std::uint32_t c = encode_category(static_cast<std::uint8_t>(ScenePhase::ImageEffect), kCategoryDrawType, 0x2Du);
    check(category_phase(c) == static_cast<std::uint8_t>(ScenePhase::ImageEffect), "phase round trip");
    check((c & 0xFFu) == 0x2Du && ((c >> 8u) & 0xFFu) == kCategoryDrawType, "source and type");
    check(category_phase(kNoCategory) == 0xFFu, "no phase");
}

void test_player_jump() {
    const float here[3] = {100.0f, 50.0f, -20.0f};
    const float step[3] = {130.0f, 50.0f, -20.0f};
    const float far[3] = {2634.0f, 385.0f, 1322.0f};
    const float nan[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    check(!player_jump_is_new_life(here, step), "carrying the player is no new life");
    check(player_jump_is_new_life(here, far), "a teleport is a new life");
    check(!player_jump_is_new_life(here, nan), "an unreadable target is no new life");
}

void test_marker_kinds() {
    const std::uint8_t kinds[] = {kMarkerShapePacket, kMarkerNameObj,    kMarkerEmitter,      kMarkerParticle,
                                  kMarkerCategory,    kMarkerScene,      kMarkerPass,         kMarkerLight,
                                  kMarkerLayoutPane,  kMarkerLayoutActor, kMarkerStarPointer};
    const std::set<std::uint8_t> seen(std::begin(kinds), std::end(kinds));
    check(seen.size() == std::size(kinds), "marker kinds are distinct");
    check(*seen.begin() == 1u && *seen.rbegin() + 1u == kMarkerKinds, "marker kinds fill 1..kMarkerKinds-1");
    check((kLayoutIn2D & kStarPointerDirector) == 0u, "layout aux bits are distinct");
    check(static_cast<std::uint32_t>(Layout2DSite::SceneEnd) == 1u &&
              static_cast<std::uint32_t>(Layout2DSite::StarPointerDirector) == 5u,
          "Layout2D sites keep the translator's arguments");
}

void test_particle_age() {
    check(particle_age_continues(5u, 6u), "one step");
    check(particle_age_continues(5u, 5u), "paused");
    check(!particle_age_continues(5u, 0u), "restart");
    check(!particle_age_continues(5u, 7u), "jump");
}

}  // namespace

int main() {
    test_epochs();
    test_epoch_reset_changes_every_identity();
    test_epochs_spread();
    test_category();
    test_particle_age();
    test_marker_kinds();
    test_player_jump();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("motion signals: ok\n");
    return 0;
}
