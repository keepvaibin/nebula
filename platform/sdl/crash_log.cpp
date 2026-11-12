// SPDX-License-Identifier: GPL-3.0-only
// A fatal signal's report in the log (sdl_host.h, install_crash_log): the signal, the address it names, the thread, the
// registers that locate the fault and the stack as library + offset (for addr2line), written straight to the log file
// before the system's own handler runs (Android's debuggerd, which writes its tombstone; elsewhere the default action).
// The log's reader thread is not relied on: a crash dump stops it. Windows has its own (host_main.cpp
// install_crash_reporter).

#include "sdl_host.h"

#if defined(__linux__)
#include <dlfcn.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#endif

namespace galaxy::sdl {

#if defined(__linux__)

namespace {

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
struct sigaction g_previous[std::size(kSignals)];
int g_log_fd = -1;
std::atomic<bool> g_reporting{false};

// One line to the log file (or stderr without one) and, on Android, to logcat.
void put(const char* line) {
    const int fd = g_log_fd >= 0 ? g_log_fd : STDERR_FILENO;
    const std::size_t length = std::strlen(line);
    if (write(fd, line, length) < 0 || write(fd, "\n", 1u) < 0) {
        // Nothing left to tell it to.
    }
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_FATAL, "Nebula", line);
#endif
}

const char* signal_name(int signal) {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV (a bad memory access)";
    case SIGBUS: return "SIGBUS (a misaligned or unmapped access)";
    case SIGILL: return "SIGILL (an illegal instruction)";
    case SIGFPE: return "SIGFPE (an arithmetic fault)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGTRAP: return "SIGTRAP (a trap)";
    default: return "a fatal signal";
    }
}

// "library +0xoffset (symbol)" for `pc`.
void describe(std::uintptr_t pc, char* text, std::size_t size) {
    Dl_info info{};
    if (pc == 0u || dladdr(reinterpret_cast<void*>(pc), &info) == 0 || info.dli_fname == nullptr) {
        std::snprintf(text, size, "0x%zx (no library)", static_cast<std::size_t>(pc));
        return;
    }
    const char* file = std::strrchr(info.dli_fname, '/');
    std::snprintf(text, size, "%s +0x%zx (%s)", file != nullptr ? file + 1 : info.dli_fname,
                  static_cast<std::size_t>(pc - reinterpret_cast<std::uintptr_t>(info.dli_fbase)),
                  info.dli_sname != nullptr ? info.dli_sname : "?");
}

struct Walk {
    std::uintptr_t frames[48];
    int count;
};

_Unwind_Reason_Code walk_frame(_Unwind_Context* context, void* data) {
    auto* walk = static_cast<Walk*>(data);
    walk->frames[walk->count++] = static_cast<std::uintptr_t>(_Unwind_GetIP(context));
    return walk->count < static_cast<int>(std::size(walk->frames)) ? _URC_NO_REASON : _URC_END_OF_STACK;
}

void report(int signal, const siginfo_t* info, const void* context) {
    char line[512];
    char place[320];
    char thread[17]{};
    prctl(PR_GET_NAME, thread, 0, 0, 0);
    std::snprintf(line, sizeof(line), "[crash] %s, code %d, address 0x%zx, thread \"%s\" (tid %ld)", signal_name(signal),
                  info != nullptr ? info->si_code : 0,
                  info != nullptr ? reinterpret_cast<std::size_t>(info->si_addr) : std::size_t{0}, thread,
                  static_cast<long>(syscall(SYS_gettid)));
    put(line);
    std::uintptr_t pc = 0u;
    if (context != nullptr) {
        const auto* uc = static_cast<const ucontext_t*>(context);
#if defined(__aarch64__)
        pc = static_cast<std::uintptr_t>(uc->uc_mcontext.pc);
        describe(pc, place, sizeof(place));
        std::snprintf(line, sizeof(line), "[crash] pc %s", place);
        put(line);
        describe(static_cast<std::uintptr_t>(uc->uc_mcontext.regs[30]), place, sizeof(place));
        std::snprintf(line, sizeof(line), "[crash] lr %s", place);
        put(line);
        std::snprintf(line, sizeof(line), "[crash] sp 0x%zx x0 0x%zx x1 0x%zx x2 0x%zx x8 0x%zx x19 0x%zx x20 0x%zx",
                      static_cast<std::size_t>(uc->uc_mcontext.sp), static_cast<std::size_t>(uc->uc_mcontext.regs[0]),
                      static_cast<std::size_t>(uc->uc_mcontext.regs[1]), static_cast<std::size_t>(uc->uc_mcontext.regs[2]),
                      static_cast<std::size_t>(uc->uc_mcontext.regs[8]), static_cast<std::size_t>(uc->uc_mcontext.regs[19]),
                      static_cast<std::size_t>(uc->uc_mcontext.regs[20]));
        put(line);
#elif defined(__x86_64__)
        pc = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
        describe(pc, place, sizeof(place));
        std::snprintf(line, sizeof(line), "[crash] pc %s", place);
        put(line);
        std::snprintf(line, sizeof(line), "[crash] rsp 0x%zx rdi 0x%zx rsi 0x%zx rax 0x%zx",
                      static_cast<std::size_t>(uc->uc_mcontext.gregs[REG_RSP]),
                      static_cast<std::size_t>(uc->uc_mcontext.gregs[REG_RDI]),
                      static_cast<std::size_t>(uc->uc_mcontext.gregs[REG_RSI]),
                      static_cast<std::size_t>(uc->uc_mcontext.gregs[REG_RAX]));
        put(line);
#endif
    }
    // The stack: this handler's frames, the signal trampoline, then the interrupted code's from the faulting pc on.
    Walk walk{};
    _Unwind_Backtrace(walk_frame, &walk);
    int first = 0;
    for (int i = 0; i < walk.count; ++i) {
        if (pc != 0u && walk.frames[i] >= pc - 1u && walk.frames[i] <= pc + 1u) {
            first = i;
            break;
        }
    }
    for (int i = first, n = 0; i < walk.count; ++i, ++n) {
        describe(walk.frames[i], place, sizeof(place));
        std::snprintf(line, sizeof(line), "[crash]   #%02d %s", n, place);
        put(line);
    }
}

void on_fatal(int signal, siginfo_t* info, void* context) {
    if (!g_reporting.exchange(true)) {
        report(signal, info, context);
    }
    // The handler that was there before (debuggerd's on Android), as if this one had not been: it ends the process.
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        if (kSignals[i] != signal) {
            continue;
        }
        const struct sigaction previous = g_previous[i];
        sigaction(signal, &previous, nullptr);
        if ((previous.sa_flags & SA_SIGINFO) != 0 && previous.sa_sigaction != nullptr) {
            previous.sa_sigaction(signal, info, context);
            return;
        }
        if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN) {
            previous.sa_handler(signal);
            return;
        }
        break;
    }
    // The default action: a fault comes again as the instruction runs again; a sent signal is raised again.
    if (info == nullptr || info->si_code <= 0) {
        raise(signal);
    }
}

}  // namespace

void install_crash_log(int fd) noexcept {
    g_log_fd = fd;
    struct sigaction action {};
    action.sa_sigaction = on_fatal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        sigaction(kSignals[i], &action, &g_previous[i]);
    }
}

#else

void install_crash_log(int) noexcept {}

#endif

}  // namespace galaxy::sdl
