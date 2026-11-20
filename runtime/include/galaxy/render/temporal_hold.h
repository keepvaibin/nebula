// SPDX-License-Identifier: GPL-3.0-only
// The temporal upscaler on generated frames, held off while the GPU cannot afford it (RenderCore's governor decides).
// A hold is tried again after a while: 30 s, doubling with each hold that follows to 8 min at most, so a slow stretch
// (a stage that loads, a busy GPU) does not end it for the session, and a GPU that truly cannot afford it is not
// asked every few seconds. A change of the settings it was decided at decides again at once, the first wait again.
#pragma once

#include <algorithm>
#include <cstdint>

namespace galaxy::render {

class TemporalInterpHold {
public:
    static constexpr std::int64_t kFirstNs = 30'000'000'000;
    static constexpr unsigned kMostDoublings = 4u;  // 30 s << 4 = 8 min

    [[nodiscard]] bool held() const noexcept { return held_; }
    // How long this hold lasts, in nanoseconds (held only).
    [[nodiscard]] std::int64_t wait_ns() const noexcept { return held_ ? until_ns_ - since_ns_ : 0; }

    void hold(std::int64_t now_ns) noexcept {
        if (held_) {
            return;
        }
        held_ = true;
        since_ns_ = now_ns;
        until_ns_ = now_ns + (kFirstNs << std::min(holds_, kMostDoublings));
        ++holds_;
    }
    // True when the hold ends now: its wait passed. The caller measures the generated frames again.
    [[nodiscard]] bool lift_if_due(std::int64_t now_ns) noexcept {
        if (!held_ || now_ns < until_ns_) {
            return false;
        }
        held_ = false;
        return true;
    }
    void settings_changed() noexcept {
        held_ = false;
        holds_ = 0u;
    }
    // At a frame's start (the four renderers' one check): while held, a change of the settings it was decided at
    // decides again (settings_changed); else true when its wait passed (lift_if_due). `changed` is asked only while
    // held.
    template <class Changed>
    [[nodiscard]] bool lift_at_frame_start(Changed&& changed, std::int64_t now_ns) {
        if (!held_) {
            return false;
        }
        if (changed()) {
            settings_changed();
            return false;
        }
        return lift_if_due(now_ns);
    }

private:
    bool held_ = false;
    unsigned holds_ = 0u;  // holds since the settings were last decided
    std::int64_t since_ns_ = 0;
    std::int64_t until_ns_ = 0;
};

}  // namespace galaxy::render
