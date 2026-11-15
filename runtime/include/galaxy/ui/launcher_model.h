// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <mutex>

namespace galaxy::ui {

enum class LauncherAction { Previous, Next, Accept, Back, Ignore };

// The home screen of launcher mode: Play, Settings, Benchmark and Quit, and over them the update notice's page while one
// is offered. The window, game and renderer share this state. Mutations hold mutex.
struct LauncherModel {
    // In the game's order too (cLauncherRows, NebulaSettingsMenu.cpp).
    enum Row : unsigned { RowPlay, RowSettings, RowBenchmark, RowQuit, RowNum };
    static constexpr int kChoicePlay = 0;
    static constexpr int kChoiceQuit = 1;
    static constexpr int kChoiceBenchmark = 2;  // the host runs the benchmark in the game (bench::Options::here)
    static constexpr int kChoiceUpdate = 3;     // the notice's Update: the game leaves as for Quit; the host's Setup updates
    // view(): the selected answer in bits 0-7, the page in bits 8-15, kLauncherView (the HOME Menu's views never have
    // it) and kNoBenchmark where the host cannot start the benchmark (no restart: iOS, Android's own launcher): the rows
    // are then Play, Settings, Quit. Pages: 0 the rows, 1 the notice's. The notice's page has kNoticeInstall where this
    // install updates itself (its answers Update, Later), else its one answer Back.
    static constexpr unsigned kNoticePage = 1u << 8;
    static constexpr unsigned kLauncherView = 1u << 16;
    static constexpr unsigned kNoBenchmark = 1u << 17;
    static constexpr unsigned kNoticeInstall = 1u << 18;
    // The notice's answers where the install updates itself, in its order; Update is the one selected first.
    enum Notice : unsigned { NoticeUpdate, NoticeLater, NoticeNum };

    // What an answer asks beyond the model, done by the caller with the lock released.
    enum class Effect { None, OpenSettings };

    std::mutex mutex;
    std::atomic<bool> visible{false};
    std::atomic<int> choice{-1};
    unsigned row = RowPlay;  // the selected answer's place among the rows shown
    bool notice_page = false;
    bool notice_held = false;
    bool installs = false;  // the notice offers Update (set with the offer)
    unsigned notice_answer = NoticeUpdate;
    bool notice_only = false;  // the notice shows alone (open_notice): leaving it goes on with the game
    bool benchmark = true;     // set before the screen opens

    void open() {
        const std::lock_guard<std::mutex> lock(mutex);
        row = RowPlay;
        notice_only = false;
        notice_page = notice_held;
        notice_answer = NoticeUpdate;
        choice.store(-1, std::memory_order_relaxed);
        visible.store(true, std::memory_order_release);
    }

    // The offered update's notice alone, over the game (the title's press): Update leaves as on the home screen; Later
    // (Back where the install does not update itself) closes it as Play does. False with no offer or while it shows.
    bool open_notice() {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!notice_held || visible.load(std::memory_order_relaxed)) return false;
        notice_only = true;
        notice_page = true;
        notice_answer = NoticeUpdate;
        choice.store(-1, std::memory_order_relaxed);
        visible.store(true, std::memory_order_release);
        return true;
    }

    int poll() const { return choice.load(std::memory_order_acquire); }

    // An update was found: its page shows over the rows (now, when they show).
    void offer_notice(bool can_install) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!notice_held) {
            installs = can_install;
            notice_answer = NoticeUpdate;
            notice_held = true;
            notice_page = true;
        }
    }

    int view() {
        if (!visible.load(std::memory_order_acquire)) return -1;
        const std::lock_guard<std::mutex> lock(mutex);
        const unsigned shown = notice_page ? kNoticePage | (installs ? kNoticeInstall | notice_answer : 0u) : row;
        return static_cast<int>(shown | kLauncherView | (benchmark ? 0u : kNoBenchmark));
    }

    void point(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed) || answer >= answers()) return;
        if (choosing()) {
            notice_answer = answer;
        } else if (!paged()) {
            row = answer;
        }
    }

    Effect click(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed) || answer >= answers()) {
            return Effect::None;
        }
        if (choosing()) {
            notice_answer = answer;
        } else if (!paged()) {
            row = answer;
        }
        return accept();
    }

    Effect key(LauncherAction action, bool was_down) {
        if (was_down && action != LauncherAction::Previous && action != LauncherAction::Next) return Effect::None;
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed)) return Effect::None;
        switch (action) {
        case LauncherAction::Previous:
            if (choosing()) {
                notice_answer = (notice_answer + NoticeNum - 1u) % NoticeNum;
            } else if (!paged()) {
                row = (row + rows() - 1u) % rows();
            }
            return Effect::None;
        case LauncherAction::Next:
            if (choosing()) {
                notice_answer = (notice_answer + 1u) % NoticeNum;
            } else if (!paged()) {
                row = (row + 1u) % rows();
            }
            return Effect::None;
        case LauncherAction::Accept: return accept();
        case LauncherAction::Back: return leave_page();  // the rows have nothing to go back to
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
    bool paged() const { return notice_page; }
    // The notice's page with its two answers.
    bool choosing() const { return notice_page && installs; }
    unsigned answers() const { return choosing() ? unsigned{NoticeNum} : paged() ? 1u : rows(); }

    // Back (and the page's one answer): the notice's page goes to the rows, or, shown alone, the game goes on.
    Effect leave_page() {
        notice_page = false;
        if (notice_only) close(kChoicePlay);
        return Effect::None;
    }

    Effect accept() {
        if (choosing() && notice_answer == NoticeUpdate) {
            close(kChoiceUpdate);
            return Effect::None;
        }
        if (paged()) {
            return leave_page();
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
