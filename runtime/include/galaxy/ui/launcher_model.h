// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <mutex>

namespace galaxy::ui {

enum class LauncherAction { Previous, Next, Accept, Back, Ignore };

// The home screen of launcher mode: Play, Settings and Quit, and the update notice's page over them while one is
// offered. The window, game and renderer share this state. Mutations hold mutex.
struct LauncherModel {
    enum Row : unsigned { RowPlay, RowSettings, RowQuit, RowNum };
    static constexpr int kChoicePlay = 0;
    static constexpr int kChoiceQuit = 1;
    // view(): the selected answer in bits 0-7, the page in bits 8-15 (0 the rows, 1 the notice's) and kLauncherView
    // (the HOME Menu's views never have it).
    static constexpr unsigned kNoticePage = 1u << 8;
    static constexpr unsigned kLauncherView = 1u << 16;

    // What an answer asks beyond the model, done by the caller with the lock released.
    enum class Effect { None, OpenSettings };

    std::mutex mutex;
    std::atomic<bool> visible{false};
    std::atomic<int> choice{-1};
    unsigned row = RowPlay;
    bool notice_page = false;
    bool notice_held = false;

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
        return static_cast<int>((notice_page ? kNoticePage : row) | kLauncherView);
    }

    void point(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (visible.load(std::memory_order_relaxed) && !notice_page && answer < RowNum) row = answer;
    }

    Effect click(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed) || answer >= (notice_page ? 1u : unsigned{RowNum})) {
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
            if (!notice_page) row = (row + RowNum - 1u) % RowNum;
            return Effect::None;
        case LauncherAction::Next:
            if (!notice_page) row = (row + 1u) % RowNum;
            return Effect::None;
        case LauncherAction::Accept: return accept();
        case LauncherAction::Back: notice_page = false; return Effect::None;  // the rows have nothing to go back to
        case LauncherAction::Ignore: return Effect::None;
        }
        return Effect::None;
    }

private:
    Effect accept() {
        if (notice_page) {
            notice_page = false;  // its one answer, Back
            return Effect::None;
        }
        switch (row) {
        case RowPlay: close(kChoicePlay); return Effect::None;
        case RowSettings: return Effect::OpenSettings;
        default: close(kChoiceQuit); return Effect::None;
        }
    }

    void close(int result) {
        choice.store(result, std::memory_order_release);
        visible.store(false, std::memory_order_release);
    }
};

}  // namespace galaxy::ui
