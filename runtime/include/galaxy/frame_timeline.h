// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Frame pipeline timeline, enabled by GALAXY_FRAME_TIMELINE=1. Any thread appends a 32-byte
// record with one relaxed atomic increment and one QueryPerformanceCounter read. Nothing is
// formatted during the run. At exit the records go to GALAXY_FRAME_TIMELINE_PATH (default
// %LOCALAPPDATA%\Nebula\timeline-<pid>.bin), analysed offline by tools/perf/timeline_digest.py.
// All timestamps are QPC ticks, including GPU timestamps (via GetClockCalibration) and vblank
// times (DXGI frame statistics, DWM timing).

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "galaxy/run_options.h"
#include <cwchar>
#include <Windows.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace galaxy::timeline {

enum class Event : std::uint16_t {
    SubmitBegin = 1,       // game: a frame's records handed to the renderer. a=bytes
    SubmitEnd = 2,         // game: handed over. a=token value, b=token kind
    RenderBegin = 4,       // render: chunk dequeued. a=token value, b=bytes
    RenderSubmitted = 6,   // render: command list executed and fence signaled. a=token value, b=fence
    GpuBegin = 7,          // GPU timestamp at command-list start (QPC domain). a=fence
    GpuEnd = 8,            // GPU timestamp at command-list end (QPC domain). a=fence
    PresentBegin = 14,     // render: IDXGISwapChain::Present call. a=present ordinal, b=sync interval
    PresentEnd = 15,       // render: Present returned. a=present ordinal
    DisplayStats = 16,     // render: DXGI frame statistics. qpc=SyncQPCTime, a=PresentCount<<32|PresentRefreshCount
    DisplayCopy = 21,      // render: display picture copy executed. a=destination key
    SlotWaitBegin = 22,    // render: waiting for a frame slot's fence. a=fence
    SlotWaitEnd = 23,
    VBlank = 24,           // render: DWM composition timing. qpc=last vblank, a=refresh period (QPC ticks)
    DisplayPresented = 25, // render: display picture chosen for a present. a=key
    GameFrame = 26,        // game: frame boundary marker. a=frame count
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

[[nodiscard]] inline std::uint64_t now() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<std::uint64_t>(t.QuadPart);
#else
    return 0u;
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
#if defined(_WIN32)
    r.thread = GetCurrentThreadId();
#else
    r.thread = 0u;
#endif
    r.event = static_cast<std::uint16_t>(event);
    r.valid = 1u;
}

inline void record(Event event, std::uint64_t a = 0u, std::uint64_t b = 0u) noexcept {
    if (!g_state.enabled) return;
    record_at(event, now(), a, b);
}

#if defined(_WIN32)
// Called once on the main thread before any worker thread starts.
inline void initialize_from_environment() noexcept {
    char flag[8]{};
    std::size_t length = 0;
    if (::galaxy::run_option_read(&length, flag, sizeof(flag), ::galaxy::run_options().frame_timeline) != 0 || length < 2 ||
        flag[0] == '0') {
        return;
    }
    std::uint64_t capacity = 2'000'000u;  // 64 MB, about 25 minutes at 60 frames/s
    char count[32]{};
    if (::galaxy::run_option_read(&length, count, sizeof(count), ::galaxy::run_options().frame_timeline_events) == 0 && length > 1) {
        const unsigned long long parsed = std::strtoull(count, nullptr, 10);
        if (parsed >= 1000u && parsed <= 50'000'000u) capacity = parsed;
    }
    void* memory = VirtualAlloc(nullptr, static_cast<SIZE_T>(capacity * sizeof(Record)),
                                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
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
    wchar_t path[MAX_PATH]{};
    const wchar_t* const named = ::galaxy::run_options().frame_timeline_path_w;
    DWORD length = named != nullptr ? static_cast<DWORD>(std::wcslen(named)) : 0u;
    if (length != 0u && length < MAX_PATH) {
        std::wmemcpy(path, named, length + 1u);
    }
    if (length == 0u || length >= MAX_PATH) {
        wchar_t base[MAX_PATH]{};
        const DWORD base_length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (base_length == 0u || base_length >= MAX_PATH) return;
        swprintf_s(path, L"%s\\Nebula", base);
        (void)CreateDirectoryW(path, nullptr);
        swprintf_s(path, L"%s\\Nebula\\timeline-%lu.bin", base, GetCurrentProcessId());
    }
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "[frame-timeline] cannot create %ls error=%lu\n", path, GetLastError());
        return;
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const std::uint64_t written_count = g_state.next.load(std::memory_order_acquire);
    const std::uint64_t count = written_count < g_state.capacity ? written_count : g_state.capacity;
    std::uint64_t header[8]{};
    std::memcpy(&header[0], "NBTL0001", 8);
    header[1] = 1u;  // format version
    header[2] = static_cast<std::uint64_t>(frequency.QuadPart);
    header[3] = count;
    header[4] = written_count;
    header[5] = GetCurrentProcessId();
    DWORD bytes = 0;
    BOOL ok = WriteFile(file, header, sizeof(header), &bytes, nullptr);
    const auto* data = reinterpret_cast<const unsigned char*>(g_state.records);
    std::uint64_t remaining = count * sizeof(Record);
    while (ok && remaining != 0u) {
        const DWORD chunk = remaining > (1u << 26) ? (1u << 26) : static_cast<DWORD>(remaining);
        ok = WriteFile(file, data, chunk, &bytes, nullptr);
        data += chunk;
        remaining -= chunk;
    }
    CloseHandle(file);
    std::fprintf(stderr, "[frame-timeline] wrote %llu of %llu events to %ls ok=%d\n",
                 static_cast<unsigned long long>(count),
                 static_cast<unsigned long long>(written_count), path, ok ? 1 : 0);
}
#else
inline void initialize_from_environment() noexcept {}
inline void dump() noexcept {}
#endif

}  // namespace galaxy::timeline
