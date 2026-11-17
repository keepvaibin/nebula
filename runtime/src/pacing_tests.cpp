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
        }
        // A new swap chain (ids from 1 again) closes the old ones still waiting as unknown.
        galaxy::pacing::describe(1, 1, 1, false);
        galaxy::pacing::presented(1);
        galaxy::pacing::reset_for_test(nullptr);
    }

    std::printf("%s\n", ok ? "pacing: every case holds" : "pacing: FAILED");
    return ok ? 0 : 1;
}
