// SPDX-License-Identifier: GPL-3.0-only
// What a start may not be silent about (host_main.cpp, the main loop's once-a-second look): a game thread that has made no
// frame for a while, and a window that has shown no picture for a while. The first goes to the log, the second also to the
// player (the window is made visible and a message says that the start has stopped and where).
#pragma once

#include <cstdint>

namespace galaxy {

class StartWatch {
public:
    // The game's first frame comes within about a second on a start that works; ten seconds with none is a stop. The log
    // says it then, again at 30 s and then each minute.
    static constexpr double kNoFrameS = 10.0;
    // The first picture comes within a few seconds (a first start shows its preparation bar); 20 s is long enough for a
    // slow disc and a cold shader cache, and short enough that a player is not left with nothing.
    static constexpr double kNoPictureS = 20.0;

    struct Notice {
        bool log_no_frame = false;      // say in the log that the game thread made no frame
        bool show_no_picture = false;   // show the window and tell the player (once)
        double no_frame_for_s = 0.0;
    };

    // `now_s`: seconds since the game thread started; `frames`: the game's frame count; `picture_shown`: the first picture
    // reached the window; `paused`: the game is held on purpose (minimised). Called about once a second.
    [[nodiscard]] Notice tick(double now_s, std::uint32_t frames, bool picture_shown, bool paused) noexcept {
        Notice notice;
        if (paused || frames != last_frames_ || now_s < last_progress_s_) {
            last_frames_ = frames;
            last_progress_s_ = now_s;
            next_log_after_s_ = kNoFrameS;
        }
        const double idle = now_s - last_progress_s_;
        if (idle >= next_log_after_s_) {
            notice.log_no_frame = true;
            notice.no_frame_for_s = idle;
            next_log_after_s_ = idle < 30.0 ? 30.0 : idle + 60.0;
        }
        if (!picture_shown && !paused && !told_ && now_s >= kNoPictureS) {
            told_ = true;
            notice.show_no_picture = true;
        }
        return notice;
    }

private:
    std::uint32_t last_frames_ = 0;
    double last_progress_s_ = 0.0;
    double next_log_after_s_ = kNoFrameS;
    bool told_ = false;
};

}  // namespace galaxy
