// SPDX-License-Identifier: GPL-3.0-only
// The display's vblanks on Windows (host_window::vblank_grid): a thread waits on each vblank of the monitor the game
// window is on (IDXGIOutput::WaitForVBlank, any adapter's output) and keeps the grid on galaxy::qpc_now()'s clock.
// The monitor's mode reports a whole number of Hz (239.76 Hz reads 240), so presents timed by it drift against the
// display; the grid is the display's own.

#include "galaxy/gx/host_window.h"
#include "galaxy/platform_time.h"

#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

#pragma comment(lib, "dxgi.lib")

namespace galaxy::gx::host_window {

namespace {

using Microsoft::WRL::ComPtr;

std::mutex g_mutex;
std::int64_t g_vblank = 0;  // the latest vblank (its smoothed wake time)
std::int64_t g_period = 0;  // the interval between vblanks, smoothed; 0 while not known
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
        if (const HMONITOR wanted = g_monitor.load(std::memory_order_relaxed); wanted != monitor || !output) {
            monitor = wanted;
            output = find_output(monitor);
            last = 0;
            const std::lock_guard<std::mutex> lock(g_mutex);
            g_vblank = 0;
            g_period = 0;
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
            continue;
        }
        // Wakes come a scheduling delay after the vblank: the grid follows them through a filter, so one late
        // wake moves it by an eighth of its lateness. A wake a whole period or more late (missed vblanks) keeps
        // the period. A wake far off the grid is ignored; three in a row are a new refresh rate.
        const std::int64_t periods = std::max<std::int64_t>(1, (now - g_vblank + g_period / 2) / g_period);
        const std::int64_t error = now - (g_vblank + periods * g_period);
        if (std::llabs(error) > g_period / 4) {
            if (++off_grid >= 3) {
                off_grid = 0;
                g_period = delta;
                g_vblank = now;
            }
            continue;
        }
        off_grid = 0;
        g_vblank += periods * g_period + error / 8;
        g_period += error / (64 * periods);
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
    vblank = g_vblank;
    period = g_period;
    return period != 0;
}

}  // namespace galaxy::gx::host_window
