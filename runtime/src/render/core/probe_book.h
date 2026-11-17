// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The screen probes' answers by the game frame that asked them (render_core.cpp): the game reads, at frame F, the
// answers its list of frame F - kDelay asked, whatever the GPU's speed, so a fixed-step run gets the same answers at the
// same step every time (the sun's glare, a talk balloon's depth). Before, the game read whichever answers came back last.
//
// A frame whose list asks probes is expected when the list is queued. It is settled when its list's real walk is over
// and every flight it asked has its answers, or when its list is released without a real walk (no answers). The game's
// read waits (RenderCore::screen_probe_results) until every expected frame up to F - kDelay is settled. Not thread-safe:
// the render core holds its lock around every call.

#include <nebula_frame_list.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

namespace galaxy::render::core {

class ProbeBook {
public:
    // Two frames: the game's frame F is made while the render thread draws F - 1, and F - 2 is on the GPU or done. The
    // console's GXPeekZ answered the frame before; one more frame keeps the CPU and GPU working side by side.
    static constexpr std::uint32_t kDelay = 2u;
    // Answers kept per channel: enough for the newest at or before F - kDelay while later frames come back.
    static constexpr std::size_t kKept = 8u;

    // A list of `frame` that asks probes is queued. Frame numbers go up; one going down (a new game) starts again.
    void expect(std::uint32_t frame) {
        if (newest_ != kNone && frame == newest_ && !pending_.empty() && pending_.back().frame == frame) {
            pending_.back().walked = false;  // a second list of the same frame: settled with the later one
            return;
        }
        if (newest_ != kNone && frame < newest_) {
            start_again();
        }
        pending_.push_back(Pending{frame, 0u, false});
        newest_ = frame;
    }

    // The real walk of `frame`'s list asked one flight of probes.
    void flight(std::uint32_t frame) {
        if (Pending* p = find(frame)) ++p->flights;
    }

    // The real walk of `frame`'s list is over: it asks no more.
    void walked(std::uint32_t frame) {
        if (Pending* p = find(frame)) p->walked = true;
        prune();
    }

    // One flight of `frame` came back with these answers for `channel`.
    void answered(std::uint32_t frame, std::uint32_t channel, const NebulaScreenProbe* probes, std::uint32_t count) {
        if (Pending* p = find(frame); p != nullptr && p->flights != 0u) --p->flights;
        Channel& c = channel_(channel);
        Answer* a = nullptr;
        for (Answer& have : c.answers) {
            if (have.frame == frame) a = &have;
        }
        if (a == nullptr) {
            c.answers.push_back(Answer{frame, {}});
            std::sort(c.answers.begin(), c.answers.end(), [](const Answer& x, const Answer& y) { return x.frame < y.frame; });
            for (Answer& have : c.answers) {
                if (have.frame == frame) a = &have;
            }
            while (c.answers.size() > kKept) c.answers.pop_front();
        }
        if (a != nullptr) a->probes.assign(probes, probes + count);  // the frame's last flight on the channel
        prune();
    }

    // A flight of `frame` will not come back (the device's state was dropped): as if answered with nothing.
    void lost(std::uint32_t frame) {
        if (Pending* p = find(frame); p != nullptr && p->flights != 0u) --p->flights;
        prune();
    }

    // `frame`'s list is released; unwalked, it asks nothing and is settled without answers.
    void released(std::uint32_t frame) {
        if (Pending* p = find(frame)) p->walked = true;
        prune();
    }

    // Nothing more will come back (the device's state is dropped): every expected frame is settled as it stands.
    void settle_all() { pending_.clear(); }

    // Every expected frame up to `asked` - kDelay is settled.
    [[nodiscard]] bool ready(std::uint32_t asked) const {
        if (asked < kDelay) return true;
        const std::uint32_t target = asked - kDelay;
        return std::none_of(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.frame <= target; });
    }

    // The answers on `channel` of the newest frame at or before `asked` - kDelay; their count (0 when none).
    std::uint32_t answers(std::uint32_t channel, std::uint32_t asked, NebulaScreenProbe* out, std::uint32_t capacity) const {
        if (asked < kDelay) return 0u;
        const std::uint32_t target = asked - kDelay;
        for (const Channel& c : channels_) {
            if (c.id != channel) continue;
            for (auto it = c.answers.rbegin(); it != c.answers.rend(); ++it) {
                if (it->frame <= target) {
                    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(it->probes.size(), capacity));
                    if (count != 0u) std::memcpy(out, it->probes.data(), count * sizeof(NebulaScreenProbe));
                    return count;
                }
            }
        }
        return 0u;
    }

    // The oldest expected frame not yet settled (for a log line when a read waits too long).
    [[nodiscard]] std::uint32_t oldest_pending() const { return pending_.empty() ? kNone : pending_.front().frame; }

    static constexpr std::uint32_t kNone = ~std::uint32_t{0};

private:
    struct Pending {
        std::uint32_t frame;
        std::uint32_t flights;
        bool walked;
    };
    struct Answer {
        std::uint32_t frame;
        std::vector<NebulaScreenProbe> probes;
    };
    struct Channel {
        std::uint32_t id;
        std::deque<Answer> answers;
    };

    Pending* find(std::uint32_t frame) {
        for (Pending& p : pending_) {
            if (p.frame == frame) return &p;
        }
        return nullptr;
    }
    Channel& channel_(std::uint32_t id) {
        for (Channel& c : channels_) {
            if (c.id == id) return c;
        }
        channels_.push_back(Channel{id, {}});
        return channels_.back();
    }
    // Settled frames leave, wherever they stand: a later frame may settle first.
    void prune() {
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [](const Pending& p) { return p.walked && p.flights == 0u; }),
                       pending_.end());
    }
    void start_again() {
        pending_.clear();
        channels_.clear();
    }

    std::deque<Pending> pending_;
    std::vector<Channel> channels_;
    std::uint32_t newest_ = kNone;
};

}  // namespace galaxy::render::core
