// SPDX-License-Identifier: GPL-3.0-only
// calibration_scene.h: the gate, the visit's resets, its travel, its readiness, its saves and its end.
#include "galaxy/calibration_scene.h"

#include "nebula_game_api.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace scene = galaxy::calibration_scene;

void check(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}

std::int64_t g_now = 1'000'000'000;
std::int64_t test_clock() {
    return g_now;
}
void wait_ms(std::int64_t ms) {
    g_now += ms * 1'000'000;
}

bool g_open = true;
bool gate() {
    return g_open;
}

int frame(std::string_view current, std::string* stage = nullptr, std::int32_t* star = nullptr) {
    char name[64] = {};
    std::int32_t asked = 0;
    const int travel = scene::scene_frame(current, name, sizeof(name), &asked);
    if (stage != nullptr) *stage = name;
    if (star != nullptr) *star = asked;
    return travel;
}

// From start() to a ready stage: the start's reset, the title, the travel, the arrival, play shown and quiet.
void visit_to_ready() {
    check(scene::start(), "a visit starts through an open gate");
    check(scene::active() && scene::live() && scene::waiting(), "a visit runs and waits");
    check(!scene::saves_in_memory(), "the saves stay the player's until the game takes the reset");
    check(scene::take_reset(), "the start's reset is asked for");
    check(!scene::take_reset(), "once");
    check(scene::saves_in_memory(), "the start's reset reads the blank saves");
    check(frame("TitleStage") == NEBULA_GALAXY_TRAVEL_NONE, "no travel before the reset is done");
    scene::reset_done();
    check(frame("TitleStage") == NEBULA_GALAXY_TRAVEL_NONE, "the title starts first");
    wait_ms(2100);
    std::string stage;
    std::int32_t star = 0;
    check(frame("TitleStage", &stage, &star) == NEBULA_GALAXY_TRAVEL_STAGE, "the travel is asked");
    check(stage == scene::kStage && star == scene::kStar, "to the reference stage");
    check(frame("TitleStage") == NEBULA_GALAXY_TRAVEL_NONE, "asked once");
    check(frame(scene::kStage) == NEBULA_GALAXY_TRAVEL_NONE, "arrived");
    check(scene::waiting(), "the opening still runs");
    scene::note_play(true);
    check(frame(scene::kStage) == NEBULA_GALAXY_TRAVEL_NONE && scene::waiting(), "play shown, not yet quiet");
    wait_ms(600);
    (void)frame(scene::kStage);
    check(!scene::waiting() && scene::active(), "ready after play was shown quietly");
}

void end_to_off() {
    scene::stop();
    check(!scene::active() && scene::live(), "ended: no longer active, still live");
    check(scene::saves_in_memory(), "the saves stay the visit's until the game takes the end's reset");
    check(scene::take_reset(), "the end's reset is asked for");
    check(!scene::saves_in_memory(), "the end's reset reads the player's saves");
    scene::reset_done();
    check(scene::live(), "live until the title starts");
    scene::scene_started();
    check(!scene::live() && !scene::active() && !scene::waiting(), "off once the title starts");
    check(!scene::take_reset(), "nothing more asked");
}
}  // namespace

int main() {
    try {
        scene::set_clock_for_tests(test_clock);
        check(!scene::start(), "no gate installed: no visit");
        scene::set_gate(gate);
        g_open = false;
        check(!scene::start(), "a closed gate: no visit");
        check(!scene::live() && !scene::active(), "nothing runs");
        g_open = true;

        // A whole visit, its saves in memory.
        visit_to_ready();
        check(!scene::start(), "one visit at a time");
        char data[4] = {'s', 'a', 'v', 'e'};
        check(scene::save_request(NEBULA_SAVE_WRITE, "GameData.bin", data, sizeof(data), nullptr) == NEBULA_SAVE_DONE,
              "a write is kept");
        char back[8] = {};
        std::uint32_t length = 0u;
        check(scene::save_request(NEBULA_SAVE_READ, "GameData.bin", back, sizeof(back), &length) == NEBULA_SAVE_DONE &&
                  length == sizeof(data) && std::memcmp(back, data, sizeof(data)) == 0,
              "and read back");
        end_to_off();

        // The next visit starts blank.
        visit_to_ready();
        check(scene::save_request(NEBULA_SAVE_READ, "GameData.bin", back, sizeof(back), &length) == NEBULA_SAVE_NO_FILE,
              "a new visit's saves are blank");
        end_to_off();

        // Ended before the game took the start's reset: nothing happens to the game.
        check(scene::start(), "a visit starts");
        scene::stop();
        check(!scene::live() && !scene::take_reset(), "the untaken reset is withdrawn");

        // Ended while the start's reset runs: its done is not the end's.
        check(scene::start(), "a visit starts");
        check(scene::take_reset(), "the start's reset is taken");
        scene::stop();
        scene::reset_done();
        check(scene::live(), "the start's reset done: the end's still owed");
        check(scene::take_reset() && !scene::saves_in_memory(), "the end's reset reads the player's saves");
        scene::reset_done();
        scene::scene_started();
        check(!scene::live(), "off once the title starts");

        // A travel the game never answers: asked again, then the visit stops waiting.
        check(scene::start(), "a visit starts");
        check(scene::take_reset(), "reset");
        scene::reset_done();
        wait_ms(2100);
        check(frame("TitleStage") == NEBULA_GALAXY_TRAVEL_STAGE, "first ask");
        wait_ms(20100);
        check(frame("TitleStage") == NEBULA_GALAXY_TRAVEL_STAGE, "second ask");
        wait_ms(20100);
        check(frame("TitleStage") == NEBULA_GALAXY_TRAVEL_STAGE, "third ask");
        wait_ms(20100);
        check(frame("TitleStage") == NEBULA_GALAXY_TRAVEL_NONE && !scene::waiting(), "given up: no longer waiting");
        end_to_off();

        // The title that never reports its start after the end's reset: live ends by time.
        visit_to_ready();
        scene::stop();
        check(scene::take_reset(), "the end's reset");
        scene::reset_done();
        wait_ms(5100);
        (void)frame("TitleStage");
        check(!scene::live(), "live ends after its longest wait");

        std::cout << "calibration scene tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "calibration scene test failed: " << error.what() << '\n';
        return 1;
    }
}
