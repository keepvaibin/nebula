// SPDX-License-Identifier: GPL-3.0-only
// Where the busy threads spend their time, without a profiler (sdl_host.h, start_thread_sampler): Android only, when
// `adb shell setprop debug.nebula.profile <seconds>` asks for it.

#include "sdl_host.h"

#if defined(__ANDROID__)
#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#endif

namespace galaxy::sdl {

#if defined(__ANDROID__)

namespace {

constexpr std::size_t kMaxSamples = 8192;
// Each sample's instruction pointer and the first caller outside libc on its stack (unwound from the signal frame),
// so a sample inside memcpy names who copies.
std::atomic<std::uintptr_t> g_samples[kMaxSamples];
std::atomic<std::uintptr_t> g_callers[kMaxSamples];
std::atomic<std::uint32_t> g_sample_count{0};

std::uintptr_t g_libc_begin = 0;
std::uintptr_t g_libc_end = 0;

struct Walk {
    std::uintptr_t frames[8];
    int count;
};

_Unwind_Reason_Code walk_frame(_Unwind_Context* context, void* data) {
    auto* walk = static_cast<Walk*>(data);
    walk->frames[walk->count++] = static_cast<std::uintptr_t>(_Unwind_GetIP(context));
    return walk->count < 8 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void on_sample(int, siginfo_t*, void* context) {
    const auto* uc = static_cast<const ucontext_t*>(context);
#if defined(__aarch64__)
    const std::uintptr_t pc = uc->uc_mcontext.pc;
#elif defined(__x86_64__)
    const std::uintptr_t pc = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#else
    const std::uintptr_t pc = 0;
#endif
    // The handler's own frames come first; the interrupted code's start after the signal trampoline. The first frame
    // past the sampled PC that lies outside libc is the caller.
    Walk walk{};
    _Unwind_Backtrace(walk_frame, &walk);
    std::uintptr_t caller = 0;
    bool past_pc = false;
    for (int i = 0; i < walk.count; ++i) {
        const std::uintptr_t frame = walk.frames[i];
        if (!past_pc) {
            past_pc = frame >= pc - 1 && frame <= pc + 1;
            continue;
        }
        if (frame < g_libc_begin || frame >= g_libc_end) {
            caller = frame;
            break;
        }
    }
    const std::uint32_t at = g_sample_count.fetch_add(1u, std::memory_order_relaxed);
    if (at < kMaxSamples) {
        g_samples[at].store(pc, std::memory_order_relaxed);
        g_callers[at].store(caller, std::memory_order_relaxed);
    }
}

// "library symbol" and the address's offset in its library, for addr2line.
std::string place_of(std::uintptr_t pc, std::uintptr_t& offset) {
    Dl_info info{};
    offset = 0;
    if (pc == 0u || dladdr(reinterpret_cast<void*>(pc), &info) == 0 || info.dli_fname == nullptr) {
        return "?";
    }
    const char* file = std::strrchr(info.dli_fname, '/');
    offset = pc - reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    char text[256];
    std::snprintf(text, sizeof(text), "%s %s", file != nullptr ? file + 1 : info.dli_fname,
                  info.dli_sname != nullptr ? info.dli_sname : "?");
    return text;
}

// The process's threads whose names start with `prefix`.
std::vector<std::pair<pid_t, std::string>> threads_named(const char* prefix) {
    std::vector<std::pair<pid_t, std::string>> found;
    DIR* tasks = opendir("/proc/self/task");
    if (tasks == nullptr) {
        return found;
    }
    while (const dirent* entry = readdir(tasks)) {
        const pid_t tid = static_cast<pid_t>(std::atoi(entry->d_name));
        if (tid <= 0) {
            continue;
        }
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
        if (std::FILE* file = std::fopen(path, "r")) {
            char name[32]{};
            if (std::fgets(name, sizeof(name), file) != nullptr) {
                name[std::strcspn(name, "\n")] = '\0';
                if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
                    found.emplace_back(tid, name);
                }
            }
            std::fclose(file);
        }
    }
    closedir(tasks);
    return found;
}

void sample(pid_t tid, const std::string& name) {
    g_sample_count.store(0u, std::memory_order_relaxed);
    const pid_t pid = getpid();
    for (int i = 0; i < 2000; ++i) {
        syscall(SYS_tgkill, pid, tid, SIGPROF);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const std::uint32_t count = std::min<std::uint32_t>(g_sample_count.load(), kMaxSamples);
    // By library and function (the exported symbol before the address) and the leaf's caller, with offsets for
    // addr2line.
    std::map<std::string, std::uint32_t> places;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uintptr_t offset = 0;
        std::uintptr_t caller_offset = 0;
        const std::string place = place_of(g_samples[i].load(std::memory_order_relaxed), offset);
        const std::string caller = place_of(g_callers[i].load(std::memory_order_relaxed), caller_offset);
        char text[640];
        std::snprintf(text, sizeof(text), "%s +0x%zx <- %s +0x%zx", place.c_str(), static_cast<std::size_t>(offset),
                      caller.c_str(), static_cast<std::size_t>(caller_offset));
        ++places[text];
    }
    std::vector<std::pair<std::uint32_t, std::string>> ranked;
    for (const auto& [key, samples] : places) {
        ranked.emplace_back(samples, key);
    }
    std::sort(ranked.rbegin(), ranked.rend());
    std::fprintf(stderr, "[sampler] %s (tid %d): %u samples\n", name.c_str(), tid, count);
    for (std::size_t i = 0; i < ranked.size() && i < 12u; ++i) {
        std::fprintf(stderr, "[sampler]   %5.1f%% %s\n", 100.0 * ranked[i].first / std::max(count, 1u),
                     ranked[i].second.c_str());
    }
}

}  // namespace

void start_thread_sampler() noexcept {
    char value[PROP_VALUE_MAX]{};
    if (__system_property_get("debug.nebula.profile", value) <= 0) {
        return;
    }
    const int delay = std::max(1, std::atoi(value));
    // libc's loaded extent, so the walk can step over it (memcpy and friends).
    dl_iterate_phdr(
        [](dl_phdr_info* object, std::size_t, void*) -> int {
            const char* name = object->dlpi_name != nullptr ? std::strrchr(object->dlpi_name, '/') : nullptr;
            if (name == nullptr || std::strcmp(name, "/libc.so") != 0) {
                return 0;
            }
            std::uintptr_t end = 0;
            for (int i = 0; i < object->dlpi_phnum; ++i) {
                const auto& header = object->dlpi_phdr[i];
                if (header.p_type == PT_LOAD) {
                    end = std::max<std::uintptr_t>(end, header.p_vaddr + header.p_memsz);
                }
            }
            g_libc_begin = object->dlpi_addr;
            g_libc_end = object->dlpi_addr + end;
            return 1;
        },
        nullptr);
    std::thread([delay] {
        prctl(PR_SET_NAME, "Nebula Sampler");
        struct sigaction action {};
        action.sa_sigaction = on_sample;
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        sigaction(SIGPROF, &action, nullptr);
        std::this_thread::sleep_for(std::chrono::seconds(delay));
        for (const char* prefix : {"Nebula GX", "Nebula Host Mai", "Nebula Game"}) {
            for (const auto& [tid, name] : threads_named(prefix)) {
                sample(tid, name);
            }
        }
        std::fflush(stderr);
    }).detach();
}

#else

void start_thread_sampler() noexcept {}

#endif

}  // namespace galaxy::sdl
