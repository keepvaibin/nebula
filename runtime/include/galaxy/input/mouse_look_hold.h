// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace galaxy::input {

// What the window's thread knows when it decides a mod's first-person look's mouse hold (input 1.4 look_mode).
struct MouseLookWindow {
    bool wanted = false;      // the game's last controller read asked for it (a look holds the controls)
    bool foreground = false;  // the window is the system's foreground window
    bool minimized = false;
    bool moving = false;      // the player is moving or sizing the window, or its system menu is open
};

// The mouse hold of a desktop window, decided on the window's own thread from its own messages (M-061): held only
// while the look wants it and the window is in front, shown and not being moved; given back at the message that ends
// any of those (minimised, another window in front, the window going away), never left for the game's next controller
// read, which a held game (minimised, a hang) or a quit never makes.
class MouseLookHold {
public:
    enum class Act {
        None,     // nothing held, nothing to give back
        Hold,     // hold (again): the system may have dropped the clip, or the window moved
        Release,  // give the mouse back now
    };
    Act update(const MouseLookWindow& window) noexcept {
        if (window.wanted && window.foreground && !window.minimized && !window.moving) {
            held_ = true;
            return Act::Hold;
        }
        return end();
    }
    // The window goes away: whatever is held is given back.
    Act end() noexcept {
        if (!held_) return Act::None;
        held_ = false;
        return Act::Release;
    }
    [[nodiscard]] bool held() const noexcept { return held_; }

private:
    bool held_ = false;
};

// How an SDL window holds the mouse for a look (M-062): SDL's relative mode where the system gives it; where it
// refuses (a compositor or X server without relative pointer support, a remote session), the window grabs the mouse,
// hides the cursor and the look reads the plain motion's own travel instead of none. Asked once each time a hold
// starts, never again at every pump while it is refused.
class SdlMouseLookHold {
public:
    enum class Mode { None, Relative, Grab };
    // `relative(on)` asks SDL's relative mode and says whether it was given; `grab(on)` holds or gives back by grab.
    template <class Relative, class Grab>
    Mode update(bool hold, Relative&& relative, Grab&& grab) {
        if (hold == (mode_ != Mode::None)) return mode_;
        if (hold) {
            if (relative(true)) {
                mode_ = Mode::Relative;
            } else {
                grab(true);
                mode_ = Mode::Grab;
            }
        } else {
            if (mode_ == Mode::Relative) (void)relative(false);
            else grab(false);
            mode_ = Mode::None;
        }
        return mode_;
    }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

private:
    Mode mode_ = Mode::None;
};

}  // namespace galaxy::input
