// SPDX-License-Identifier: GPL-3.0-only
// Variable refresh (I-007, docs/plan/native/05-NATIVE-LANE-G.md G2.3.5): told by the system where it says so, else
// proved from the presents. A variable display shows each picture for the presents' own interval inside its range; a
// fixed one shows each for whole refreshes of its mode. Pure arithmetic, under frame_schedule_tests.cpp; the renderers
// feed it (DXGI's frame statistics, Vulkan's present timing and present-completion stamps, the hosts' system answers).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace galaxy::render {

enum class RefreshKind : std::uint8_t { kUnknown, kFixed, kVariable };

// Vulkan present timing (VK_EXT_present_timing, VkSwapchainTimingPropertiesEXT): `refreshInterval` UINT64_MAX is a
// variable display, equal to `refreshDuration` a fixed one, 0 (or anything else) not known: the presents decide.
[[nodiscard]] constexpr RefreshKind vulkan_refresh_kind(std::uint64_t refresh_interval,
                                                        std::uint64_t refresh_duration) noexcept {
    if (refresh_interval == std::numeric_limits<std::uint64_t>::max()) return RefreshKind::kVariable;
    if (refresh_interval != 0u && refresh_interval == refresh_duration) return RefreshKind::kFixed;
    return RefreshKind::kUnknown;
}

// Android: `Display.hasArrSupport()` (API 36) as it answers; below API 36 it is not asked and the display is fixed at its
// current mode (a mode switch is a display change). Pictures land on the vsync grid either way.
[[nodiscard]] constexpr RefreshKind android_refresh_kind(int sdk, bool has_arr) noexcept {
    return sdk >= 36 && has_arr ? RefreshKind::kVariable : RefreshKind::kFixed;
}

// macOS (NSScreen's `minimumRefreshInterval` and `maximumRefreshInterval`, macOS 12): a display with a range has a
// longest interval longer than its shortest. Neither read (0): not known.
[[nodiscard]] constexpr RefreshKind mac_refresh_kind(double shortest_s, double longest_s) noexcept {
    if (shortest_s <= 0.0 || longest_s <= 0.0) return RefreshKind::kUnknown;
    return longest_s > shortest_s * 1.001 ? RefreshKind::kVariable : RefreshKind::kFixed;
}

// The answer the renderer acts on: fixed where a present cannot be shown at once (Windows with no tearing support or
// a window the system composes: `can_vary` false); else the system's where it says; else the presents' proof. Not
// known is taken as fixed.
[[nodiscard]] constexpr bool refresh_varies(bool can_vary, RefreshKind system, RefreshKind proved) noexcept {
    if (!can_vary) return false;
    return (system != RefreshKind::kUnknown ? system : proved) == RefreshKind::kVariable;
}

// The cap (point 2): the pictures' rate is held so that no two presents come closer than the display's shortest
// interval: one over (that interval + the margin). The margin is the precise wait's lateness at its 99.9th percentile
// (PreciseWait's WaitLateness), 1/3600 s until the first measure is in (a recorded start value, not a rule).
inline constexpr double kStartMarginSeconds = 1.0 / 3600.0;
[[nodiscard]] constexpr double variable_refresh_cap_hz(double shortest_s, double margin_s) noexcept {
    return shortest_s > 0.0 ? 1.0 / (shortest_s + (margin_s > 0.0 ? margin_s : 0.0)) : 0.0;
}

// The presents' proof (point 5, Windows and Linux): presents held at the probe rate (`probe_period`, 1.25 mode periods:
// between the mode's rate and half of it, never a whole number of refreshes), each with how long it stayed up. A fixed
// display shows each for whole mode periods (1 and 2 refreshes at the probe), a variable one for the presents' own
// interval. A present counts only when the pacer held it between one and two mode periods, its spacing within 5 % of
// that, and its time up says one of the two and not both (at the mode's own rate they are the same). Over 120 counted:
// 90 % whole is fixed, 90 % the presents' own is variable, else another 120. Presents below the probe rate never count,
// so a machine too slow to hold it gives no new answer. With an answer held, a window that says the other asks again.
class RefreshProof {
public:
    static constexpr int kCounted = 120;

    // A new question (a display change, full screen, focus, another display, the mode): `mode_period` in the clock's
    // ticks (0: not known, nothing is proved).
    void restart(std::int64_t mode_period) noexcept {
        *this = RefreshProof{};
        mode_ = mode_period > 0 ? mode_period : 0;
    }
    [[nodiscard]] RefreshKind answer() const noexcept { return answer_; }
    [[nodiscard]] std::int64_t mode_period() const noexcept { return mode_; }
    // The pacer holds presents at the probe's period while no answer is held.
    [[nodiscard]] bool probing() const noexcept { return mode_ > 0 && answer_ == RefreshKind::kUnknown; }
    [[nodiscard]] std::int64_t probe_period() const noexcept { return mode_ + mode_ / 4; }

    // A present the pacer held to `held`, the next present `spacing` after it, and the time from its being shown to the
    // next one's (`shown`; ticks).
    void note(std::int64_t held, std::int64_t spacing, std::int64_t shown) noexcept {
        if (mode_ <= 0 || held < mode_ || held > 2 * mode_ || shown <= 0 || spacing <= 0) return;
        if (std::llabs(spacing - held) * 20 > held) return;
        const std::int64_t refreshes = (shown + mode_ / 2) / mode_;
        const bool whole = refreshes >= 1 && std::llabs(shown - refreshes * mode_) * 10 <= mode_;
        const bool own = std::llabs(shown - spacing) * 20 <= spacing;
        if (whole == own) return;
        ++counted_;
        (whole ? whole_ : own_) += 1;
        if (counted_ < kCounted) return;
        const RefreshKind window = whole_ * 10 >= counted_ * 9 ? RefreshKind::kFixed
                                   : own_ * 10 >= counted_ * 9 ? RefreshKind::kVariable
                                                               : RefreshKind::kUnknown;
        counted_ = whole_ = own_ = 0;
        if (answer_ == RefreshKind::kUnknown) {
            answer_ = window;
        } else if (window != RefreshKind::kUnknown && window != answer_) {
            answer_ = RefreshKind::kUnknown;  // (e): the statistics no longer fit the answer held
        }
    }

private:
    std::int64_t mode_ = 0;
    RefreshKind answer_ = RefreshKind::kUnknown;
    int counted_ = 0;
    int whole_ = 0;
    int own_ = 0;
};

// Each present's call and the period the pacer held it to, until the display says when it and the next were shown
// (DXGI's frame statistics, Vulkan's present-completion stamps): then the proof hears of the earlier one.
class ShownIntervals {
public:
    void presented(std::uint64_t id, std::int64_t call, std::int64_t held) noexcept {
        ring_[id % kRing] = Entry{id, call, held, 0};
    }
    // Present `id` was shown at `at`.
    void shown(std::uint64_t id, std::int64_t at, RefreshProof& proof) noexcept {
        Entry& entry = ring_[id % kRing];
        if (id == 0u || entry.id != id || at <= 0) return;
        entry.shown = at;
        const Entry& before = ring_[(id - 1u) % kRing];
        if (before.id != id - 1u || before.shown == 0) return;
        proof.note(before.held, entry.call - before.call, at - before.shown);
    }
    void clear() noexcept { ring_ = {}; }

private:
    struct Entry {
        std::uint64_t id = 0;
        std::int64_t call = 0;
        std::int64_t held = 0;
        std::int64_t shown = 0;
    };
    static constexpr std::size_t kRing = 64;
    std::array<Entry, kRing> ring_{};
};

// One renderer's question, asked again at each of point 5's events (`ask`): what the system says (`told`), the presents'
// proof and the intervals that feed it. The renderers act on `varies`, hold presents at `probe_hz` while `probing`, and
// under variable refresh hold them under `cap_hz`.
class RefreshQuestion {
public:
    // The presents a probe runs for at most (about 5 s at the probe rate): a window whose presents the display never
    // reports (hidden, or a remote session's) stops holding them, with no answer (fixed) until it is asked again.
    static constexpr int kProbePresents = 600;

    // A new question: the mode's period in the clock's ticks (0: not known).
    void ask(std::int64_t mode_period) noexcept {
        proof_.restart(mode_period);
        intervals_.clear();
        presents_ = 0;
    }
    // The system's answer (kUnknown: it gives none) and the display's shortest interval as it gives it (seconds; 0: it
    // gives none, the mode's period stands).
    void told(RefreshKind kind, double shortest_s) noexcept {
        system_ = kind;
        shortest_s_ = shortest_s > 0.0 ? shortest_s : 0.0;
    }
    [[nodiscard]] RefreshKind system() const noexcept { return system_; }
    [[nodiscard]] RefreshKind proved() const noexcept { return proof_.answer(); }
    [[nodiscard]] bool varies(bool can_vary) const noexcept { return refresh_varies(can_vary, system_, proof_.answer()); }
    // Presents are held at the probe rate: the presents decide (a present can be shown at once, the system says nothing,
    // no answer held) and each present waits for a refresh (VSync on; VSync off shows every present at once on either
    // kind of display, which proves nothing).
    [[nodiscard]] bool probing(bool can_vary, bool vsync) const noexcept {
        return can_vary && vsync && system_ == RefreshKind::kUnknown && proof_.probing() && presents_ < kProbePresents;
    }
    [[nodiscard]] double probe_hz(std::int64_t frequency) const noexcept {
        const std::int64_t period = proof_.probe_period();
        return period > 0 ? static_cast<double>(frequency) / static_cast<double>(period) : 0.0;
    }
    // Point 2's cap for a mode of `mode_hz`: the system's shortest interval where it gives one, else the mode's period.
    [[nodiscard]] double cap_hz(double mode_hz, double margin_s) const noexcept {
        const double shortest = shortest_s_ > 0.0 ? shortest_s_ : mode_hz > 1.0 ? 1.0 / mode_hz : 0.0;
        return variable_refresh_cap_hz(shortest, margin_s);
    }
    // A present (`id`, its call, the period it was held to: 0 when none), and when the display showed one.
    void presented(std::uint64_t id, std::int64_t call, std::int64_t held) noexcept {
        intervals_.presented(id, call, held);
        presents_ += presents_ < kProbePresents ? 1 : 0;
    }
    // Statistics that no longer fit the answer held (point 5 (e)) start a probe of its own length.
    void shown(std::uint64_t id, std::int64_t at) noexcept {
        const RefreshKind held = proof_.answer();
        intervals_.shown(id, at, proof_);
        if (held != RefreshKind::kUnknown && proof_.answer() == RefreshKind::kUnknown) {
            presents_ = 0;
        }
    }

private:
    RefreshProof proof_;
    ShownIntervals intervals_;
    RefreshKind system_ = RefreshKind::kUnknown;
    double shortest_s_ = 0.0;
    int presents_ = 0;  // presents since the question was asked, to kProbePresents
};

}  // namespace galaxy::render
