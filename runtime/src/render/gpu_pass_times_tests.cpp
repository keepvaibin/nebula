// SPDX-License-Identifier: GPL-3.0-only
// Tests of the GPU's time by pass (gpu_pass_times.h, P-022), the one copy every renderer uses: the billing of each
// interval between two timestamps, the settings page's running averages and their clearing, the logs' window, and the
// benchmark's hook.
#include "galaxy/render/gpu_pass_times.h"

#include "galaxy/benchmark.h"
#include "galaxy/render_api.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace galaxy::render;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-6;
}

void test_billing() {
    // Scene from 0 to 10, bloom from 10 to 14, the scene again (a scope's end) from 14 to 20, present 20 to 21.
    const std::uint64_t ticks[] = {0u, 10u, 14u, 20u, 21u};
    const GpuPass marks[] = {GpuPass::Scene, GpuPass::Bloom, GpuPass::Scene, GpuPass::Present};
    const GpuPassMs ms = gpu_pass_ms(ticks, marks, 4u, 0.5);
    check(near(ms[static_cast<std::size_t>(GpuPass::Scene)], 8.0), "an interval goes to the pass that began at its start");
    check(near(ms[static_cast<std::size_t>(GpuPass::Bloom)], 2.0), "a scope's interval is its own");
    check(near(ms[static_cast<std::size_t>(GpuPass::Present)], 0.5), "the last interval ends at the last timestamp");
    check(near(ms[static_cast<std::size_t>(GpuPass::Resolve)], 0.0), "a pass not marked has no time");

    // A counter that went back (a reset) skips its interval; a mark of Count bills nothing.
    const std::uint64_t back[] = {100u, 90u, 95u, 99u};
    const GpuPass marks2[] = {GpuPass::Scene, GpuPass::Count, GpuPass::Bloom};
    const GpuPassMs ms2 = gpu_pass_ms(back, marks2, 3u, 1.0);
    check(near(ms2[static_cast<std::size_t>(GpuPass::Scene)], 0.0), "an interval that goes backwards is skipped");
    check(near(ms2[static_cast<std::size_t>(GpuPass::Bloom)], 4.0), "the interval after a skipped one is billed");
    double sum = 0.0;
    for (double v : ms2) sum += v;
    check(near(sum, 4.0), "a mark of Count bills nothing");
    check(gpu_pass_ms(back, marks2, 0u, 1.0) == GpuPassMs{}, "no intervals, no time");
}

void test_names() {
    for (std::size_t pass = 0u; pass < kGpuPassCount; ++pass) {
        check(kGpuPassPageNames[pass] != nullptr && kGpuPassLogNames[pass] != nullptr, "every pass has both names");
        check(std::strchr(kGpuPassLogNames[pass], ' ') == nullptr, "a log name has no space");
    }
    check(std::strcmp(kGpuPassPageNames[static_cast<std::size_t>(GpuPass::FramePass)], "frame read") == 0, "the page's name");
    check(std::strcmp(kGpuPassLogNames[static_cast<std::size_t>(GpuPass::FramePass)], "frame-read") == 0, "the log's name");
}

GpuPassMs frame(double scene, double bloom) {
    GpuPassMs ms{};
    ms[static_cast<std::size_t>(GpuPass::Scene)] = scene;
    ms[static_cast<std::size_t>(GpuPass::Bloom)] = bloom;
    return ms;
}

void test_averages() {
    GpuPassTimes times;
    GpuPassTime out[kGpuPassCount]{};
    check(times.page_times(out, kGpuPassCount) == 0u, "no frame, no time on the page");
    check(times.begin_frame(true) && times.timed(), "a frame asked to time is timed");
    times.add_frame(frame(4.0, 0.0), true);
    check(times.page_times(out, kGpuPassCount) == 1u && std::strcmp(out[0].name, "scene") == 0 && near(out[0].ms, 4.0),
          "the first frame is taken as it is, and only the passes that ran are listed");
    times.add_frame(frame(14.0, 2.0), true);
    check(times.page_times(out, kGpuPassCount) == 2u && near(out[0].ms, 5.0) && near(out[1].ms, 2.0),
          "each later frame moves the average a tenth of the way; a pass's first frame is its own");
    times.add_frame(frame(5.0, 0.0), true);
    check(times.page_times(out, kGpuPassCount) == 2u && near(out[1].ms, 2.0), "a frame without the pass leaves its average");
    check(times.page_times(out, 1u) == 1u, "at most the capacity asked");

    check(!times.begin_frame(false) && !times.timed(), "a frame not asked to time is not");
    check(times.page_times(out, kGpuPassCount) == 0u, "timing turned off clears the page");
    times.begin_frame(true);
    times.add_frame(frame(1.0, 0.0), true);
    check(times.page_times(out, kGpuPassCount) == 1u && near(out[0].ms, 1.0), "timing again starts the average afresh");
}

void test_window() {
    GpuPassTimes times;
    check(times.take_window_field().empty() && times.take_window_list().empty(), "no frame timed: nothing to log");
    times.begin_frame(true);
    times.add_frame(frame(2.0, 1.0), true);
    times.add_frame(frame(4.0, 0.0), false);
    check(times.window_frames() == 2u, "the window counts the timed frames");
    const std::string field = times.take_window_field();
    check(field == " gpu-pass-ms=scene:3.000,bloom:0.500", "the present statistics' field: ms a timed frame, the passes that ran");
    check(times.window_frames() == 0u && times.take_window_field().empty(), "taking the window starts it again");
    times.add_frame(frame(1.0, 0.0), true);
    check(times.take_window_list() == " scene=1.000", "the list form");
}

const char* const* g_names = nullptr;
std::size_t g_count = 0u;
double g_scene = -1.0;
bool g_game = false;

void hook(const char* const* names, const double* ms, std::size_t count, bool game_frame) {
    g_names = names;
    g_count = count;
    g_scene = ms[static_cast<std::size_t>(GpuPass::Scene)];
    g_game = game_frame;
}

void test_benchmark_hook() {
    galaxy::bench::g_gpu_frame_hook.store(hook, std::memory_order_release);
    GpuPassTimes times;
    times.begin_frame(true);
    times.add_frame(frame(3.0, 0.0), false);
    check(g_names == kGpuPassPageNames && g_count == kGpuPassCount && near(g_scene, 3.0) && !g_game,
          "the benchmark sees each frame's passes by the page's names, and whether it was a game step's");
    galaxy::bench::g_gpu_frame_hook.store(nullptr, std::memory_order_release);
}

void test_frame_time() {
    FrameGpuTime time;
    check(time.scene_ema_ms() == 0.0 && time.generated_ema_ms() == 0.0, "no frame, no time");
    time.add(10.0, true, false);
    check(near(time.scene_ema_ms(), 10.0) && time.generated_ema_ms() == 0.0, "the first scene frame is taken as it is");
    time.add(20.0, true, false);
    check(near(time.scene_ema_ms(), 11.0), "a scene frame moves the scene's average a tenth of the way");
    time.add(5.0, false, false);
    check(near(time.scene_ema_ms(), 11.0), "a frame without the scene leaves the scene's average");
    time.add(4.0, false, true);
    time.add(8.0, true, true);
    check(near(time.generated_ema_ms(), 4.4) && near(time.scene_ema_ms(), 10.7),
          "a generated frame moves its own average, and the scene's when it drew the scene");
    double total = 0.0;
    std::uint64_t samples = 0u;
    time.take_generated(total, samples);
    check(near(total, 12.0) && samples == 2u, "the generated frames' total and count");
    time.take_generated(total, samples);
    check(total == 0.0 && samples == 0u, "taken, the total starts again");
    time.reset_generated_ema();
    time.add(6.0, false, true);
    check(near(time.generated_ema_ms(), 6.0), "after a reset the next generated frame is taken as it is");
    time.reset_scene_ema();
    check(time.scene_ema_ms() == 0.0 && near(time.generated_ema_ms(), 6.0), "the scene's reset leaves the generated average");
}

}  // namespace

int main() {
    test_billing();
    test_names();
    test_averages();
    test_window();
    test_benchmark_hook();
    test_frame_time();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("gpu pass times: all passed\n");
    return 0;
}
