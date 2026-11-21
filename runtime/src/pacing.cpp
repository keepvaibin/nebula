// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/pacing.h"

#include <algorithm>
#include <cmath>

namespace galaxy::pacing {

namespace {

// The p-th percentile (0..100) of values, nearest rank; 0 for none. Sorts its argument.
double percentile(std::vector<double>& values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(values.size())));
    return values[std::min(values.size() - 1, rank == 0 ? 0 : rank - 1)];
}

double average(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (const double v : values) sum += v;
    return sum / static_cast<double>(values.size());
}

}  // namespace

void Window::add_step(std::int64_t start_qpc) {
    steps_.push_back(start_qpc);
}

void Window::add_step_work(std::int64_t work_ns) {
    step_work_us_.push_back(static_cast<double>(work_ns) / 1000.0);
}

void Window::add_picture(const Picture& picture) {
    pictures_.push_back(picture);
}

void Window::clear() {
    steps_.clear();
    step_work_us_.clear();
    pictures_.clear();
    cpu_ns_ = -1;
    threads_.clear();
}

Summary Window::summarize(std::int64_t refresh_qpc, std::int64_t frequency) const {
    Summary s;
    const double us_per_tick = 1e6 / static_cast<double>(frequency);
    s.steps = steps_.size();
    if (steps_.size() >= 2) {
        s.seconds = static_cast<double>(steps_.back() - steps_.front()) / static_cast<double>(frequency);
        // Steps a second over the steps' own span: n steps begin n - 1 periods apart.
        if (s.seconds > 0.0) s.steps_per_second = static_cast<double>(steps_.size() - 1) / s.seconds;
    }
    s.pictures = pictures_.size();
    s.refresh_ms = static_cast<double>(refresh_qpc) * us_per_tick / 1000.0;

    std::vector<double> offsets;  // display - content, us
    std::vector<double> delays;
    std::vector<double> render;
    std::vector<double> generated_render;
    const Picture* last = nullptr;
    for (const Picture& p : pictures_) {
        (p.generated ? generated_render : render).push_back(static_cast<double>(p.render_ns) / 1000.0);
        if (p.generated) ++s.generated;
        if (p.displayed_qpc < 0) {
            ++s.doubled_pictures;  // the renderer knows it never reached the display
            continue;
        }
        if (p.displayed_qpc == 0) continue;
        ++s.displayed;
        offsets.push_back(static_cast<double>(p.displayed_qpc - p.content_qpc) * us_per_tick);
        if (!p.generated && p.step_qpc != 0) delays.push_back(static_cast<double>(p.displayed_qpc - p.step_qpc) * us_per_tick);
        if (last != nullptr && refresh_qpc > 0) {
            const double period = static_cast<double>(refresh_qpc);
            const long long shown = std::llround(static_cast<double>(p.displayed_qpc - last->displayed_qpc) / period);
            const long long asked = std::max(1LL, std::llround(static_cast<double>(p.asked_qpc - last->asked_qpc) / period));
            if (shown <= 0) {
                ++s.doubled_pictures;
            } else if (shown > asked) {
                s.missed_refreshes += static_cast<std::uint64_t>(shown - asked);
            } else if (shown == asked) {
                ++s.on_time;
            }
        }
        last = &p;
    }
    if (!offsets.empty()) {
        std::vector<double> sorted = offsets;
        const double median = percentile(sorted, 50.0);
        std::vector<double> errors;
        std::uint64_t within = 0;
        for (const double o : offsets) {
            const double e = std::abs(o - median);
            errors.push_back(e);
            if (e <= 1000.0) ++within;
        }
        s.animation_within_1ms = static_cast<double>(within) / static_cast<double>(offsets.size());
        s.animation_p99_us = percentile(errors, 99.0);
    }
    s.delay_avg_us = average(delays);
    s.delay_p99_us = percentile(delays, 99.0);
    s.render_avg_us = average(render);
    s.render_p99_us = percentile(render, 99.0);
    s.generated_render_avg_us = average(generated_render);
    s.generated_render_p99_us = percentile(generated_render, 99.0);
    std::vector<double> work = step_work_us_;
    s.step_work_avg_us = average(work);
    s.step_work_p99_us = percentile(work, 99.0);
    s.step_work_max_us = work.empty() ? 0.0 : work.back();  // sorted by the percentile
    if (cpu_ns_ >= 0 && !steps_.empty()) {
        s.cpu_known = true;
        s.cpu_us_per_step = static_cast<double>(cpu_ns_) / 1000.0 / static_cast<double>(steps_.size());
    }
    if (!threads_.empty() && !steps_.empty()) {
        s.work_known = true;
        for (const ThreadWorkNs& t : threads_) {
            const double us = static_cast<double>(t.ns) / 1000.0 / static_cast<double>(steps_.size());
            s.thread_cpu.push_back({t.name, us});
            s.cpu_work_us_per_step += us;
        }
    }
    return s;
}

}  // namespace galaxy::pacing
