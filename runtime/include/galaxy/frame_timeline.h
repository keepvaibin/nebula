// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Frame pipeline timeline, recorded with --diagnostics timeline, on every platform. Any thread appends a 32-byte
// record with one relaxed atomic increment and one clock read. Nothing is formatted during the run. At exit the
// records go to --timeline's file (default the diagnostics folder's timeline-<pid>.bin), analysed offline by
// tools/perf/timeline_digest.py. Timestamps are ticks of galaxy::qpc_now() (platform_time.h; the header gives the
// ticks a second): QueryPerformanceCounter on Windows, CLOCK_MONOTONIC nanoseconds on Linux and Android,
// CLOCK_UPTIME_RAW nanoseconds on Apple's systems. GPU timestamps and vblank times are in the same ticks.

#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#elif defined(__APPLE__)
#include <pthread.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace galaxy::timeline {

enum class Event : std::uint16_t {
    SubmitBegin = 1,       // game: a frame's records handed to the renderer
    SubmitEnd = 2,         // game: handed over
    RenderBegin = 4,       // render: chunk dequeued
    RenderSubmitted = 6,   // render: command list executed and fence signaled. b=fence
    GpuBegin = 7,          // GPU timestamp at command-list start (QPC domain). a=fence
    GpuEnd = 8,            // GPU timestamp at command-list end (QPC domain). a=fence
    PresentBegin = 14,     // render: IDXGISwapChain::Present call. a=present ordinal, b=sync interval
    PresentEnd = 15,       // render: Present returned. a=present ordinal
    DisplayStats = 16,     // render: DXGI frame statistics. qpc=SyncQPCTime, a=PresentCount<<32|PresentRefreshCount
    DisplayCopy = 21,      // render: display picture copy executed. a=destination key
    SlotWaitBegin = 22,    // render: waiting for a frame slot's fence. a=fence
    SlotWaitEnd = 23,
    VBlank = 24,           // render: DWM composition timing. qpc=last vblank, a=refresh period (QPC ticks)
    DisplayPresented = 25, // render: display picture chosen for a present. a=slot, b=picture number
    GameFrame = 26,        // game: frame boundary marker. a=frame count
    ListOpsBegin = 27,     // render: a new list's queued registry and texture work begins. a=list serial
    ListOpsEnd = 28,
    WalkBegin = 29,        // render: a list walked for a picture. a=list serial, b=1 for a picture between game steps
    WalkEnd = 30,
    StepWaitBegin = 31,    // game: the step clock's wait for the next step begins. a=the step's due time (QPC)
    StepWaitEnd = 32,      // game: the wait returned
    ContentTime = 33,      // render: a picture of an interpolation sequence. qpc=the moment it shows, a=its list's game
                           // frame, b=t (0 the frame before, 1 this one) in millionths
    AcquireBegin = 34,     // render: waiting for a swap image (vkAcquireNextImageKHR, Metal's nextDrawable, Direct3D's
                           // frame latency object)
    AcquireEnd = 35,       // render: the wait returned. a=the image's index (Metal: 0), b=1 when it gave none
    GateWaitBegin = 36,    // render, Direct3D 12: a Present waits for pipeline creations in flight (core/present_gate.h).
                           // a=creations in flight
    GateWaitEnd = 37,
    PipelineMissed = 38,   // render: a draw found its pipeline not made yet (core/pipelines.cpp, missing): left out, or
                           // waited for with fixed steps, and made at the workers' queue's front. a=pipeline id, b=its key
    DisplayModel = 39,     // render, Direct3D and Vulkan: a present of a picture drawn for a refresh (dxgi_pacing.h; on
                           // Vulkan the refresh it asked for with present timing, renderer_vk.cpp). qpc=that refresh,
                           // a=the present's count (DisplayStats' PresentCount) or id, b=when it was ready to present
};

struct Record {
    std::uint64_t qpc;
    std::uint64_t a;
    std::uint64_t b;
    std::uint32_t thread;
    std::uint16_t event;
    std::uint16_t valid;
};
static_assert(sizeof(Record) == 32);

struct State {
    Record* records = nullptr;
    std::uint64_t capacity = 0;
    std::atomic<std::uint64_t> next{0};
    bool enabled = false;
};

inline State g_state;

[[nodiscard]] inline bool enabled() noexcept { return g_state.enabled; }

[[nodiscard]] inline std::uint64_t now() noexcept { return static_cast<std::uint64_t>(::galaxy::qpc_now()); }

// The calling thread's id as the system's tools show it (32 bits; a record's thread).
[[nodiscard]] inline std::uint32_t thread_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentThreadId());
#else
    // One system call a thread, then a thread-local read (constant initialised: no guard, nothing run at load).
    thread_local std::uint32_t id = 0u;
    if (id == 0u) {
#if defined(__APPLE__)
        std::uint64_t tid = 0u;
        (void)pthread_threadid_np(nullptr, &tid);
        id = static_cast<std::uint32_t>(tid);
#else
        id = static_cast<std::uint32_t>(syscall(SYS_gettid));
#endif
    }
    return id;
#endif
}

inline void record_at(Event event, std::uint64_t qpc, std::uint64_t a = 0u,
                      std::uint64_t b = 0u) noexcept {
    if (!g_state.enabled) return;
    const std::uint64_t index = g_state.next.fetch_add(1u, std::memory_order_relaxed);
    if (index >= g_state.capacity) return;
    Record& r = g_state.records[index];
    r.qpc = qpc;
    r.a = a;
    r.b = b;
    r.thread = thread_id();
    r.event = static_cast<std::uint16_t>(event);
    r.valid = 1u;
}

inline void record(Event event, std::uint64_t a = 0u, std::uint64_t b = 0u) noexcept {
    if (!g_state.enabled) return;
    record_at(event, now(), a, b);
}

// Called once on the main thread before any worker thread starts.
inline void initialize() noexcept {
    if (!::galaxy::diagnosing(::galaxy::kDiagnoseTimeline)) {
        return;
    }
    // Events: 128 MB of address space, its pages taken as records fill them; about four minutes of an uncapped run
    // at 2000 pictures a second (seven events a picture).
    constexpr std::uint64_t capacity = 4'000'000u;
    void* memory = std::malloc(static_cast<std::size_t>(capacity * sizeof(Record)));
    if (memory == nullptr) return;
    g_state.records = static_cast<Record*>(memory);
    g_state.capacity = capacity;
    g_state.enabled = true;
    std::fprintf(stderr, "[frame-timeline] enabled capacity=%llu\n",
                 static_cast<unsigned long long>(capacity));
}

// Writes the records once, at orderly exit, after the worker threads stopped.
inline void dump() noexcept {
    if (!g_state.enabled) return;
    g_state.enabled = false;
    std::filesystem::path path;
    try {
        if (!::galaxy::launch_args().timeline.empty()) {
            path = ::galaxy::launch_args().timeline;
        } else {
            const std::filesystem::path folder = ::galaxy::nebula_diagnostics_directory();
            if (folder.empty()) return;
            path = folder / ("timeline-" + std::to_string(::galaxy::current_process_id()) + ".bin");
        }
    } catch (...) {
        return;
    }
    // The path as UTF-8 for the log line (C++20's u8string is char8_t text).
    const std::u8string shown = path.generic_u8string();
    const char* const shown_text = reinterpret_cast<const char*>(shown.c_str());
    std::FILE* const file = ::galaxy::open_path(path, "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "[frame-timeline] cannot create %s\n", shown_text);
        return;
    }
    const std::uint64_t written_count = g_state.next.load(std::memory_order_acquire);
    const std::uint64_t count = written_count < g_state.capacity ? written_count : g_state.capacity;
    std::uint64_t header[8]{};
    std::memcpy(&header[0], "NBTL0001", 8);
    header[1] = 1u;  // format version
    header[2] = static_cast<std::uint64_t>(::galaxy::qpc_frequency());
    header[3] = count;
    header[4] = written_count;
    header[5] = ::galaxy::current_process_id();
    bool ok = std::fwrite(header, sizeof(header), 1u, file) == 1u;
    if (ok && count != 0u) {
        ok = std::fwrite(g_state.records, sizeof(Record), static_cast<std::size_t>(count), file) == count;
    }
    ok = std::fclose(file) == 0 && ok;
    std::fprintf(stderr, "[frame-timeline] wrote %llu of %llu events to %s ok=%d\n",
                 static_cast<unsigned long long>(count), static_cast<unsigned long long>(written_count),
                 shown_text, ok ? 1 : 0);
}

}  // namespace galaxy::timeline
