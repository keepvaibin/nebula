// SPDX-License-Identifier: GPL-3.0-only
// benchmark_menu.h: the settings menu's benchmark rows, questions and results window.
#include "galaxy/ui/benchmark_menu.h"

#include "galaxy/benchmark.h"
#include "galaxy/benchmark_recommend.h"
#include "galaxy/benchmark_results.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/render/render_config.h"
#include "galaxy/ui/settings_benchmark_page.h"
#include "galaxy/virtual_keys.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace galaxy::ui::benchmark_menu {
namespace {

// Run: the run question. Send: the send question. Report: the send under way, then how it ended.
enum class Ask { None, Run, Send, Report };
enum class Sending { Idle, Sending, Sent, Failed };

struct State {
    std::vector<std::filesystem::path> files;  // the kept results, newest first
    std::vector<bench::ResultView> runs;       // each read (a file that does not read is left out with its run)
    std::string newest_text;                   // the newest result's file, for its recommendation over the draft
    bool newest_sent = false;                  // it was sent (its .sent marker), as it was read
    Ask ask = Ask::None;
    // The results window: the run shown, its pages, the page open, and the summary's graph.
    bool window = false;
    std::size_t shown = 0u;
    std::vector<BenchmarkPage> pages;
    unsigned page = 0u;
    std::shared_ptr<const render::NativeSettingsPicture> graph_picture;
    render::NativeSettingsGraph graph;
    // The newest result's send, while this session sends it, and why the last one failed.
    std::filesystem::path send_file;
    Sending sending = Sending::Idle;
    SendOutcome failure = SendOutcome::NotMade;
    std::string failure_detail;
};

std::mutex s_mutex;
State s_state;
std::atomic<void (*)(const std::filesystem::path&)> s_send{nullptr};
std::atomic<void (*)(const std::filesystem::path&)> s_show{nullptr};
std::atomic<bool (*)()> s_start{nullptr};
std::atomic<bool (*)()> s_in_play{nullptr};

bool file_exists(const std::filesystem::path& file) {
    std::error_code error;
    return std::filesystem::exists(file, error);
}

std::filesystem::path sent_marker(const std::filesystem::path& result) {
    std::filesystem::path marker = result;
    marker += ".sent";
    return marker;
}

// Larger than any file the benchmark writes: a result (read_result's bound) and its frame lists.
constexpr std::size_t kMostResultBytes = std::size_t{1} << 20;
constexpr std::size_t kMostFrameBytes = std::size_t{16} << 20;

// A file's bytes; empty when it does not read or is larger than `most`.
std::string file_text(const std::filesystem::path& file, std::size_t most) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::string text(most + 1u, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    const auto read = static_cast<std::size_t>(in.gcount());
    if (read > most) return {};
    text.resize(read);
    return text;
}

// Holds s_mutex.
void read_results(State& state) {
    state.files.clear();
    state.runs.clear();
    state.newest_text.clear();
    state.newest_sent = false;
    for (const std::filesystem::path& file : bench::list_results(bench::results_folder())) {
        bench::ResultView run;
        if (state.runs.size() < bench::kKeptResults && bench::read_result(file, run)) {
            state.files.push_back(file);
            state.runs.push_back(std::move(run));
        }
    }
    if (!state.files.empty()) {
        state.newest_text = file_text(state.files.front(), kMostResultBytes);
        state.newest_sent = file_exists(sent_marker(state.files.front()));
    }
}

// The frame-time graph of the shown run's stages at the player's settings: empty when its frame lists are missing or
// unreadable.
void draw_graph(State& state) {
    state.graph_picture.reset();
    state.graph = {};
    if (state.shown >= state.runs.size()) return;
    constexpr unsigned part = 0u;  // the frame lists' part at the player's settings (benchmark.cpp frame_lists)
    const std::string bytes = file_text(bench::frames_file(state.files[state.shown]), kMostFrameBytes);
    std::vector<bench::FrameList> lists;
    if (!bench::decode_frame_lists(bytes, lists)) return;
    bench::FrameGraph drawn = bench::draw_frame_graph(lists, part, kBenchmarkGraphWidth, kBenchmarkGraphHeight);
    if (drawn.image.rgba.empty()) return;
    auto picture = std::make_shared<render::NativeSettingsPicture>();
    picture->width = drawn.image.width;
    picture->height = drawn.image.height;
    picture->rgba = std::move(drawn.image.rgba);
    state.graph_picture = std::move(picture);
    state.graph.left = drawn.left;
    state.graph.top = drawn.top;
    state.graph.right = drawn.right;
    state.graph.bottom = drawn.bottom;
    state.graph.top_fps = static_cast<float>(drawn.top_fps);
    state.graph.step_fps = static_cast<float>(drawn.step_fps);
    state.graph.stage_starts = std::move(drawn.stage_starts);
}

// Holds s_mutex.
bool open_window(State& state, std::size_t shown) {
    if (shown >= state.runs.size()) return false;
    state.ask = Ask::None;
    state.window = true;
    state.shown = shown;
    state.page = 0u;
    state.pages = benchmark_pages(state.runs, shown, benchmark_local_time);
    draw_graph(state);
    return !state.pages.empty();
}

// Whether the newest result may be sent: the build sends reports, the run completed or failed (one the player stopped
// or closed measured too little), and it was not sent.
bool sendable(const State& state) {
    return s_send.load(std::memory_order_acquire) != nullptr && !state.runs.empty() &&
           (state.runs.front().status == "complete" || state.runs.front().status == "failed");
}

// The results window's Send to developer, on the newest run where it may be sent: kBenchmarkResultsSendable offered,
// kBenchmarkResultsSent greyed (it was sent or is being sent), 0 not shown.
int window_send(const State& state) {
    if (!state.window || state.shown != 0u || state.files.empty() || !sendable(state)) return 0;
    const bool this_file = state.send_file == state.files.front();
    if (state.newest_sent || (this_file && (state.sending == Sending::Sending || state.sending == Sending::Sent))) {
        return kBenchmarkResultsSent;
    }
    return kBenchmarkResultsSendable;
}

bool window_sendable(const State& state) {
    return state.ask == Ask::None && window_send(state) == kBenchmarkResultsSendable;
}

}  // namespace

void set_reports(const Reports& reports) noexcept {
    s_send.store(reports.send, std::memory_order_release);
    s_show.store(reports.show, std::memory_order_release);
}

void set_runner(const Runner& runner) noexcept {
    s_start.store(runner.start, std::memory_order_release);
    s_in_play.store(runner.in_play, std::memory_order_release);
}

void report_sent(SendOutcome outcome, std::string_view detail) noexcept {
    try {
        const std::lock_guard<std::mutex> lock(s_mutex);
        if (s_state.sending != Sending::Sending) return;
        s_state.sending = outcome == SendOutcome::Sent ? Sending::Sent : Sending::Failed;
        s_state.failure = outcome;
        s_state.failure_detail.assign(detail.substr(0u, 200u));
    } catch (...) {
    }
}

void refresh() {
    const std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_state.window) read_results(s_state);
}

BenchmarkView rows(const render::RenderConfig& draft, std::string_view adapter,
                   std::function<void(const render::RenderConfig&)> set_draft) {
    const std::lock_guard<std::mutex> lock(s_mutex);
    State& state = s_state;
    BenchmarkView view;
    view.can_run = s_start.load(std::memory_order_acquire) != nullptr;
    view.run = [] {
        const std::lock_guard<std::mutex> inner(s_mutex);
        s_state.ask = Ask::Run;
    };
    view.has_result = !state.runs.empty();
    if (!view.has_result) return view;
    const bench::ResultView& last = state.runs.front();
    view.last_when = benchmark_local_time(last.created_utc);
    view.last_outcome = benchmark_outcome(last);
    view.view = [] {
        const std::lock_guard<std::mutex> inner(s_mutex);
        (void)open_window(s_state, 0u);
    };

    render::RenderConfig recommended = draft;
    if (!bench::parse_recommendation(state.newest_text, recommended)) {
        view.apply = BenchmarkApply::NoResult;
    } else if (!last.gpu.empty() && !adapter.empty() && last.gpu != adapter) {
        view.apply = BenchmarkApply::OtherGpu;
    } else if (!last.backend.empty() && last.backend != bench::running_backend()) {
        view.apply = BenchmarkApply::OtherRenderer;
    } else if (recommended == draft) {
        view.apply = BenchmarkApply::InUse;
    } else {
        view.apply = BenchmarkApply::Offer;
        view.recommend = [set_draft, recommended] {
            if (set_draft) set_draft(recommended);
        };
    }

    const std::filesystem::path& newest = state.files.front();
    if (state.send_file != newest) {
        state.send_file = newest;
        state.sending = Sending::Idle;
    }
    if (state.sending == Sending::Sending) {
        view.send = BenchmarkSend::Sending;
    } else if (state.sending == Sending::Sent || state.newest_sent) {
        view.send = s_send.load(std::memory_order_acquire) != nullptr ? BenchmarkSend::Sent : BenchmarkSend::Hidden;
    } else if (!sendable(state)) {
        view.send = BenchmarkSend::Hidden;
    } else {
        view.send = state.sending == Sending::Failed ? BenchmarkSend::Failed : BenchmarkSend::Offer;
        view.send_report = [] {
            const std::lock_guard<std::mutex> inner(s_mutex);
            s_state.ask = Ask::Send;
        };
    }
    return view;
}

bool open_results(const std::filesystem::path& result) {
    const std::lock_guard<std::mutex> lock(s_mutex);
    read_results(s_state);
    std::size_t shown = 0u;
    std::error_code error;
    if (!std::filesystem::is_directory(result, error)) {
        const auto at = std::find_if(s_state.files.begin(), s_state.files.end(), [&](const std::filesystem::path& file) {
            std::error_code same;
            return std::filesystem::equivalent(file, result, same);
        });
        if (at == s_state.files.end()) return false;
        shown = static_cast<std::size_t>(at - s_state.files.begin());
    }
    return open_window(s_state, shown);
}

bool up() noexcept {
    try {
        const std::lock_guard<std::mutex> lock(s_mutex);
        return s_state.window || s_state.ask != Ask::None;
    } catch (...) {
        return false;
    }
}

bool results_up() noexcept {
    try {
        const std::lock_guard<std::mutex> lock(s_mutex);
        return s_state.window && s_state.ask == Ask::None;
    } catch (...) {
        return false;
    }
}

bool key(std::uintptr_t key, bool was_down, const std::function<void(const char*)>& message) {
    void (*send)(const std::filesystem::path&) = nullptr;
    void (*show)(const std::filesystem::path&) = nullptr;
    std::filesystem::path file;
    bool run = false;
    {
        const std::lock_guard<std::mutex> lock(s_mutex);
        State& state = s_state;
        if (!state.window && state.ask == Ask::None) return false;
        if (state.ask == Ask::Report) {
            // The send: every key waits while it is under way; after it, OK goes back (to the window when it was
            // asked from there).
            if (!was_down && state.sending != Sending::Sending &&
                (key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE || key == VK_BACK ||
                 key == input::settings_pad::kKeyAccept)) {
                state.ask = Ask::None;
            }
            return key != VK_F1;
        }
        if (state.ask == Ask::None) {
            // The window. Its Send to developer asks first (the send question over it, then the window again).
            if ((key == VK_SPACE || key == input::settings_pad::kKeyAccept) && window_sendable(state)) {
                if (!was_down) state.ask = Ask::Send;
                return true;
            }
            if (key == input::settings_pad::kKeyAccept) key = VK_RIGHT;
            const bool turn_next = key == VK_RETURN || key == VK_SPACE || key == VK_RIGHT || key == VK_NEXT;
            const bool turn_back = key == VK_BACK || key == VK_LEFT || key == VK_PRIOR;
            // Held keys turn the pages on as they repeat; the window's other keys wait for a press.
            if (turn_next && state.page + 1u < state.pages.size()) {
                ++state.page;
            } else if (turn_back && state.page > 0u) {
                --state.page;
            } else if (!was_down && (key == VK_ESCAPE || key == VK_F1)) {
                state.window = false;
                state.pages.clear();
                state.graph_picture.reset();
                state.graph = {};
            }
            return key != VK_F1;
        }
        if (was_down) return true;
        if (state.ask == Ask::Run) {
            if (key == VK_RETURN || key == VK_SPACE) {
                state.ask = Ask::None;
                run = true;
            } else if (key == VK_ESCAPE || key == VK_BACK || key == VK_F1) {
                state.ask = Ask::None;
            }
        } else if (state.ask == Ask::Send) {
            if (key == VK_RETURN || key == VK_SPACE) {
                state.ask = Ask::None;
                if (!state.files.empty() && sendable(state)) {
                    state.ask = Ask::Report;
                    state.send_file = state.files.front();
                    state.sending = Sending::Sending;
                    state.failure_detail.clear();
                    send = s_send.load(std::memory_order_acquire);
                    file = state.send_file;
                }
            } else if (key == VK_BACK) {
                show = s_show.load(std::memory_order_acquire);
                if (!state.files.empty()) file = state.files.front();
            } else if (key == VK_ESCAPE || key == VK_F1) {
                state.ask = Ask::None;
            }
        }
    }
    // Outside the lock: a send answers through report_sent, which takes it.
    if (send != nullptr && !file.empty()) send(file);
    if (show != nullptr && !file.empty()) show(file);
    if (run) {
        bool (*const start)() = s_start.load(std::memory_order_acquire);
        if (start != nullptr && start()) {
            std::fprintf(stderr, "[settings] the benchmark starts: the game goes back to its title\n");
        } else if (message) {
            message("COULD NOT START");
        }
    }
    return key != VK_F1;
}

bool fill(render::NativeSettingsView& view) {
    const std::lock_guard<std::mutex> lock(s_mutex);
    const State& state = s_state;
    if (state.ask == Ask::Run) {
        bool (*const in_play)() = s_in_play.load(std::memory_order_acquire);
        view.status = render::NativeSettingsStatus::BenchmarkAsk;
        view.status_number = in_play != nullptr && in_play() ? 1 : 0;
        return true;
    }
    // The send's question and screens: in the results window's place when asked from it.
    const int from = state.window ? kBenchmarkFromResults : 0;
    if (state.ask == Ask::Send) {
        view.status = render::NativeSettingsStatus::BenchmarkSendAsk;
        view.status_number = (s_show.load(std::memory_order_acquire) != nullptr ? 1 : 0) | from;
        return true;
    }
    if (state.ask == Ask::Report) {
        if (state.sending == Sending::Sending) {
            view.status = render::NativeSettingsStatus::BenchmarkSending;
            view.status_number = from;
        } else if (state.sending == Sending::Sent) {
            view.status = render::NativeSettingsStatus::BenchmarkSent;
            view.status_number = from;
        } else {
            view.status = render::NativeSettingsStatus::BenchmarkSendFailed;
            view.status_number = static_cast<int>(state.failure) | from;
            view.status_text = state.failure_detail;
        }
        return true;
    }
    if (!state.window || state.page >= state.pages.size()) return false;
    const BenchmarkPage& page = state.pages[state.page];
    view.status = render::NativeSettingsStatus::BenchmarkResults;
    view.status_text = page.title;
    view.status_number = static_cast<int>(state.page << 8u | static_cast<unsigned>(state.pages.size())) | window_send(state);
    view.long_text = page.table;
    if (page.graph && state.graph_picture != nullptr) {
        view.picture = state.graph_picture;
        view.graph = state.graph;
    }
    return true;
}

void close() noexcept {
    try {
        const std::lock_guard<std::mutex> lock(s_mutex);
        s_state.ask = Ask::None;
        s_state.window = false;
        s_state.pages.clear();
        s_state.graph_picture.reset();
        s_state.graph = {};
    } catch (...) {
    }
}

}  // namespace galaxy::ui::benchmark_menu
