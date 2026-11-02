// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <mutex>

namespace galaxy::ui {

enum class HomeAction { Previous, Next, Accept, Back, Close, Ignore };

// The window, game and renderer share this state. Mutations hold mutex.
struct HomeMenuModel {
    static constexpr unsigned rows = 3;
    static constexpr unsigned answers = 2;
    static constexpr std::array<int, rows> row_choices{0, 2, 1};
    std::mutex mutex;
    std::atomic<bool> visible{false};
    std::atomic<unsigned> row{0};
    std::atomic<int> asking{0};
    std::atomic<int> choice{-1};
    std::atomic<bool> drawn_by_game{false};

    void open() {
        const std::lock_guard<std::mutex> lock(mutex);
        row.store(0u, std::memory_order_relaxed);
        asking.store(0, std::memory_order_relaxed);
        choice.store(-1, std::memory_order_relaxed);
        visible.store(true, std::memory_order_release);
    }

    int poll() const { return choice.load(std::memory_order_acquire); }
    void hide() { visible.store(false, std::memory_order_release); }

    int view(bool drawing) {
        if (!visible.load(std::memory_order_acquire)) return -1;
        if (drawing) drawn_by_game.store(true, std::memory_order_relaxed);
        const std::lock_guard<std::mutex> lock(mutex);
        const unsigned selected = row.load(std::memory_order_relaxed) % answer_count();
        return static_cast<int>(selected | static_cast<unsigned>(asking.load(std::memory_order_relaxed)) << 8u);
    }

    void point(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (visible.load(std::memory_order_relaxed) && answer < answer_count()) {
            row.store(answer, std::memory_order_relaxed);
        }
    }

    void click(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (visible.load(std::memory_order_relaxed) && answer < answer_count()) {
            row.store(answer, std::memory_order_relaxed);
            choose(answer);
        }
    }

    unsigned answer_count() const {
        return asking.load(std::memory_order_relaxed) != 0 ? answers : rows;
    }

    void close(int result) {
        choice.store(result, std::memory_order_release);
        visible.store(false, std::memory_order_release);
    }

    void back() {
        const int question = asking.load(std::memory_order_relaxed);
        if (question == 0) {
            close(0);
            return;
        }
        asking.store(0, std::memory_order_relaxed);
        for (unsigned i = 0; i < rows; ++i) {
            if (row_choices[i] == question) row.store(i, std::memory_order_relaxed);
        }
    }

    void choose(unsigned answer) {
        const int question = asking.load(std::memory_order_relaxed);
        if (question != 0) {
            if (answer == 1u) close(question);
            else back();
            return;
        }
        const int result = row_choices[answer % rows];
        if (result == 0) {
            close(0);
            return;
        }
        asking.store(result, std::memory_order_relaxed);
        row.store(0u, std::memory_order_relaxed);
    }

    void key(HomeAction action, bool was_down) {
        if (was_down && action != HomeAction::Previous && action != HomeAction::Next) return;
        const std::lock_guard<std::mutex> lock(mutex);
        const unsigned count = answer_count();
        const unsigned current = row.load(std::memory_order_relaxed) % count;
        switch (action) {
        case HomeAction::Previous: row.store((current + count - 1u) % count, std::memory_order_relaxed); break;
        case HomeAction::Next: row.store((current + 1u) % count, std::memory_order_relaxed); break;
        case HomeAction::Accept: choose(current); break;
        case HomeAction::Back: back(); break;
        case HomeAction::Close: close(0); break;
        case HomeAction::Ignore: break;
        }
    }
};

}  // namespace galaxy::ui
