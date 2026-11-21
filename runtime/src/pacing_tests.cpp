// SPDX-License-Identifier: GPL-3.0-only
// galaxy::pacing::Window on synthetic runs whose measures are known by arithmetic: a clock of 10 MHz, steps at 60 a
// second, a display at 60 Hz or 120 Hz.
#include "galaxy/pacing.h"
#include "galaxy/pacing_recorder.h"
#include "galaxy/platform_time.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr std::int64_t kHz = 10'000'000;               // ticks a second
constexpr double kStep = kHz / 60.0;                   // 166666.67 ticks
constexpr std::int64_t kRefresh60 = kHz / 60;          // 166666
constexpr std::int64_t kRefresh120 = kHz / 120;        // 83333

std::int64_t at(double ticks) { return static_cast<std::int64_t>(std::llround(ticks)); }

bool expect(bool ok, const char* what) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", what);
    return ok;
}

bool close_to(double a, double b, double within) { return std::abs(a - b) <= within; }

// n steps at 60 a second; one real picture per step, shown `latency` refreshes after its step began; picture `late`
// (when not negative) one refresh later than that.
galaxy::pacing::Window sixty(int n, int latency, int late) {
    galaxy::pacing::Window w;
    for (int i = 0; i < n; ++i) {
        const double step = 1'000'000.0 + i * kStep;
        w.add_step(at(step));
        galaxy::pacing::Picture p;
        p.step_qpc = at(step);
        p.content_qpc = at(step);
        p.asked_qpc = at(step + latency * kStep);
        p.displayed_qpc = at(step + (latency + (i == late ? 1 : 0)) * kStep);
        p.render_ns = 2'000'000;
        w.add_picture(p);
    }
    return w;
}

}  // namespace

int main() {
    bool ok = true;

    // Even pictures at 60 Hz: 601 steps over exactly 10 s.
    {
        const galaxy::pacing::Summary s = sixty(601, 2, -1).summarize(kRefresh60, kHz);
        ok &= expect(close_to(s.seconds, 10.0, 1e-6), "601 steps span 10 s");
        ok &= expect(close_to(s.steps_per_second, 60.0, 0.006), "60.000 steps a second (within 0.01%)");
        ok &= expect(s.pictures == 601 && s.displayed == 601, "every picture displayed");
        ok &= expect(s.animation_within_1ms == 1.0 && s.animation_p99_us < 1.0, "no animation error");
        ok &= expect(s.missed_refreshes == 0 && s.doubled_pictures == 0 && s.on_time == 600, "every refresh on time");
        ok &= expect(close_to(s.delay_avg_us, 2 * 1e6 / 60, 1.0), "delay: two refreshes");
        ok &= expect(close_to(s.render_avg_us, 2000.0, 0.01) && s.generated == 0, "render time 2 ms, none generated");
    }
    // One picture a refresh late: the gap before it is two refreshes (one missed), the one after is none (the next
    // picture reaches the display in the same refresh: doubled), and its animation error is a refresh.
    {
        const galaxy::pacing::Summary s = sixty(601, 2, 300).summarize(kRefresh60, kHz);
        ok &= expect(s.missed_refreshes == 1, "a late picture: one missed refresh");
        ok &= expect(s.doubled_pictures == 1, "the picture after it: doubled");
        ok &= expect(s.on_time == 598, "the rest on time");
        ok &= expect(close_to(s.animation_within_1ms, 600.0 / 601.0, 1e-9), "one picture outside 1 ms");
        ok &= expect(close_to(s.animation_p99_us, 0.0, 1.0), "p99 of 601: still none (one picture is the top 0.2%)");
    }
    // 120 Hz with in-between pictures: two pictures per step, the second at t = 0.5, each shown one refresh after the
    // one before; the in-between one costs 0.5 ms.
    {
        galaxy::pacing::Window w;
        for (int i = 0; i < 600; ++i) {
            const double step = 1'000'000.0 + i * kStep;
            w.add_step(at(step));
            for (int half = 0; half < 2; ++half) {
                galaxy::pacing::Picture p;
                p.step_qpc = at(step);
                p.content_qpc = at(step + half * kStep / 2);
                p.asked_qpc = at(step + kStep + half * kStep / 2);
                p.displayed_qpc = p.asked_qpc;
                p.generated = half == 1;
                p.render_ns = half == 1 ? 500'000 : 3'000'000;
                w.add_picture(p);
            }
        }
        const galaxy::pacing::Summary s = w.summarize(kRefresh120, kHz);
        ok &= expect(s.pictures == 1200 && s.generated == 600, "1200 pictures, 600 in between");
        ok &= expect(s.missed_refreshes == 0 && s.doubled_pictures == 0 && s.on_time == 1199, "120 Hz: one refresh each");
        ok &= expect(s.animation_within_1ms == 1.0, "in-between pictures show their own moment");
        ok &= expect(close_to(s.generated_render_avg_us, 500.0, 0.01) && close_to(s.render_avg_us, 3000.0, 0.01),
                     "render time apart for real and in-between pictures");
        ok &= expect(close_to(s.delay_avg_us, 1e6 / 60, 1.0), "delay counted on real pictures: one step");
    }
    // An in-between picture that shows the wrong moment (its content 2 ms off) is an animation error of 2 ms even when
    // it reaches the display on time.
    {
        galaxy::pacing::Window w;
        for (int i = 0; i < 100; ++i) {
            galaxy::pacing::Picture p;
            p.content_qpc = at(i * kStep) + (i == 50 ? 20'000 : 0);  // 2 ms at 10 MHz
            p.asked_qpc = at(i * kStep + kStep);
            p.displayed_qpc = p.asked_qpc;
            w.add_picture(p);
        }
        const galaxy::pacing::Summary s = w.summarize(kRefresh60, kHz);
        ok &= expect(close_to(s.animation_within_1ms, 0.99, 1e-9), "99 of 100 within 1 ms");
        ok &= expect(close_to(s.animation_p99_us, 0.1, 0.2), "p99 of 100 excludes the one");
    }
    // Pictures whose display moment is unknown count as pictures but in no display measure.
    {
        galaxy::pacing::Window w;
        for (int i = 0; i < 10; ++i) {
            galaxy::pacing::Picture p;
            p.asked_qpc = i * kRefresh60;
            w.add_picture(p);
        }
        const galaxy::pacing::Summary s = w.summarize(kRefresh60, kHz);
        ok &= expect(s.pictures == 10 && s.displayed == 0 && s.on_time == 0 && s.missed_refreshes == 0,
                     "no display moments: nothing judged");
        ok &= expect(s.steps == 0 && s.steps_per_second == 0.0, "no steps: no rate");
    }

    // A picture the renderer knows never reached the display counts as doubled, not as displayed.
    {
        galaxy::pacing::Window w = sixty(10, 2, -1);
        galaxy::pacing::Picture p;
        p.displayed_qpc = -1;
        w.add_picture(p);
        const galaxy::pacing::Summary s = w.summarize(kRefresh60, kHz);
        ok &= expect(s.pictures == 11 && s.displayed == 10 && s.doubled_pictures == 1, "never shown: doubled");
    }

    // Ticks as nanoseconds past the 9.2 seconds where ticks times 10^9 leaves 64 bits (the recorder's step and walk
    // times: a step that long, a stage's load, overflowed; the address and undefined-behaviour job found it).
    {
        const std::int64_t f = galaxy::qpc_frequency();
        ok &= expect(galaxy::qpc_to_ns(20 * f) == 20'000'000'000 && galaxy::qpc_to_ns(-10 * f) == -10'000'000'000 &&
                         galaxy::qpc_to_ns(3600 * f + f / 2) == 3'600'500'000'000 && galaxy::qpc_to_ns(f / 1000) == 1'000'000,
                     "ticks as nanoseconds: 20 s, -10 s, an hour and a half second, 1 ms");
    }

    // Step work: its average, 99th percentile and longest.
    {
        galaxy::pacing::Window w;
        for (int i = 1; i <= 100; ++i) w.add_step_work(i * 100'000);  // 0.1 ms .. 10 ms
        const galaxy::pacing::Summary s = w.summarize(kRefresh60, kHz);
        ok &= expect(close_to(s.step_work_avg_us, 5050.0, 0.01) && close_to(s.step_work_p99_us, 9900.0, 0.01) &&
                         close_to(s.step_work_max_us, 10000.0, 0.01),
                     "step work: 5.05 ms average, 9.9 ms p99, 10 ms longest");
    }

    // The window's processor time per step: 600 steps that used 3 s of processor time are 5000 us a step; not known
    // (-1, or no steps) is not a number.
    {
        galaxy::pacing::Window w;
        for (int i = 0; i < 600; ++i) w.add_step(at(1'000'000.0 + i * kStep));
        w.set_cpu_ns(3'000'000'000);
        galaxy::pacing::Summary s = w.summarize(kRefresh60, kHz);
        ok &= expect(s.cpu_known && close_to(s.cpu_us_per_step, 5000.0, 0.001), "3 s over 600 steps: 5000 us a step");
        w.set_cpu_ns(0);
        s = w.summarize(kRefresh60, kHz);
        ok &= expect(s.cpu_known && s.cpu_us_per_step == 0.0, "no processor time used is a known 0");
        w.set_cpu_ns(-1);
        s = w.summarize(kRefresh60, kHz);
        ok &= expect(!s.cpu_known, "a processor time of -1 is not known");
        w.set_cpu_ns(1'000'000);
        w.clear();
        for (int i = 0; i < 600; ++i) w.add_step(at(1'000'000.0 + i * kStep));  // the next window's steps
        ok &= expect(!w.summarize(kRefresh60, kHz).cpu_known, "a cleared window forgets its processor time");
        galaxy::pacing::Window none;
        none.set_cpu_ns(1'000'000);
        ok &= expect(!none.summarize(kRefresh60, kHz).cpu_known, "no steps: no per step figure");
    }
    // The [pace] line: cpu_us_per_step at its end when known, nothing of it when not.
    {
        galaxy::pacing::Summary s;
        s.steps = 600;
        const std::string without = galaxy::pacing::line(s);
        ok &= expect(without.find("cpu_us_per_step") == std::string::npos && without.find("step_work_max_us=") != std::string::npos,
                     "not known: the field is left out of the line");
        s.cpu_known = true;
        s.cpu_us_per_step = 3350.55;
        const std::string with = galaxy::pacing::line(s);
        ok &= expect(with.size() > without.size() && with.compare(0, without.size(), without) == 0 &&
                         with.substr(without.size()) == " cpu_us_per_step=3350.6",
                     "known: the field follows the others, one decimal");
        s.cpu_us_per_step = 0.0;
        ok &= expect(galaxy::pacing::line(s).find(" cpu_us_per_step=0.0") != std::string::npos, "a known 0 is written as 0.0");
    }
    // Cycles as time by the counter's rate over the span: a counter of 3 GHz over one second of the clock, 6e9 cycles are
    // 2 s; a span of two seconds with the counter at 2 GHz makes 1e9 cycles half a second.
    {
        ok &= expect(galaxy::cycles_to_ns(6'000'000'000ULL, 3'000'000'000ULL, kHz, kHz) == 2'000'000'000LL,
                     "6e9 cycles on a 3 GHz counter: 2 s");
        ok &= expect(galaxy::cycles_to_ns(1'000'000'000ULL, 4'000'000'000ULL, 2 * kHz, kHz) == 500'000'000LL,
                     "1e9 cycles on a 2 GHz counter: 0.5 s");
        ok &= expect(galaxy::cycles_to_ns(5, 0, kHz, kHz) == -1 && galaxy::cycles_to_ns(5, 7, 0, kHz) == -1 &&
                         galaxy::cycles_to_ns(5, 7, kHz, 0) == -1,
                     "an empty span is not known");
        galaxy::ProcessCpuStamp a, b;
        ok &= expect(galaxy::process_cpu_ns_between(a, b) == -1, "stamps that are not known give -1");
        if (galaxy::process_cpu_known()) {
            // Spin this thread for 300 ms of the clock: the process used at least a tenth of that and no more than all of
            // the machine's processors could give (a count of 64 here).
            a = galaxy::process_cpu_stamp();
            const std::int64_t until = galaxy::qpc_now() + galaxy::qpc_frequency() / 1000 * 300;
            volatile std::uint64_t sink = 0;
            while (galaxy::qpc_now() < until) sink = sink + 1;
            b = galaxy::process_cpu_stamp();
            const std::int64_t ns = galaxy::process_cpu_ns_between(a, b);
            ok &= expect(a.known && b.known, "this machine's stamps are known");
            ok &= expect(ns >= 30'000'000 && ns <= 64 * 600'000'000LL, "a 300 ms spin: its processor time in the stamps");
        }
    }
    // The frame threads' work per step: 600 steps in which the game thread worked 1.2 s and the render thread 0.6 s are 2000 and
    // 1000 us a step, together 3000; no threads read is not a number.
    {
        galaxy::pacing::Window w;
        for (int i = 0; i < 600; ++i) w.add_step(at(1'000'000.0 + i * kStep));
        w.set_thread_work({{"game", 1'200'000'000}, {"render", 600'000'000}});
        galaxy::pacing::Summary s = w.summarize(kRefresh60, kHz);
        ok &= expect(s.work_known && s.thread_cpu.size() == 2 && s.thread_cpu[0].name == "game" &&
                         close_to(s.thread_cpu[0].us_per_step, 2000.0, 0.001) && s.thread_cpu[1].name == "render" &&
                         close_to(s.thread_cpu[1].us_per_step, 1000.0, 0.001) && close_to(s.cpu_work_us_per_step, 3000.0, 0.001),
                     "frame threads' work: 2000 and 1000 us a step, 3000 together");
        w.set_thread_work({});
        ok &= expect(!w.summarize(kRefresh60, kHz).work_known, "no threads: the work is not known");
        w.set_thread_work({{"game", 1'000'000}});
        w.clear();
        for (int i = 0; i < 600; ++i) w.add_step(at(1'000'000.0 + i * kStep));
        ok &= expect(!w.summarize(kRefresh60, kHz).work_known, "a cleared window forgets its threads' work");
        galaxy::pacing::Window none;
        none.set_thread_work({{"game", 1'000'000}});
        ok &= expect(!none.summarize(kRefresh60, kHz).work_known, "no steps: no per step figure");
    }
    // Their fields in the line: the sum, then each thread by its name; none when not known.
    {
        galaxy::pacing::Summary s;
        s.steps = 600;
        const std::string without = galaxy::pacing::line(s);
        ok &= expect(without.find("cpu_work_us_per_step") == std::string::npos && without.find("cpu_game_us_per_step") == std::string::npos,
                     "not read: no cpu_work fields in the line");
        s.work_known = true;
        s.cpu_work_us_per_step = 2500.04;
        s.thread_cpu = {{"game", 1900.0}, {"render", 600.04}, {"step_worker", 0.0}};
        const std::string with = galaxy::pacing::line(s);
        ok &= expect(with.compare(0, without.size(), without) == 0 &&
                         with.substr(without.size()) == " cpu_work_us_per_step=2500.0 cpu_game_us_per_step=1900.0 "
                                                       "cpu_render_us_per_step=600.0 cpu_step_worker_us_per_step=0.0",
                     "known: the sum, then each thread by its name, one decimal");
    }
    // Units as nanoseconds: a cycle count by the counter's rate over the span the stamps close (Windows on x64), nanoseconds as
    // they are elsewhere.
    {
        galaxy::ProcessCpuStamp a, b;
        ok &= expect(galaxy::cpu_units_to_ns(5, a, b) == -1, "stamps that are not known: -1");
        a.known = b.known = true;
#if defined(GALAXY_PROCESS_CYCLES)
        a.counter = 1'000;
        b.counter = 1'000 + 3'000'000'000ULL;
        a.qpc = 5;
        b.qpc = 5 + galaxy::qpc_frequency();  // one second of the clock: the counter ran at 3 GHz
        ok &= expect(galaxy::cpu_units_to_ns(6'000'000'000ULL, a, b) == 2'000'000'000LL, "6e9 cycles on a 3 GHz counter: 2 s");
        b.counter = a.counter;
        ok &= expect(galaxy::cpu_units_to_ns(1, a, b) == -1, "an empty counter span: -1");
#else
        ok &= expect(galaxy::cpu_units_to_ns(1234, a, b) == 1234, "nanoseconds stay as they are");
#endif
    }
    // The recorder reads the frame threads at the window's first step and at its end: here a reader whose "game" thread did all
    // of the process's work in the window and whose "render" thread half of it, so the fields hold that.
    {
        static std::vector<std::string> work_lines;
        work_lines.clear();
        galaxy::pacing::reset_for_test([](const std::string& text) { work_lines.push_back(text); });
        galaxy::pacing::set_work_reader([](galaxy::ThreadCpuSample* out, std::size_t capacity) -> std::size_t {
            if (capacity < 2 || !galaxy::process_cpu_known()) return 0;
            const galaxy::ProcessCpuStamp now = galaxy::process_cpu_stamp();
            out[0] = {"game", now.cycles};
            out[1] = {"render", now.cycles / 2};
            return 2;
        });
        const std::int64_t f = galaxy::qpc_frequency();
        const std::int64_t base = galaxy::qpc_now();
        for (int i = 0; i <= 601; ++i) {
            if (i == 300) {  // some work in the window
                const std::int64_t until = galaxy::qpc_now() + f / 1000 * 20;
                volatile std::uint64_t sink = 0;
                while (galaxy::qpc_now() < until) sink = sink + 1;
            }
            galaxy::pacing::step(base + static_cast<std::int64_t>(std::llround(i * static_cast<double>(f) / 60.0)));
        }
        ok &= expect(work_lines.size() == 1, "one window with the reader set");
        if (!work_lines.empty()) {
            const std::string& l = work_lines.front();
            const auto value = [&l](const char* key) {
                const std::string k = std::string(" ") + key + "=";
                const std::size_t at = l.find(k);
                return at == std::string::npos ? std::nan("") : std::strtod(l.c_str() + at + k.size(), nullptr);
            };
            if (galaxy::process_cpu_known()) {
                const double game = value("cpu_game_us_per_step"), render = value("cpu_render_us_per_step"), sum = value("cpu_work_us_per_step");
                ok &= expect(game > 0.0 && close_to(render, game / 2.0, game * 0.1) && close_to(sum, game + render, 0.31),
                             "the reader's threads: game all of the window's work, render half of it, together the sum");
                // The reader's "game" thread worked the whole process's window: only what it did since the window's first step counts.
                const double whole = value("cpu_us_per_step");
                ok &= expect(close_to(game, whole, whole * 0.2),
                             "the window's start reading is taken off: game is the process's own figure, not the thread's total");
                ok &= expect(l.find("cpu_work_us_per_step") < l.find("cpu_game_us_per_step") &&
                                 l.find("cpu_game_us_per_step") < l.find("cpu_render_us_per_step"),
                             "the fields in the order of the sum, game, render");
            } else {
                ok &= expect(std::isnan(value("cpu_work_us_per_step")), "no processor time known: no work fields");
            }
        }
        galaxy::pacing::reset_for_test(nullptr);
        // No reader: the line has no work fields.
        work_lines.clear();
        galaxy::pacing::reset_for_test([](const std::string& text) { work_lines.push_back(text); });
        for (int i = 0; i <= 601; ++i) {
            galaxy::pacing::step(base + static_cast<std::int64_t>(std::llround(i * static_cast<double>(f) / 60.0)));
        }
        ok &= expect(work_lines.size() == 1 && work_lines.front().find("cpu_work_us_per_step") == std::string::npos,
                     "no reader: no cpu_work fields in the line");
        galaxy::pacing::reset_for_test(nullptr);
    }
    // The recorder: steps on a step clock, pictures described and presented, display reports as DXGI gives them (only
    // the latest shown, two presents behind), one window written after ten seconds of steps.
    {
        static std::vector<std::string> lines;
        lines.clear();
        galaxy::pacing::reset_for_test([](const std::string& text) { lines.push_back(text); });
        const std::int64_t f = galaxy::qpc_frequency();
        const std::int64_t base = galaxy::qpc_now();
        const auto moment = [&](int i) { return base + static_cast<std::int64_t>(std::llround(i * static_cast<double>(f) / 60.0)); };
        for (int i = 0; i <= 601; ++i) {
            galaxy::pacing::step(moment(i));
            galaxy::pacing::step_done();
            galaxy::pacing::walk_began();
            galaxy::pacing::walk_ended();
            galaxy::pacing::describe(moment(i + 2), moment(i + 1), moment(i), false);
            galaxy::pacing::presented(static_cast<std::uint64_t>(i + 1));
            if (i >= 2) {
                // Present i - 1 (id i - 1) shown at its asked moment: steps i - 2's picture, asked for step i.
                galaxy::pacing::displayed(static_cast<std::uint64_t>(i - 1), moment(i), f / 60);
            }
        }
        ok &= expect(lines.size() == 1 && galaxy::pacing::windows_written() == 1, "one window after ten seconds");
        if (!lines.empty()) {
            const std::string& l = lines.front();
            // A key's value in the line, NaN when it is missing.
            const auto value = [&l](const char* key) {
                const std::string k = std::string(" ") + key + "=";
                const std::size_t at = l.find(k);
                return at == std::string::npos ? std::nan("") : std::strtod(l.c_str() + at + k.size(), nullptr);
            };
            const double steps = value("steps"), rate = value("steps_per_second"), pictures = value("pictures");
            const double displayed = value("displayed"), on_time = value("on_time"), missed = value("missed");
            const double within = value("animation_within_1ms"), delay = value("delay_avg_us");
            ok &= expect(l.rfind("[pace] seconds=", 0) == 0 && !std::isnan(delay), "the [pace] line reads back");
            ok &= expect(steps == 600 && close_to(rate, 60.0, 0.006), "600 steps in ten seconds at 60 a second");
            // Pictures 0..597 settled into the window: each reported, two presents behind the last.
            ok &= expect(pictures >= 595 && pictures == displayed, "every settled picture displayed");
            ok &= expect(missed == 0 && on_time + 1 == displayed, "every one a refresh after the one before");
            ok &= expect(within == 1.0, "no animation error");
            ok &= expect(close_to(delay, 2e6 / 60, 2.0), "delay: two steps");
            ok &= expect(!std::isnan(value("step_work_max_us")), "the step's work in the line");
            // The processor time of the whole process: in the line when the machine gives it (a positive time per step: the
            // loop ran a few milliseconds), else not in it.
            ok &= expect(galaxy::process_cpu_known() == !std::isnan(value("cpu_us_per_step")),
                         "cpu_us_per_step in the line exactly where the processor time is known");
            ok &= expect(!galaxy::process_cpu_known() || value("cpu_us_per_step") > 0.0, "the processor time per step is above 0");
        }
        // A new swap chain (ids from 1 again) closes the old ones still waiting as unknown.
        galaxy::pacing::describe(1, 1, 1, false);
        galaxy::pacing::presented(1);
        galaxy::pacing::reset_for_test(nullptr);
    }

    std::printf("%s\n", ok ? "pacing: every case holds" : "pacing: FAILED");
    return ok ? 0 : 1;
}
