// SPDX-License-Identifier: GPL-3.0-only
// The display's vblanks on Windows (host_window::vblank_grid): a thread waits on each vblank of the monitor the game
// window is on (IDXGIOutput::WaitForVBlank, any adapter's output) and keeps the grid on galaxy::qpc_now()'s clock.
// The monitor's mode reports a whole number of Hz (239.76 Hz reads 240), so presents timed by it drift against the
// display; the grid is the display's own.

#include "galaxy/render/host_window.h"
#include "galaxy/platform_time.h"

#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

#pragma comment(lib, "dxgi.lib")

namespace galaxy::render::host_window {

namespace {

using Microsoft::WRL::ComPtr;

std::mutex g_mutex;
std::condition_variable g_asked;
std::int64_t g_last_asked = 0;  // nobody for two seconds: the thread sleeps until someone asks
std::int64_t g_vblank = 0;  // the latest vblank (its smoothed wake time)
std::int64_t g_period = 0;  // the interval between vblanks; 0 while not known

// The period from the wakes themselves: a least-squares line through the last kFitWakes (vblank number, wake time) pairs.
// A wake's scheduling delay is noise around the line, which a fit over a second of vblanks averages to a ten-thousandth of
// a period (a 240 Hz display reads 240.06, not 239.9 to 240.1 from one wake to the next); the phase follows the latest
// wakes through its own filter. Under kFitLeast wakes the filter's period stands.
constexpr std::size_t kFitWakes = 256u;
constexpr std::size_t kFitLeast = 32u;
struct FitWake {
    std::int64_t number = 0;  // vblanks since the fit began
    std::int64_t qpc = 0;
};
std::array<FitWake, kFitWakes> g_fit{};
std::size_t g_fit_count = 0;
std::size_t g_fit_next = 0;
std::int64_t g_fit_number = 0;

void fit_reset() noexcept {
    g_fit_count = g_fit_next = 0;
    g_fit_number = 0;
}

// The wake `periods` vblanks after the one before; the fitted period, or 0 while there are too few wakes.
std::int64_t fit_add(std::int64_t periods, std::int64_t qpc) noexcept {
    g_fit_number += periods;
    g_fit[g_fit_next] = {g_fit_number, qpc};
    g_fit_next = (g_fit_next + 1u) % kFitWakes;
    g_fit_count = std::min(g_fit_count + 1u, kFitWakes);
    if (g_fit_count < kFitLeast) {
        return 0;
    }
    // Relative to the newest wake, in doubles: QPC values are too large to square.
    const FitWake& base = g_fit[(g_fit_next + kFitWakes - 1u) % kFitWakes];
    double sn = 0.0, st = 0.0;
    for (std::size_t i = 0; i < g_fit_count; ++i) {
        sn += static_cast<double>(g_fit[i].number - base.number);
        st += static_cast<double>(g_fit[i].qpc - base.qpc);
    }
    const double n = static_cast<double>(g_fit_count);
    const double mn = sn / n, mt = st / n;
    double snn = 0.0, snt = 0.0;
    for (std::size_t i = 0; i < g_fit_count; ++i) {
        const double dn = static_cast<double>(g_fit[i].number - base.number) - mn;
        snn += dn * dn;
        snt += dn * (static_cast<double>(g_fit[i].qpc - base.qpc) - mt);
    }
    return snn > 0.0 ? static_cast<std::int64_t>(std::llround(snt / snn)) : 0;
}
std::atomic<HMONITOR> g_monitor{nullptr};
std::once_flag g_started;

ComPtr<IDXGIOutput> find_output(HMONITOR monitor) {
    ComPtr<IDXGIFactory1> factory;
    if (monitor == nullptr || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return {};
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) {
                return output;
            }
        }
    }
    return {};
}

void vblank_thread() {
    SetThreadDescription(GetCurrentThread(), L"Nebula Vblank");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    const std::int64_t frequency = galaxy::qpc_frequency();
    HMONITOR monitor = nullptr;
    ComPtr<IDXGIOutput> output;
    std::int64_t last = 0;
    unsigned off_grid = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_asked.wait(lock, [frequency] { return galaxy::qpc_now() - g_last_asked < frequency * 2; });
        }
        if (const HMONITOR wanted = g_monitor.load(std::memory_order_relaxed); wanted != monitor || !output) {
            monitor = wanted;
            output = find_output(monitor);
            last = 0;
            const std::lock_guard<std::mutex> lock(g_mutex);
            g_vblank = 0;
            g_period = 0;
            fit_reset();
        }
        if (!output || FAILED(output->WaitForVBlank())) {
            output.Reset();
            Sleep(250);
            continue;
        }
        const std::int64_t now = galaxy::qpc_now();
        const std::lock_guard<std::mutex> lock(g_mutex);
        const std::int64_t delta = last != 0 ? now - last : 0;
        last = now;
        if (delta < frequency / 1000 || delta > frequency / 20) {
            continue;  // the first wake, or a pause (display off, a mode change)
        }
        if (g_period == 0) {
            g_period = delta;
            g_vblank = now;
            fit_reset();
            fit_add(0, now);
            continue;
        }
        // Wakes come a scheduling delay after the vblank: the grid follows them through a filter, so one late
        // wake moves it by an eighth of its lateness. A wake a whole period or more late (missed vblanks) counts
        // them. A wake far off the grid is ignored; three in a row are a new refresh rate.
        const std::int64_t periods = std::max<std::int64_t>(1, (now - g_vblank + g_period / 2) / g_period);
        const std::int64_t error = now - (g_vblank + periods * g_period);
        if (std::llabs(error) > g_period / 4) {
            if (++off_grid >= 3) {
                off_grid = 0;
                g_period = delta;
                g_vblank = now;
                fit_reset();
                fit_add(0, now);
            }
            continue;
        }
        off_grid = 0;
        g_vblank += periods * g_period + error / 8;
        if (const std::int64_t fitted = fit_add(periods, now); fitted > 0) {
            g_period = fitted;
        } else {
            g_period += error / (64 * periods);
        }
    }
}

}  // namespace

bool vblank_grid(NativeWindow window, std::int64_t& vblank, std::int64_t& period) noexcept {
    g_monitor.store(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), std::memory_order_relaxed);
    try {
        std::call_once(g_started, [] { std::thread(vblank_thread).detach(); });
    } catch (...) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(g_mutex);
    const std::int64_t now = galaxy::qpc_now();
    if (now - g_last_asked >= galaxy::qpc_frequency()) {
        g_last_asked = now;
        g_asked.notify_one();
    }
    vblank = g_vblank;
    period = g_period;
    return period != 0;
}

// The monitor's vblanks are timed here: a present's time adds nothing.
void note_present_shown(std::int64_t, bool) noexcept {}

bool vblank_grid_known(std::int64_t& vblank, std::int64_t& period) noexcept {
    const std::lock_guard<std::mutex> lock(g_mutex);
    vblank = g_vblank;
    period = g_period;
    return period != 0;
}

}  // namespace galaxy::render::host_window
