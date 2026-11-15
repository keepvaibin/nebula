#pragma once

// --diagnostics profile: the threads the host's sampler takes besides the game's, each written at exit to
// <name>-profile-<process>.txt in the game profile's format (tools/perf/game_profile.py, host frames by symbolizer). A
// thread adds itself when it starts; the sampler picks it up within a millisecond.

#include "galaxy/launch_args.h"

#include <array>
#include <atomic>
#include <cstddef>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#endif

namespace galaxy::profiled_threads {

inline constexpr std::size_t kMost = 8u;

struct Entry {
    std::atomic<unsigned long> id{0u};  // set last: an entry is complete once its id is
    const char* name = nullptr;         // static storage
};

inline std::array<Entry, kMost> g_entries{};
inline std::atomic<std::size_t> g_claimed{0u};

// The calling thread, sampled from now on under --diagnostics profile (nothing otherwise). `name`: static storage, a
// file name's part.
inline void add_current(const char* name) noexcept {
#if defined(_WIN32)
    if (!::galaxy::diagnosing(::galaxy::kDiagnoseProfile)) return;
    const std::size_t index = g_claimed.fetch_add(1u, std::memory_order_relaxed);
    if (index >= kMost) return;
    g_entries[index].name = name;
    g_entries[index].id.store(GetCurrentThreadId(), std::memory_order_release);
#else
    static_cast<void>(name);
#endif
}

}  // namespace galaxy::profiled_threads
