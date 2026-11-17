// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "galaxy/render/host_window.h"

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace galaxy::render {

// Whether the game window can be seen, for a renderer that acquires its images itself (Vulkan, Metal): while it
// cannot, the renderer acquires and presents nothing, as Direct3D does once Present says DXGI_STATUS_OCCLUDED. A
// window the renderer has not shown yet is unseen only in a hidden start (it is shown after its first present); a
// shown one is asked (host_window::window_unseen) at most every 100 ms. Each change is one line in the log.
class UnseenWindow {
public:
    [[nodiscard]] bool now(host_window::NativeWindow window, bool shown) noexcept {
        if (!shown) {
            return set(host_window::hidden_window_mode_enabled());
        }
        const std::int64_t at = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
        if (at >= next_ns_) {
            next_ns_ = at + kProbeNs;
            return set(host_window::window_unseen(window));
        }
        return unseen_;
    }

private:
    static constexpr std::int64_t kProbeNs = 100'000'000;

    bool set(bool unseen) noexcept {
        if (unseen != unseen_ || !known_) {
            if (known_ || unseen) {
                std::fprintf(stderr, "[render] the window %s\n",
                             unseen ? "cannot be seen: nothing is presented until it can" : "can be seen again");
            }
            known_ = true;
            unseen_ = unseen;
        }
        return unseen_;
    }

    std::int64_t next_ns_ = 0;
    bool unseen_ = false;
    bool known_ = false;
};

}  // namespace galaxy::render
