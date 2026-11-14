// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <mutex>

namespace galaxy::ui {

enum class LauncherAction { Previous, Next, Accept, Back, Ignore };

// The home screen of launcher mode: Play, Settings, Benchmark and Quit; over them, in this order, a message's page (a
// benchmark that gave no recommendation, once), the benchmark report's upload question and its answer pages (once),
// and the update notice's page while one is offered. The window, game and renderer share this state. Mutations hold
// mutex.
struct LauncherModel {
    // In the game's order too (cLauncherRows, NebulaSettingsMenu.cpp).
    enum Row : unsigned { RowPlay, RowSettings, RowBenchmark, RowQuit, RowNum };
    static constexpr int kChoicePlay = 0;
    static constexpr int kChoiceQuit = 1;
    static constexpr int kChoiceBenchmark = 2;  // the game leaves as for Quit; the host starts again into the benchmark
    // view(): the selected answer in bits 0-7, the page in bits 8-15, kLauncherView (the HOME Menu's views never have
    // it) and kNoBenchmark where the host cannot start the benchmark (no restart: iOS, Android's own launcher): the rows
    // are then Play, Settings, Quit. Pages: 0 the rows, 1 the notice's, 2 the message's, 3 the upload question (its
    // answers Don't send, Show the full report, Send), 4 sending, 5 sent, 6 not sent (each over Back).
    static constexpr unsigned kNoticePage = 1u << 8;
    static constexpr unsigned kMessagePage = 2u << 8;
    static constexpr unsigned kUploadPage = 3u << 8;
    static constexpr unsigned kSendingPage = 4u << 8;
    static constexpr unsigned kSentPage = 5u << 8;
    static constexpr unsigned kNotSentPage = 6u << 8;
    static constexpr unsigned kLauncherView = 1u << 16;
    static constexpr unsigned kNoBenchmark = 1u << 17;
    // The upload question's answers, in its order; Don't send is the one selected first.
    enum Upload : unsigned { UploadNo, UploadShow, UploadSend, UploadNum };

    // What an answer asks beyond the model, done by the caller with the lock released.
    enum class Effect { None, OpenSettings, ShowReport, SendReport, UploadLeft };

    std::mutex mutex;
    std::atomic<bool> visible{false};
    std::atomic<int> choice{-1};
    unsigned row = RowPlay;  // the selected answer's place among the rows shown
    bool notice_page = false;
    bool notice_held = false;
    bool message_page = false;  // shown until its answer; then never again
    unsigned upload_page = 0u;  // 3-6 while the upload's pages show (once), else 0
    unsigned upload_answer = UploadNo;
    bool benchmark = true;  // set before the screen opens

    void open() {
        const std::lock_guard<std::mutex> lock(mutex);
        row = RowPlay;
        notice_page = notice_held;
        choice.store(-1, std::memory_order_relaxed);
        visible.store(true, std::memory_order_release);
    }

    int poll() const { return choice.load(std::memory_order_acquire); }

    // The benchmark this start returned from gave no recommendation: its page shows first (now, when the rows show).
    void offer_message() {
        const std::lock_guard<std::mutex> lock(mutex);
        message_page = true;
    }

    // The benchmark's result may be sent: its question shows after the message (now, when the rows show), Don't send
    // selected.
    void offer_upload() {
        const std::lock_guard<std::mutex> lock(mutex);
        upload_page = 3u;
        upload_answer = UploadNo;
    }

    // The send's answer (any thread): its page, when the player still waits on the sending page.
    void upload_finished(bool sent) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (upload_page == 4u) upload_page = sent ? 5u : 6u;
    }

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
        const unsigned shown = message_page        ? kMessagePage
                               : upload_page == 3u ? kUploadPage | upload_answer
                               : upload_page != 0u ? upload_page << 8
                               : notice_page       ? kNoticePage
                                                   : row;
        return static_cast<int>(shown | kLauncherView | (benchmark ? 0u : kNoBenchmark));
    }

    void point(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed) || answer >= answers()) return;
        if (asking()) {
            upload_answer = answer;
        } else if (!paged()) {
            row = answer;
        }
    }

    Effect click(unsigned answer) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!visible.load(std::memory_order_relaxed) || answer >= answers()) {
            return Effect::None;
        }
        if (asking()) {
            upload_answer = answer;
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
            if (asking()) {
                upload_answer = (upload_answer + UploadNum - 1u) % UploadNum;
            } else if (!paged()) {
                row = (row + rows() - 1u) % rows();
            }
            return Effect::None;
        case LauncherAction::Next:
            if (asking()) {
                upload_answer = (upload_answer + 1u) % UploadNum;
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
    bool paged() const { return message_page || upload_page != 0u || notice_page; }
    bool asking() const { return !message_page && upload_page == 3u; }
    unsigned answers() const { return asking() ? unsigned{UploadNum} : paged() ? 1u : rows(); }

    // Back (and a page's one answer): the message's page goes to the upload's, the upload's (No, or after its answer)
    // to the notice's when one is up, the notice's to the rows. The send itself, once started, goes on.
    Effect leave_page() {
        if (message_page) {
            message_page = false;
            return upload_page == 0u ? Effect::UploadLeft : Effect::None;
        }
        if (upload_page != 0u) {
            upload_page = 0u;
            return Effect::UploadLeft;
        }
        notice_page = false;
        return Effect::None;
    }

    Effect accept() {
        if (asking()) {
            switch (upload_answer) {
            case UploadShow: return Effect::ShowReport;
            case UploadSend: upload_page = 4u; return Effect::SendReport;
            default: return leave_page();
            }
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
