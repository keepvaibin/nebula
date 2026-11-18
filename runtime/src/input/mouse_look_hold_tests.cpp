// SPDX-License-Identifier: GPL-3.0-only
// A mod's look's mouse hold, as the window's thread decides it (M-061): given back at the window's own message that
// ends it (minimised, another window in front, moved, gone), with no controller read after it.
#include "galaxy/input/mouse_look_hold.h"

#include <iostream>
#include <stdexcept>

namespace {
using galaxy::input::MouseLookHold;
using galaxy::input::MouseLookWindow;
using Act = MouseLookHold::Act;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

MouseLookWindow front(bool wanted) {
    MouseLookWindow window;
    window.wanted = wanted;
    window.foreground = true;
    return window;
}

void held_while_wanted_in_front() {
    MouseLookHold hold;
    check(hold.update(front(false)) == Act::None && !hold.held(), "no look: nothing held, nothing given back");
    check(hold.update(front(true)) == Act::Hold && hold.held(), "a look in front holds the mouse");
    check(hold.update(front(true)) == Act::Hold, "and holds it again at each message (a clip Windows dropped)");
    check(hold.update(front(false)) == Act::Release && !hold.held(), "the look ends: given back");
    check(hold.update(front(false)) == Act::None, "given back once");
}

// The game's last read wanted the look and the game reads no more (minimised, so held; a hang; a quit): the window's
// messages give the mouse back by themselves.
void given_back_by_the_window_alone() {
    MouseLookHold hold;
    (void)hold.update(front(true));
    MouseLookWindow minimised = front(true);
    minimised.minimized = true;
    check(hold.update(minimised) == Act::Release, "minimised: given back with the look still wanted");
    check(hold.update(front(true)) == Act::Hold, "shown again with the look on: held again");

    MouseLookWindow behind = front(true);
    behind.foreground = false;
    check(hold.update(behind) == Act::Release, "another window in front: given back");
    (void)hold.update(front(true));

    MouseLookWindow moving = front(true);
    moving.moving = true;
    check(hold.update(moving) == Act::Release, "moved or sized by the player, or its menu open: given back");
    (void)hold.update(front(true));

    check(hold.end() == Act::Release && !hold.held(), "the window goes away: given back");
    check(hold.end() == Act::None, "nothing held after it");
}
// An SDL window where relative mode is refused (M-062): held by a grab instead of not at all, and relative mode asked
// once per hold, not again at every pump.
void sdl_grab_where_relative_is_refused() {
    using galaxy::input::SdlMouseLookHold;
    SdlMouseLookHold hold;
    int asked = 0;
    int grabs = 0;
    int releases = 0;
    const auto refused = [&](bool on) { if (on) ++asked; return false; };
    const auto grab = [&](bool on) { ++(on ? grabs : releases); };
    for (int pump = 0; pump < 10; ++pump) (void)hold.update(true, refused, grab);
    check(hold.mode() == SdlMouseLookHold::Mode::Grab, "relative mode refused: the mouse is held by a grab");
    check(asked == 1 && grabs == 1, "relative mode asked once, not at every pump");
    check(hold.update(false, refused, grab) == SdlMouseLookHold::Mode::None && releases == 1, "the grab given back");

    SdlMouseLookHold given;
    int relative_off = 0;
    const auto relative = [&](bool on) { if (!on) ++relative_off; return true; };
    check(given.update(true, relative, grab) == SdlMouseLookHold::Mode::Relative, "relative mode where it is given");
    (void)given.update(false, relative, grab);
    check(relative_off == 1 && grabs == 1, "relative mode turned off, no grab");
}
}  // namespace

int main() {
    try {
        held_while_wanted_in_front();
        given_back_by_the_window_alone();
        sdl_grab_where_relative_is_refused();
        std::cout << "mouse look hold: held while wanted in front, given back by the window's own messages\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
