// SPDX-License-Identifier: GPL-3.0-only
// A key event of a host whose keys are physical places (SDL on Linux; galaxy/input/key_positions.h apply_physical_key_event)
// against the default keyboard layout: W A S D move, Space is A, Control is Z. The layout binds the letters to places
// (KeyPosition), so a host that sets only the key's own level (what the SDL host did) moves nothing with them while
// Space and Control, which are keys, still work.
#include "galaxy/input/key_codes.h"
#include "galaxy/input/key_positions.h"

#include <iostream>
#include <stdexcept>

namespace {
using namespace galaxy::input;

void check(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}

LogicalInput play(const KeyboardMouseLevels& levels) {
    return evaluate_keyboard_bindings(default_controls_config().table(ControlScheme::Keyboard), levels);
}

void down(KeyboardMouseLevels& levels, int vk) { apply_physical_key_event(levels, vk, true); }
void up(KeyboardMouseLevels& levels, int vk) { apply_physical_key_event(levels, vk, false); }

void movement() {
    KeyboardMouseLevels levels;
    down(levels, 'W');
    check(play(levels).stick_y == 1.0f && play(levels).stick_x == 0.0f, "W is stick up");
    up(levels, 'W');
    check(play(levels).stick_y == 0.0f, "releasing W lets the stick go");
    down(levels, 'S');
    check(play(levels).stick_y == -1.0f, "S is stick down");
    up(levels, 'S');
    down(levels, 'A');
    check(play(levels).stick_x == -1.0f, "A is stick left");
    up(levels, 'A');
    down(levels, 'D');
    check(play(levels).stick_x == 1.0f, "D is stick right");
    down(levels, 'W');
    check(play(levels).stick_x == 1.0f && play(levels).stick_y == 1.0f, "W and D together are up and right");
    down(levels, 'A');
    check(play(levels).stick_x == 0.0f, "A and D cancel");
}

void the_other_letters_of_the_layout() {
    KeyboardMouseLevels levels;
    down(levels, 'Z');
    check(play(levels).held(GameInput::Z), "Z is Z");
    up(levels, 'Z');
    down(levels, 'C');
    check(play(levels).held(GameInput::C), "C is C");
    up(levels, 'C');
    down(levels, 'Q');
    check(play(levels).held(GameInput::Shake), "Q shakes");
    up(levels, 'Q');
    down(levels, 'L');
    check(play(levels).pointer_x == 1.0f, "L moves the pointer right");
    up(levels, 'L');
    down(levels, 'R');
    check(play(levels).held(GameInput::PointerRecenter), "R recentres the pointer");
}

void the_keys_that_never_failed() {
    KeyboardMouseLevels levels;
    down(levels, keys::SPACE);
    check(play(levels).held(GameInput::A), "Space is A");
    up(levels, keys::SPACE);
    down(levels, keys::LCONTROL);
    check(play(levels).held(GameInput::Z) && levels.key(keys::CONTROL), "left Control is Z and the generic Control key follows");
    up(levels, keys::LCONTROL);
    check(!play(levels).held(GameInput::Z) && !levels.key(keys::CONTROL), "releasing it releases both");
    down(levels, keys::RSHIFT);
    check(play(levels).held(GameInput::B) && levels.key(keys::SHIFT), "right Shift is B and the generic Shift key follows");
}

void the_old_event_moved_nothing() {
    // What the SDL host did before: the key's own level and nothing else.
    KeyboardMouseLevels levels;
    levels.set_key('W', true);
    levels.set_key(keys::SPACE, true);
    check(play(levels).stick_y == 0.0f && play(levels).held(GameInput::A),
          "the key's own level alone: Space works and W does nothing, as the owner saw");
}

void a_non_letter_sets_no_place() {
    KeyboardMouseLevels levels;
    down(levels, '5');
    down(levels, keys::SPACE);
    check(levels.letter_positions == 0u, "a digit and Space are no letter's place");
    check(levels.key('5'), "the digit's key level is set");
    apply_physical_key_event(levels, 0, true);
    apply_physical_key_event(levels, 0x100, true);
    check(levels.letter_positions == 0u, "a key with no number changes nothing");
}

}  // namespace

int main() {
    try {
        movement();
        the_other_letters_of_the_layout();
        the_keys_that_never_failed();
        the_old_event_moved_nothing();
        a_non_letter_sets_no_place();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "key event tests passed\n";
    return 0;
}
