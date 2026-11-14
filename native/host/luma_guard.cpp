// SPDX-License-Identifier: GPL-3.0-only
// The guard's host half (plan 1.6, contract guard, luma_guard.h): the fault handler that resumes a mod's fault at the
// kernel's innermost frame when that is safe, and names the mod in the crash report when it is not.
//
// Resumed: an access violation, an illegal instruction or an integer division fault, on a thread inside a boundary,
// not while an earlier fault of that thread is being handled, with the faulting instruction inside the mod's own code
// and no frame of the game or the host between the fault and the boundary (the mod's code called the game, which
// called back into the mod: the game's frames would be skipped half done). Windows x64 follows the call chain by the
// mod's unwind data (why_not_frames); a mod without unwind data, and POSIX, read the words on the stack instead
// (why_not_words), which errs toward not resuming. Everything else
// passes on untouched: Windows, to the next vectored handler and the crash reporter; POSIX, to the handler that was
// there before (the default one ends the process), after one line on stderr naming the mod.
#include "galaxy/luma_guard_host.h"

#include <luma_guard.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
// ucontext_t and its machine context come with <signal.h>; Apple's <ucontext.h> stops without _XOPEN_SOURCE.
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#else
#include <link.h>
#include <ucontext.h>
#endif
#endif

namespace galaxy::luma_guard {
namespace {

struct Range {
    std::uintptr_t base = 0;  // the image's
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    [[nodiscard]] bool has(std::uintptr_t at) const noexcept { return at >= begin && at < end; }
};

Range g_game;  // the game module's code
Range g_host;  // the executable's code (or, on Android, the host library's)
bool g_installed = false;

// The last fault the guard did not resume, for the crash report: written once (the first), on the faulting thread.
// Lock-free atomics, so a signal handler may use them (and MSVC, which builds the Windows host, has no __atomic).
char g_note[384];
char g_note_mod[96];
std::uintptr_t g_note_pc = 0;
std::atomic<std::uint32_t> g_note_claimed{0};
std::atomic<std::uint32_t> g_noted{0};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the note's flags are used in a signal handler");

const char* const kBoundaryWords[LUMA_GUARD_BOUNDARY_COUNT] = {"before hook", "after hook", "wrap hook", "hook",
                                                              "start",       "stop",       "service event", "class"};

// Async-signal-safe text: appends `text` at *at within `size`.
void put(char* out, std::size_t size, std::size_t* at, const char* text) noexcept {
    for (; text != nullptr && *text != 0 && *at + 1 < size; ++text) out[(*at)++] = *text;
    out[*at < size ? *at : size - 1] = 0;
}

void put_hex(char* out, std::size_t size, std::size_t* at, std::uintptr_t value) noexcept {
    char digits[2 + 16];
    std::size_t n = 0;
    do {
        digits[n++] = "0123456789abcdef"[value & 15u];
        value >>= 4;
    } while (value != 0 && n < 16);
    put(out, size, at, "0x");
    while (n > 0 && *at + 1 < size) out[(*at)++] = digits[--n];
    out[*at] = 0;
}

const char* fault_words(std::uint32_t fault) noexcept {
    switch (fault) {
    case LUMA_GUARD_ACCESS: return "a memory access it may not make";
    case LUMA_GUARD_ILLEGAL: return "an instruction the processor refuses";
    case LUMA_GUARD_DIVIDE: return "a division by zero";
    default: return "a stack overflow";
    }
}

constexpr const char* kGameBetween = "the game's or the host's code is between the boundary and the fault";

// The checks every platform makes before the frames are looked at; null when they pass.
[[maybe_unused]] const char* why_not_here(const LumaGuardThread* thread, const LumaGuardFrame* frame, std::uint32_t fault, std::uintptr_t pc,
                         std::uintptr_t sp) noexcept {
    if (fault == LUMA_GUARD_STACK) return "the stack overflowed";
    if (thread->handling != 0u) return "a second fault came while the first was handled";
    if (frame->code_begin == 0 || !(pc >= frame->code_begin && pc < frame->code_end)) {
        return "the fault is outside the mod's code (in the game, the host or the system, called by the mod)";
    }
    const std::uintptr_t top = static_cast<std::uintptr_t>(frame->regs[LUMA_GUARD_SP]);
    if (sp > top || top - sp > (std::uintptr_t{1} << 22)) return "the stack between the fault and the mod's boundary is not one the guard can read";
    return nullptr;
}

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
// luma_guard_call (LumaGuard.cpp, win-x64) pushes rbx and 32 bytes of shadow space below the stack pointer it saves
// (regs[LUMA_GUARD_SP]), then calls the mod: the call's return address is 56 bytes below that pointer, and the mod's
// outermost frame returns with the stack pointer 48 bytes below it.
constexpr std::uintptr_t kCallReturnSlot = 56;
constexpr std::uintptr_t kCallReturnSp = 48;
constexpr unsigned kMostFrames = 4096;
constexpr const char* kUnreadable = "the frames between the fault and the boundary could not be read";

// The function's frame register (UNWIND_INFO's FrameRegister, through its chained entries): the unwind reads the
// frame's saved words from that register's value. 0: none.
unsigned frame_register(DWORD64 image, const RUNTIME_FUNCTION* entry) noexcept {
    for (unsigned depth = 0; entry != nullptr && depth < 32; ++depth) {
        const auto* info = reinterpret_cast<const std::uint8_t*>(image + entry->UnwindData);
        if ((info[3] & 0x0Fu) != 0u) return info[3] & 0x0Fu;
        if (((info[0] >> 3) & UNW_FLAG_CHAININFO) == 0u) return 0u;
        const unsigned codes = (info[2] + 1u) & ~1u;  // UNWIND_CODE slots, padded to an even count
        entry = reinterpret_cast<const RUNTIME_FUNCTION*>(info + 4 + codes * 2u);
    }
    return 0u;
}

// From the fault's own registers up the real call chain to the boundary, frame by frame, as the system's unwinder
// does it: by each function's unwind data (a mod built by clang or MSVC carries it), or, for a function with none (a
// leaf, which never moves the stack pointer), by the return address at the stack pointer. Words left on the stack by
// calls that already returned are never taken for frames: only the return addresses the unwind leads to count. Every
// word it reads is inside the thread's stack: the stack pointer stays between the fault's and the boundary's, and a
// frame register must point into the stack, or the walk stops (not resumed). Resumed only when every frame up to the
// one that returns into luma_guard_call is the mod's own.
const char* why_not_frames(const LumaGuardFrame* frame, const CONTEXT* fault_context) noexcept {
    const std::uintptr_t top = static_cast<std::uintptr_t>(frame->regs[LUMA_GUARD_SP]);
    const std::uintptr_t lowest = static_cast<std::uintptr_t>(fault_context->Rsp);
    const auto stack_base = reinterpret_cast<std::uintptr_t>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    if (top < kCallReturnSlot) return kUnreadable;
    CONTEXT context = *fault_context;
    for (unsigned n = 0; n < kMostFrames; ++n) {
        const std::uintptr_t sp = static_cast<std::uintptr_t>(context.Rsp);
        if (sp < lowest || sp > top - kCallReturnSlot || (sp & 7u) != 0u) return kUnreadable;
        DWORD64 image = 0;
        PRUNTIME_FUNCTION const entry = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
        if (entry == nullptr) {
            // Only the faulting function may be a leaf: one that called the next frame has unwind data.
            if (n != 0u) return kUnreadable;
            context.Rip = *reinterpret_cast<const DWORD64*>(sp);
            context.Rsp = sp + 8u;
        } else {
            if (const unsigned reg = frame_register(image, entry); reg != 0u) {
                const auto value = static_cast<std::uintptr_t>((&context.Rax)[reg]);
                if (value < lowest || value >= stack_base) return kUnreadable;
            }
            void* handler_data = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, entry, &context, &handler_data, &establisher, nullptr);
        }
        const std::uintptr_t pc = static_cast<std::uintptr_t>(context.Rip);
        const std::uintptr_t up = static_cast<std::uintptr_t>(context.Rsp);
        if (up <= sp) return kUnreadable;  // a frame that does not move up the stack is not one
        if (pc == frame->call_return) return up == top - kCallReturnSp ? nullptr : kUnreadable;
        if (g_game.has(pc) || g_host.has(pc)) return kGameBetween;
        if (!(pc >= frame->code_begin && pc < frame->code_end)) {
            return "other code (the system's or another library's) is between the boundary and the fault";
        }
    }
    return kUnreadable;
}

// Whether the image holding `pc` (the mod's library) carries unwind data (an exception directory). A library built
// without unwind tables (clang's -ffreestanding drops them unless -fasynchronous-unwind-tables is given) has none:
// its call chain cannot be followed, and the words on its stack are read instead (why_not_words).
bool has_unwind_data(std::uintptr_t pc) noexcept {
    void* base = nullptr;
    if (RtlPcToFileHeader(reinterpret_cast<PVOID>(pc), &base) == nullptr || base == nullptr) return false;
    const auto image = reinterpret_cast<std::uintptr_t>(base);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + static_cast<std::uintptr_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) {
        return false;
    }
    return nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size != 0u;
}
#endif

// For a mod with no unwind data (and on POSIX, where the call chain is not followed yet): the words on the stack from
// the fault up to the boundary. A return address into the game or the host below one into the mod means the mod
// called the game, which called the mod again. A word a returned call left in a frame of the mod counts too, so a
// fault after the mod called the game is not resumed: it errs that way. `lr`: the faulting thread's link register (0
// where the return address is on the stack).
[[maybe_unused]] const char* why_not_words(const LumaGuardFrame* frame, std::uintptr_t sp, std::uintptr_t lr) noexcept {
    const std::uintptr_t top = static_cast<std::uintptr_t>(frame->regs[LUMA_GUARD_SP]);
    bool game_seen = false;
    const auto look = [&](std::uintptr_t value) {
        if (value == frame->call_return) return false;
        if (g_game.has(value) || g_host.has(value)) {
            game_seen = true;
        } else if (game_seen && value >= frame->code_begin && value < frame->code_end) {
            return true;
        }
        return false;
    };
    if (lr != 0 && look(lr)) return kGameBetween;
    for (std::uintptr_t at = sp & ~std::uintptr_t{7}; at < top; at += sizeof(std::uintptr_t)) {
        if (look(*reinterpret_cast<const std::uintptr_t*>(at))) return kGameBetween;
    }
    return nullptr;
}

// The note the crash report carries, written without allocating (the faulting thread may hold the heap's lock).
void note(const LumaGuardFrame* frame, std::uint32_t fault, std::uintptr_t pc, const char* why) noexcept {
    if (g_note_claimed.exchange(1u, std::memory_order_acq_rel) != 0u) return;
    g_note_pc = pc;
    std::size_t at = 0;
    put(g_note_mod, sizeof(g_note_mod), &at, frame->mod);
    at = 0;
    const std::uint32_t kind = frame->boundary < LUMA_GUARD_BOUNDARY_COUNT ? frame->boundary : 0u;
    put(g_note, sizeof(g_note), &at, "the code of the mod ");
    put(g_note, sizeof(g_note), &at, frame->mod);
    put(g_note, sizeof(g_note), &at, " failed (");
    put(g_note, sizeof(g_note), &at, kBoundaryWords[kind]);
    put(g_note, sizeof(g_note), &at, ": ");
    put(g_note, sizeof(g_note), &at, frame->where);
    put(g_note, sizeof(g_note), &at, "): ");
    put(g_note, sizeof(g_note), &at, fault_words(fault));
    put(g_note, sizeof(g_note), &at, " at ");
    put_hex(g_note, sizeof(g_note), &at, pc);
    put(g_note, sizeof(g_note), &at, "; not resumed: ");
    put(g_note, sizeof(g_note), &at, why);
    g_noted.store(1u, std::memory_order_release);
}

// The image's executable part, from its headers in memory.
#if defined(_WIN32)
Range code_of(HMODULE module) noexcept {
    Range range;
    if (module == nullptr) return range;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    range.base = base;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return range;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) return range;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const std::uintptr_t begin = base + section->VirtualAddress;
        const std::uintptr_t end = begin + (section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData);
        if (range.begin == 0 || begin < range.begin) range.begin = begin;
        if (end > range.end) range.end = end;
    }
    return range;
}

Range code_at(const void* address) noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module)) {
        return {};
    }
    return code_of(module);
}
#elif defined(__APPLE__)
Range code_at(const void* address) noexcept {
    Dl_info info{};
    if (address == nullptr || dladdr(address, &info) == 0 || info.dli_fbase == nullptr) return {};
    for (std::uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        if (static_cast<const void*>(header) != info.dli_fbase) continue;
        const std::intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const auto* command = reinterpret_cast<const load_command*>(header + 1);
        for (std::uint32_t c = 0; c < header->ncmds; ++c) {
            if (command->cmd == LC_SEGMENT_64) {
                const auto* segment = reinterpret_cast<const segment_command_64*>(command);
                if (std::strncmp(segment->segname, "__TEXT", 16) == 0) {
                    const auto begin = static_cast<std::uintptr_t>(segment->vmaddr + slide);
                    return {reinterpret_cast<std::uintptr_t>(header), begin, begin + static_cast<std::uintptr_t>(segment->vmsize)};
                }
            }
            command = reinterpret_cast<const load_command*>(reinterpret_cast<const char*>(command) + command->cmdsize);
        }
    }
    return {};
}
#else
struct Find {
    std::uintptr_t address;
    Range range;
};

Range code_at(const void* address) noexcept {
    Find find{reinterpret_cast<std::uintptr_t>(address), {}};
    if (address == nullptr) return {};
    dl_iterate_phdr(
        [](dl_phdr_info* info, std::size_t, void* data) -> int {
            auto* f = static_cast<Find*>(data);
            Range range;
            bool here = false;
            for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
                const ElfW(Phdr)& header = info->dlpi_phdr[i];
                if (header.p_type != PT_LOAD) continue;
                const std::uintptr_t begin = info->dlpi_addr + header.p_vaddr;
                const std::uintptr_t end = begin + header.p_memsz;
                if (f->address >= begin && f->address < end) here = true;
                if ((header.p_flags & PF_X) == 0) continue;
                if (range.begin == 0 || begin < range.begin) range.begin = begin;
                if (end > range.end) range.end = end;
            }
            if (!here) return 0;
            range.base = info->dlpi_addr;
            f->range = range;
            return 1;
        },
        &find);
    return find.range;
}
#endif

int32_t code_range(void* library, std::uintptr_t* base, std::uintptr_t* begin, std::uintptr_t* end) {
#if defined(_WIN32)
    const Range range = code_of(static_cast<HMODULE>(library));
#else
    const Range range = code_at(dlsym(library, "luma_mod"));
#endif
    *base = range.base;
    *begin = range.begin;
    *end = range.end;
    return range.begin != 0 ? 0 : -1;
}

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#define LUMA_GUARD_HANDLER 1
DWORD g_slot = TLS_OUT_OF_INDEXES;

int32_t thread_enter(LumaGuardThread* thread) {
    // Leave room for the vectored handler and crash reporter when a mod exhausts this thread's stack.
    ULONG stack_bytes = 64 * 1024;
    SetThreadStackGuarantee(&stack_bytes);
    return g_slot != TLS_OUT_OF_INDEXES && TlsSetValue(g_slot, thread) ? 0 : -1;
}

LONG CALLBACK on_fault(EXCEPTION_POINTERS* info) {
    std::uint32_t fault = LUMA_GUARD_NONE;
    switch (info->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR: fault = LUMA_GUARD_ACCESS; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION: fault = LUMA_GUARD_ILLEGAL; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW: fault = LUMA_GUARD_DIVIDE; break;
    case EXCEPTION_STACK_OVERFLOW: fault = LUMA_GUARD_STACK; break;
    default: return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD error = GetLastError();
    auto* thread = static_cast<LumaGuardThread*>(TlsGetValue(g_slot));
    SetLastError(error);
    LumaGuardFrame* const frame = thread != nullptr ? thread->top : nullptr;
    if (frame == nullptr) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* const context = info->ContextRecord;
    const auto pc = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    const char* why = why_not_here(thread, frame, fault, pc, static_cast<std::uintptr_t>(context->Rsp));
    if (why == nullptr) {
        why = has_unwind_data(pc) ? why_not_frames(frame, context) : why_not_words(frame, static_cast<std::uintptr_t>(context->Rsp), 0);
    }
    if (why != nullptr) {
        note(frame, fault, pc, why);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    frame->fault = fault;
    frame->pc = pc;
    frame->address = info->ExceptionRecord->NumberParameters >= 2 && fault == LUMA_GUARD_ACCESS
                         ? static_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionInformation[1])
                         : 0;
    thread->handling = 1u;
    context->Rcx = reinterpret_cast<DWORD64>(frame);
    context->Rsp = ((frame->regs[LUMA_GUARD_SP] - 512u) & ~DWORD64{15}) - 8u;
    context->Rip = reinterpret_cast<DWORD64>(frame->resume);
    return EXCEPTION_CONTINUE_EXECUTION;
}
#elif defined(_WIN32)
// Windows on ARM64: no handler yet (contract guard 5); install() says the guard is off.
int32_t thread_enter(LumaGuardThread*) { return -1; }
#else
#define LUMA_GUARD_HANDLER 1
pthread_key_t g_slot;
pthread_key_t g_alt;  // the alternate signal stack this module gave the thread, freed when the thread ends
struct sigaction g_previous[4];
const int kSignals[4] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
constexpr std::size_t kAltBytes = 64 * 1024;

void drop_alt(void* stack) {
    stack_t off{};
    off.ss_flags = SS_DISABLE;
    sigaltstack(&off, nullptr);
    munmap(stack, kAltBytes);
}

int32_t thread_enter(LumaGuardThread* thread) {
    if (pthread_setspecific(g_slot, thread) != 0) return -1;
    stack_t now{};
    if (sigaltstack(nullptr, &now) == 0 && (now.ss_flags & SS_DISABLE) == 0) return 0;  // the thread has one
    void* const stack = mmap(nullptr, kAltBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) return -1;
    stack_t alt{};
    alt.ss_sp = stack;
    alt.ss_size = kAltBytes;
    if (sigaltstack(&alt, nullptr) != 0) {
        munmap(stack, kAltBytes);
        return -1;
    }
    pthread_setspecific(g_alt, stack);
    return 0;
}

struct Registers {
    std::uintptr_t pc, sp, lr;
};

Registers registers_of(const ucontext_t* context) noexcept {
#if defined(__APPLE__) && defined(__x86_64__)
    return {context->uc_mcontext->__ss.__rip, context->uc_mcontext->__ss.__rsp, 0};
#elif defined(__APPLE__) && defined(__aarch64__)
    return {context->uc_mcontext->__ss.__pc, context->uc_mcontext->__ss.__sp, context->uc_mcontext->__ss.__lr};
#elif defined(__x86_64__)
    return {static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RIP]),
            static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RSP]), 0};
#elif defined(__aarch64__)
    return {context->uc_mcontext.pc, context->uc_mcontext.sp, context->uc_mcontext.regs[30]};
#endif
}

void resume_at(ucontext_t* context, LumaGuardFrame* frame) noexcept {
    const auto sp = static_cast<std::uintptr_t>((frame->regs[LUMA_GUARD_SP] - 512u) & ~std::uint64_t{15});
    const auto resume = reinterpret_cast<std::uintptr_t>(frame->resume);
    const auto arg = reinterpret_cast<std::uintptr_t>(frame);
#if defined(__APPLE__) && defined(__x86_64__)
    context->uc_mcontext->__ss.__rip = resume;
    context->uc_mcontext->__ss.__rsp = sp - 8;
    context->uc_mcontext->__ss.__rdi = arg;
#elif defined(__APPLE__) && defined(__aarch64__)
    context->uc_mcontext->__ss.__pc = resume;
    context->uc_mcontext->__ss.__sp = sp;
    context->uc_mcontext->__ss.__x[0] = arg;
#elif defined(__x86_64__)
    context->uc_mcontext.gregs[REG_RIP] = static_cast<greg_t>(resume);
    context->uc_mcontext.gregs[REG_RSP] = static_cast<greg_t>(sp - 8);
    context->uc_mcontext.gregs[REG_RDI] = static_cast<greg_t>(arg);
#elif defined(__aarch64__)
    context->uc_mcontext.pc = resume;
    context->uc_mcontext.sp = sp;
    context->uc_mcontext.regs[0] = arg;
#endif
}

CrashLine g_crash_line = nullptr;

// The crash report of a fault no handler before the guard's takes: what it was and where, and the guard's sentence
// when it saw the fault in a mod's code and did not resume it. Only what a signal handler may use.
void report_fault(int signal, siginfo_t* info, void* raw) noexcept {
    const Registers regs = registers_of(static_cast<const ucontext_t*>(raw));
    char line[160];
    std::size_t at = 0;
    put(line, sizeof(line), &at, signal == SIGSEGV ? "SIGSEGV" : signal == SIGBUS ? "SIGBUS" : signal == SIGILL ? "SIGILL" : "SIGFPE");
    put(line, sizeof(line), &at, " at ");
    put_hex(line, sizeof(line), &at, regs.pc);
    put(line, sizeof(line), &at, ", address ");
    put_hex(line, sizeof(line), &at, reinterpret_cast<std::uintptr_t>(info->si_addr));
    const bool noted = g_noted.load(std::memory_order_acquire) != 0u;
    if (g_crash_line != nullptr) {
        g_crash_line("crash:", line);
        if (noted) g_crash_line("crash: guard:", g_note);
        return;
    }
    put(line, sizeof(line), &at, "\n");
    (void)!write(2, "crash: ", 7);
    (void)!write(2, line, at);
}

void pass_on(int signal, siginfo_t* info, void* context) noexcept {
    for (int i = 0; i < 4; ++i) {
        if (kSignals[i] != signal) continue;
        const struct sigaction& previous = g_previous[i];
        if ((previous.sa_flags & SA_SIGINFO) != 0 && previous.sa_sigaction != nullptr) {
            previous.sa_sigaction(signal, info, context);
            return;
        }
        if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != nullptr) {
            previous.sa_handler(signal);
            return;
        }
        // The default: the report, then the signal's default action at once, from here. Returning to fault again
        // left a Linux game running to a test's time limit after a mod's stack overflow.
        report_fault(signal, info, context);
        struct sigaction fallback{};
        fallback.sa_handler = SIG_DFL;
        sigemptyset(&fallback.sa_mask);
        sigaction(signal, &fallback, nullptr);
        sigset_t only;
        sigemptyset(&only);
        sigaddset(&only, signal);
        pthread_sigmask(SIG_UNBLOCK, &only, nullptr);
        raise(signal);
        _exit(128 + signal);
    }
}

void on_signal(int signal, siginfo_t* info, void* raw) {
    auto* const context = static_cast<ucontext_t*>(raw);
    std::uint32_t fault = LUMA_GUARD_NONE;
    const Registers regs = registers_of(context);
    if (signal == SIGSEGV || signal == SIGBUS) {
        const auto address = reinterpret_cast<std::uintptr_t>(info->si_addr);
        // A touch just below the stack pointer is the stack running out.
        fault = address <= regs.sp + 256u && regs.sp - address < 65536u ? LUMA_GUARD_STACK : LUMA_GUARD_ACCESS;
    } else if (signal == SIGILL) {
        fault = LUMA_GUARD_ILLEGAL;
    } else if (signal == SIGFPE && (info->si_code == FPE_INTDIV || info->si_code == FPE_INTOVF)) {
        fault = LUMA_GUARD_DIVIDE;
    }
    auto* const thread = fault != LUMA_GUARD_NONE ? static_cast<LumaGuardThread*>(pthread_getspecific(g_slot)) : nullptr;
    LumaGuardFrame* const frame = thread != nullptr ? thread->top : nullptr;
    if (frame == nullptr) {
        pass_on(signal, info, raw);
        return;
    }
    const char* why = why_not_here(thread, frame, fault, regs.pc, regs.sp);
    if (why == nullptr) why = why_not_words(frame, regs.sp, regs.lr);
    if (why != nullptr) {
        note(frame, fault, regs.pc, why);
        char line[480];
        std::size_t at = 0;
        put(line, sizeof(line), &at, "guard: ");
        put(line, sizeof(line), &at, g_note);
        put(line, sizeof(line), &at, "\n");
        (void)!write(2, line, at);
        pass_on(signal, info, raw);
        return;
    }
    frame->fault = fault;
    frame->pc = regs.pc;
    frame->address = fault == LUMA_GUARD_ACCESS ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0;
    thread->handling = 1u;
    resume_at(context, frame);
}
#endif

const LumaGuardHost kHost = {sizeof(LumaGuardHost), thread_enter, code_range};

}  // namespace

bool install(void* (*find_symbol)(const char*)) noexcept {
    if (g_installed) return true;
#if !defined(LUMA_GUARD_HANDLER)
    (void)find_symbol;
    return false;
#else
    const auto give = reinterpret_cast<LumaGuardInstall>(find_symbol != nullptr ? find_symbol("luma_guard_install") : nullptr);
    if (give == nullptr) return false;
    g_game = code_at(reinterpret_cast<const void*>(give));
    g_host = code_at(reinterpret_cast<const void*>(&install));
#if defined(_WIN32)
    g_slot = TlsAlloc();
    if (g_slot == TLS_OUT_OF_INDEXES) return false;
    // First: a mod's fault is the guard's before any other handler sees it; everything else passes on.
    if (AddVectoredExceptionHandler(1, on_fault) == nullptr) return false;
#else
    if (pthread_key_create(&g_slot, nullptr) != 0 || pthread_key_create(&g_alt, drop_alt) != 0) return false;
    struct sigaction action{};
    action.sa_sigaction = on_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < 4; ++i) {
        if (sigaction(kSignals[i], &action, &g_previous[i]) != 0) return false;
    }
#endif
    if (give(&kHost) != 1) return false;
    g_installed = true;
    return true;
#endif
}

const char* note_text(const void* pc) noexcept {
    return g_noted.load(std::memory_order_acquire) != 0u && g_note_pc == reinterpret_cast<std::uintptr_t>(pc) ? g_note : nullptr;
}

const char* note_mod() noexcept {
    return g_noted.load(std::memory_order_acquire) != 0u ? g_note_mod : nullptr;
}

void set_crash_line(CrashLine line) noexcept {
#if defined(LUMA_GUARD_HANDLER) && !defined(_WIN32)
    g_crash_line = line;
#else
    (void)line;
#endif
}

}  // namespace galaxy::luma_guard
