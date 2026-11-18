// SPDX-License-Identifier: GPL-3.0-only
// The system's heat (thermal_state.h).
#include "galaxy/thermal_state.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>

#if defined(__ANDROID__)
#include <android/thermal.h>
#elif defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace galaxy::host {

const char* heat_name(Heat heat) noexcept {
    switch (heat) {
        case Heat::Unknown: return "unknown";
        case Heat::None: return "none";
        case Heat::Light: return "light";
        case Heat::Moderate: return "moderate";
        case Heat::Severe: return "severe";
        case Heat::Critical: return "critical";
        case Heat::Emergency: return "emergency";
        case Heat::Shutdown: return "shutdown";
    }
    return "unknown";
}

namespace {

constexpr float kNoHeadroom = std::numeric_limits<float>::quiet_NaN();

std::atomic<Heat> g_heat{Heat::Unknown};
std::atomic<bool> g_listened{false};  // a listener keeps g_heat (Android)

std::mutex g_read_mutex;  // the reads below and their times
std::chrono::steady_clock::time_point g_heat_read{};
bool g_heat_ever = false;
std::chrono::steady_clock::time_point g_headroom_read{};
bool g_headroom_ever = false;
std::atomic<float> g_headroom{kNoHeadroom};  // written under g_read_mutex, read anywhere

// The heat as now known; logged when it differs from the last.
void take(Heat heat) noexcept {
    const Heat before = g_heat.exchange(heat, std::memory_order_acq_rel);
    if (before == heat || heat == Heat::Unknown) return;
    const float headroom = g_headroom.load(std::memory_order_relaxed);  // the last read, not asked again
    if (std::isnan(headroom)) {
        std::fprintf(stderr, "[heat] %s\n", heat_name(heat));
    } else {
        std::fprintf(stderr, "[heat] %s (headroom %.2f)\n", heat_name(heat), static_cast<double>(headroom));
    }
}

#if defined(__ANDROID__)
// One manager for the process, never released (its listener lives as long).
AThermalManager* manager() noexcept {
    static AThermalManager* const made = AThermal_acquireManager();
    return made;
}

Heat read_heat() noexcept {
    AThermalManager* const thermal = manager();
    return thermal != nullptr ? heat_of_android_status(static_cast<int>(AThermal_getCurrentThermalStatus(thermal)))
                              : Heat::Unknown;
}

float read_headroom() noexcept {
    AThermalManager* const thermal = manager();
    return thermal != nullptr ? AThermal_getThermalHeadroom(thermal, 0) : kNoHeadroom;
}

void changed(void*, AThermalStatus status) {
    take(heat_of_android_status(static_cast<int>(status)));
}

void listen() noexcept {
    AThermalManager* const thermal = manager();
    if (thermal != nullptr && AThermal_registerThermalStatusListener(thermal, &changed, nullptr) == 0) {
        g_listened.store(true, std::memory_order_release);
    }
}
#elif defined(__APPLE__)
// NSProcessInfo.processInfo.thermalState, through the Objective-C runtime (this file is C++).
Heat read_heat() noexcept {
    Class const type = objc_getClass("NSProcessInfo");
    if (type == nullptr) return Heat::Unknown;
    using Shared = id (*)(Class, SEL);
    using State = long (*)(id, SEL);
    id const info = reinterpret_cast<Shared>(objc_msgSend)(type, sel_registerName("processInfo"));
    if (info == nullptr) return Heat::Unknown;
    return heat_of_apple_state(reinterpret_cast<State>(objc_msgSend)(info, sel_registerName("thermalState")));
}

float read_headroom() noexcept {
    return kNoHeadroom;
}

void listen() noexcept {}
#else
Heat read_heat() noexcept {
    return Heat::Unknown;
}

float read_headroom() noexcept {
    return kNoHeadroom;
}

void listen() noexcept {}
#endif

// Reads the system's heat when `force` or a second has passed since the last read.
void refresh_heat(bool force) noexcept {
    Heat heat = Heat::Unknown;
    {
        const auto now = std::chrono::steady_clock::now();
        const std::lock_guard<std::mutex> lock(g_read_mutex);
        if (!force && g_heat_ever && now - g_heat_read < std::chrono::seconds(1)) return;
        g_heat_ever = true;
        g_heat_read = now;
        heat = read_heat();
    }
    take(heat);
}

}  // namespace

void thermal_watch_start() noexcept {
    (void)thermal_headroom();
    refresh_heat(true);
    listen();
}

void thermal_heat_changed() noexcept {
    refresh_heat(true);
}

Heat thermal_heat() noexcept {
    if (!g_listened.load(std::memory_order_acquire)) refresh_heat(false);
    return g_heat.load(std::memory_order_acquire);
}

float thermal_headroom() noexcept {
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(g_read_mutex);
    if (!g_headroom_ever || now - g_headroom_read >= std::chrono::seconds(1)) {
        g_headroom_ever = true;
        g_headroom_read = now;
        // Asked too soon, the system answers not a number: the last answer stands.
        const float read = read_headroom();
        if (!std::isnan(read)) g_headroom.store(read, std::memory_order_relaxed);
    }
    return g_headroom.load(std::memory_order_relaxed);
}

}  // namespace galaxy::host
