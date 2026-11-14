// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <mutex>

namespace galaxy::ui {

enum class LauncherAction { Previous, Next, Accept, Back, Ignore };

// The home screen of launcher mode: Play, Settings, Benchmark and Quit, and the update notice's page over them while
// one is offered. The window, game and renderer share this state. Mutations hold mutex.
struct LauncherModel {
    // In the game's order too (cLauncherRows, NebulaSettingsMenu.cpp).
    enum Row : unsigned { RowPlay, RowSettings, RowBenchmark, RowQuit, RowNum };
    static constexpr int kChoicePlay = 0;
    static constexpr int kChoiceQuit = 1;
    static constexpr int kChoiceBenchmark = 2;  // the game leaves as for Quit; the host starts again into the benchmark
    // view(): the selected answer in bits 0-7, the page in bits 8-15 (0 the rows, 1 the notice's), kLauncherView (the
    // HOME Menu's views never have it) and kNoBenchmark where the host cannot start the benchmark (no restart: iOS,
    // Android's own launcher): the rows are then Play, Settings, Quit.
    static constexpr unsigned kNoticePage = 1u << 8;
    static constexpr unsigned kLauncherView = 1u << 16;
    static constexpr unsigned kNoBenchmark = 1u << 17;

    // What an answer asks beyond the model, done by the caller with the lock released.
    enum class Effect { None, OpenSettings };

    std::mutex mutex;
    std::atomic<bool> visible{false};
    std::atomic<int> choice{-1};
    unsigned row = RowPlay;  // the selected answer's place among the rows shown
    bool notice_page = false;
    bool notice_held = false;
    bool benchmark = true;  // set before the screen opens

    void open() {
        const std::lock_guard<std::mutex> lock(mutex);
        row = RowPlay;
        notice_page = notice_held;
        choice.store(-1, std::memory_order_relaxed);
        visible.store(true, std::memory_order_release);
    }

    int poll() const { return choice.load(std::memory_order_acquire); }

    // An update was found: its page shows over the rows (now, when they show).
    void offer_notice() {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!notice_held) {
            notice_held = true;
            notice_page = true;
        }
    }

    int view() {
        if (!visible.load(std::memory_order_acquire)) return -1;
        const std::lock_guard<std::mutex> lock(mutex);
        return static_cast<int>((notice_page ? kNoticePage : row) | kLauncherView | (benchmark ? 0u : kNoBenchmark));
    }

    void point(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (visible.load(std::memory_order_relaxed) && !notice_page && answer < rows()) row = answer;
    }

    Effect click(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed) || answer >= (notice_page ? 1u : rows())) {
            return Effect::None;
        }
        if (!notice_page) row = answer;
        return accept();
    }

    Effect key(LauncherAction action, bool was_down) {
        if (was_down && action != LauncherAction::Previous && action != LauncherAction::Next) return Effect::None;
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed)) return Effect::None;
        switch (action) {
        case LauncherAction::Previous:
            if (!notice_page) row = (row + rows() - 1u) % rows();
            return Effect::None;
        case LauncherAction::Next:
            if (!notice_page) row = (row + 1u) % rows();
            return Effect::None;
        case LauncherAction::Accept: return accept();
        case LauncherAction::Back: notice_page = false; return Effect::None;  // the rows have nothing to go back to
        case LauncherAction::Ignore: return Effect::None;
        }
        return Effect::None;
    }

    // The rows shown, and the row at a place among them.
    unsigned rows() const { return benchmark ? unsigned{RowNum} : unsigned{RowNum} - 1u; }
    Row row_at(unsigned place) const {
        return static_cast<Row>(!benchmark && place >= RowBenchmark ? place + 1u : place);
    }

private:
    Effect accept() {
        if (notice_page) {
            notice_page = false;  // its one answer, Back
            return Effect::None;
        }
        switch (row_at(row)) {
        case RowPlay: close(kChoicePlay); return Effect::None;
        case RowSettings: return Effect::OpenSettings;
        case RowBenchmark: close(kChoiceBenchmark); return Effect::None;
        default: close(kChoiceQuit); return Effect::None;
        }
    }

    void close(int result) {
        choice.store(result, std::memory_order_release);
        visible.store(false, std::memory_order_release);
    }
};

}  // namespace galaxy::ui
