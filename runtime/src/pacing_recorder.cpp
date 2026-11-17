// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/pacing_recorder.h"

#include "galaxy/pacing.h"
#include "galaxy/platform_time.h"

#include <cstdio>
#include <deque>
#include <mutex>

namespace galaxy::pacing {

namespace {

constexpr std::int64_t kWindowSeconds = 10;
// A picture whose display report has not come this long after its present is closed as unknown.
constexpr std::int64_t kReportWaitMs = 1000;

void write_stderr(const std::string& text) {
    const std::string whole = text + "\n";
    std::fwrite(whole.data(), 1u, whole.size(), stderr);  // one call: the line stays whole beside other threads'
}

struct Waiting {
    Picture picture;
    std::uint64_t id = 0;
    std::int64_t presented_qpc = 0;
    bool reported = false;
};

struct Recorder {
    std::mutex mutex;
    Window window;
    std::int64_t window_first_step = 0;
    std::int64_t last_step = 0;             // game thread only: the moment of the step under way, 0 when done
    std::int64_t period_qpc = 0;            // the display's refresh period, from the latest report
    std::deque<Waiting> waiting;            // presented, in present order, until reported or given up
    std::uint64_t last_id = 0;              // the newest id presented
    std::uint64_t newest_reported = 0;      // the newest id reported
    std::uint64_t windows = 0;
    void (*write)(const std::string&) = write_stderr;

    // Render thread only.
    std::int64_t walk_start = 0;
    std::int64_t walk_ns = 0;
    bool described = false;
    Picture next;

    // Pictures leave the queue in present order: once reported, once a later present was reported (a report that
    // gives only the latest shown, as DXGI's, says nothing of the ones between), or once given up on.
    void settle(std::int64_t now) {
        const std::int64_t give_up = galaxy::qpc_frequency() / 1000 * kReportWaitMs;
        while (!waiting.empty()) {
            Waiting& w = waiting.front();
            if (!w.reported && w.id > newest_reported && now - w.presented_qpc < give_up) {
                break;
            }
            window.add_picture(w.picture);
            waiting.pop_front();
        }
    }

    Waiting* find(std::uint64_t id) {
        for (auto it = waiting.rbegin(); it != waiting.rend(); ++it) {
            if (it->id == id) return &*it;
            if (it->id < id) break;
        }
        return nullptr;
    }
};

Recorder& recorder() {
    static Recorder r;
    return r;
}

}  // namespace

void step(std::int64_t moment_qpc) noexcept {
    if (moment_qpc == 0) return;
    Recorder& r = recorder();
    std::string text;
    {
        const std::lock_guard<std::mutex> lock(r.mutex);
        const std::int64_t frequency = galaxy::qpc_frequency();
        if (r.window.steps() != 0 && moment_qpc - r.window_first_step >= kWindowSeconds * frequency) {
            r.settle(galaxy::qpc_now());
            text = line(r.window.summarize(r.period_qpc, frequency));
            r.window.clear();
            ++r.windows;
        }
        if (r.window.steps() == 0) r.window_first_step = moment_qpc;
        r.window.add_step(moment_qpc);
        r.last_step = moment_qpc;
    }
    if (!text.empty()) r.write(text);
}

void step_done() noexcept {
    Recorder& r = recorder();
    if (r.last_step == 0) return;
    const std::int64_t work = galaxy::qpc_now() - r.last_step;
    r.last_step = 0;
    const std::lock_guard<std::mutex> lock(r.mutex);
    r.window.add_step_work(galaxy::qpc_to_ns(work));
}

void walk_began() noexcept {
    recorder().walk_start = galaxy::qpc_now();
}

void walk_ended() noexcept {
    Recorder& r = recorder();
    if (r.walk_start == 0) return;
    r.walk_ns = galaxy::qpc_to_ns(galaxy::qpc_now() - r.walk_start);
    r.walk_start = 0;
}

void describe(std::int64_t asked_qpc, std::int64_t content_qpc, std::int64_t step_qpc, bool generated) noexcept {
    Recorder& r = recorder();
    r.next = Picture{};
    r.next.asked_qpc = asked_qpc;
    r.next.content_qpc = content_qpc;
    r.next.step_qpc = step_qpc;
    r.next.generated = generated;
    r.described = true;
}

void presented(std::uint64_t id) noexcept {
    Recorder& r = recorder();
    if (!r.described) return;
    r.described = false;
    r.next.render_ns = r.walk_ns;
    const std::int64_t now = galaxy::qpc_now();
    const std::lock_guard<std::mutex> lock(r.mutex);
    if (id != 0 && id <= r.last_id) {
        // A new swap chain counts its presents from the start again: nothing more comes for the old ones.
        r.newest_reported = ~std::uint64_t{0};
        r.settle(now);
        r.newest_reported = 0;
    }
    if (id != 0) r.last_id = id;
    Waiting w;
    w.picture = r.next;
    w.id = id;
    w.presented_qpc = now;
    // No report will come for an id of 0: it leaves with the next settle.
    w.reported = id == 0;
    r.waiting.push_back(w);
    r.settle(now);
}

void drop_description() noexcept {
    recorder().described = false;
}

void displayed(std::uint64_t id, std::int64_t moment_qpc, std::int64_t period_qpc) noexcept {
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mutex);
    if (period_qpc > 0) r.period_qpc = period_qpc;
    if (Waiting* w = r.find(id); w != nullptr && !w->reported) {
        w->picture.displayed_qpc = moment_qpc;
        w->reported = true;
    }
    if (id > r.newest_reported) r.newest_reported = id;
    r.settle(galaxy::qpc_now());
}

void not_shown(std::uint64_t id) noexcept {
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mutex);
    if (Waiting* w = r.find(id); w != nullptr && !w->reported) {
        w->picture.displayed_qpc = -1;
        w->reported = true;
    }
    if (id > r.newest_reported) r.newest_reported = id;
    r.settle(galaxy::qpc_now());
}

std::string line(const Summary& s) {
    char text[768];
    std::snprintf(text, sizeof(text),
                  "[pace] seconds=%.6f steps=%llu steps_per_second=%.4f pictures=%llu displayed=%llu generated=%llu "
                  "refresh_ms=%.3f on_time=%llu missed=%llu doubled=%llu animation_within_1ms=%.4f animation_p99_us=%.1f "
                  "delay_avg_us=%.1f delay_p99_us=%.1f render_avg_us=%.1f render_p99_us=%.1f generated_render_avg_us=%.1f "
                  "generated_render_p99_us=%.1f step_work_avg_us=%.1f step_work_p99_us=%.1f step_work_max_us=%.1f",
                  s.seconds, static_cast<unsigned long long>(s.steps), s.steps_per_second,
                  static_cast<unsigned long long>(s.pictures), static_cast<unsigned long long>(s.displayed),
                  static_cast<unsigned long long>(s.generated), s.refresh_ms, static_cast<unsigned long long>(s.on_time),
                  static_cast<unsigned long long>(s.missed_refreshes), static_cast<unsigned long long>(s.doubled_pictures),
                  s.animation_within_1ms, s.animation_p99_us, s.delay_avg_us, s.delay_p99_us, s.render_avg_us,
                  s.render_p99_us, s.generated_render_avg_us, s.generated_render_p99_us, s.step_work_avg_us,
                  s.step_work_p99_us, s.step_work_max_us);
    return text;
}

std::uint64_t windows_written() noexcept {
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mutex);
    return r.windows;
}

void reset_for_test(void (*write)(const std::string& line)) noexcept {
    Recorder& r = recorder();
    const std::lock_guard<std::mutex> lock(r.mutex);
    r.window.clear();
    r.window_first_step = 0;
    r.last_step = 0;
    r.period_qpc = 0;
    r.waiting.clear();
    r.last_id = 0;
    r.newest_reported = 0;
    r.windows = 0;
    r.write = write != nullptr ? write : write_stderr;
    r.walk_start = 0;
    r.walk_ns = 0;
    r.described = false;
}

}  // namespace galaxy::pacing
