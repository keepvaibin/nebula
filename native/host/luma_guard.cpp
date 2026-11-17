// SPDX-License-Identifier: GPL-3.0-only
// The guard's host half (luma_guard.h): the fault handler that resumes a mod's fault at the
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
#include "galaxy/mod_crash.h"
#include "luma_guard_spans.h"
#include "luma_guard_unwind.h"
#include "luma_guard_words.h"

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

// Every mod library's code while it is loaded (add_library, remove_library), as many as there are
// (luma_guard_spans.h); a handler reads it with no lock.
Spans g_spans;

// The kernel's need walk for a fault that is not resumed (luma_guard.h, LumaGuardBlame); null: a game module without it.
LumaGuardBlame g_blame = nullptr;

// Whether `pc` is in a loaded mod library's code (any mod's: a library mod's function another mod called).
bool in_mod_library(std::uintptr_t pc) noexcept { return g_spans.has(pc); }

// The mod's own code (the boundary's) or any mod library's: a fault there is a mod's fault.
bool in_mod_code(const LumaGuardFrame* frame, std::uintptr_t pc) noexcept {
    return (frame->code_begin != 0 && pc >= frame->code_begin && pc < frame->code_end) || in_mod_library(pc);
}

// The fault's call chain as the kernel reads it (LumaGuardThread.chain): one address for each run of frames in one mod
// library's code, innermost first. Written on the faulting thread, before the kernel can look.
class Chain {
public:
    Chain(LumaGuardThread* thread, const LumaGuardFrame* frame, std::uintptr_t pc) noexcept : thread_(thread), frame_(frame) {
        thread_->chain_count = 0;
        thread_->chain_gap = LUMA_GUARD_CHAIN;
        thread_->chain_pc = pc;
    }
    // A frame whose code is at `pc`: recorded when it is a mod's and not the same library as the frame before it.
    void add(std::uintptr_t pc) noexcept {
        std::uintptr_t span = g_spans.span_of(pc);
        if (span == 0 && frame_->code_begin != 0 && pc >= frame_->code_begin && pc < frame_->code_end) span = frame_->code_begin;
        if (span == 0) return;
        if (thread_->chain_count != 0 && span == last_) {
            thread_->chain_outer[thread_->chain_count - 1] = pc;  // the same run, one frame further out
            return;
        }
        last_ = span;
        if (thread_->chain_count < LUMA_GUARD_CHAIN) {
            thread_->chain_outer[thread_->chain_count] = pc;
            thread_->chain[thread_->chain_count++] = pc;
            return;
        }
        // More runs than the chain holds: the outermost one so far takes the last place, the ones before it are dropped.
        thread_->chain[LUMA_GUARD_CHAIN - 1] = pc;
        thread_->chain_outer[LUMA_GUARD_CHAIN - 1] = pc;
        if (thread_->chain_gap > LUMA_GUARD_CHAIN - 2) thread_->chain_gap = LUMA_GUARD_CHAIN - 2;
    }
    // The frames after the last one recorded were not followed.
    void broken() noexcept {
        if (thread_->chain_count != 0 && thread_->chain_gap > thread_->chain_count - 1) thread_->chain_gap = thread_->chain_count - 1;
    }

private:
    LumaGuardThread* thread_;
    const LumaGuardFrame* frame_;
    std::uintptr_t last_ = 0;
};

// The last fault the guard did not resume, for the crash report: written once (the first), on the faulting thread.
// Lock-free atomics, so a signal handler may use them (and MSVC, which builds the Windows host, has no __atomic).
char g_note[640];
char g_note_mod[96];
std::uintptr_t g_note_pc = 0;
std::atomic<std::uint32_t> g_note_claimed{0};
std::atomic<std::uint32_t> g_noted{0};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the note's flags are used in a signal handler");

const char* const kBoundaryWords[LUMA_GUARD_BOUNDARY_COUNT] = {"before hook", "after hook", "wrap hook", "hook", "start",
                                                              "stop", "service event", "class", "call from another mod", "cut"};

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
    if (frame->code_begin == 0 || !in_mod_code(frame, pc)) {
        return "the fault is outside the mods' code (in the game, the host or the system, called by the mod)";
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

// The bytes of the image at `image` (SizeOfImage: the loader checked the headers that give it), or 0.
std::size_t image_size(DWORD64 image) noexcept {
    if (image == 0) return 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(image);
    return reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew)
        ->OptionalHeader.SizeOfImage;
}

// The function's frame register (UNWIND_INFO's FrameRegister, through its chained entries): the unwind reads the
// frame's saved words from that register's value. 0: none; -1: its unwind data is not wholly in its image (a mod's
// file: read within it only, luma_guard_unwind.h).
int frame_register(DWORD64 image, const RUNTIME_FUNCTION* entry) noexcept {
    const auto at = reinterpret_cast<std::uintptr_t>(entry);
    if (entry == nullptr || at < image) return -1;
    return galaxy::luma_guard::pe_frame_register(reinterpret_cast<const std::uint8_t*>(image), image_size(image),
                                                 at - static_cast<std::uintptr_t>(image));
}

// From the fault's own registers up the real call chain to the boundary, frame by frame, as the system's unwinder
// does it: by each function's unwind data (a mod built by clang or MSVC carries it), or, for a function with none (a
// leaf, which never moves the stack pointer), by the return address at the stack pointer. Words left on the stack by
// calls that already returned are never taken for frames: only the return addresses the unwind leads to count. Every
// word it reads is inside the thread's stack: the stack pointer stays between the fault's and the boundary's, and a
// frame register must point into the stack, or the walk stops (not resumed). Resumed only when every frame up to the
// one that returns into luma_guard_call is the mod's own.
const char* why_not_frames(const LumaGuardFrame* frame, const CONTEXT* fault_context, Chain& chain) noexcept {
    const std::uintptr_t top = static_cast<std::uintptr_t>(frame->regs[LUMA_GUARD_SP]);
    const std::uintptr_t lowest = static_cast<std::uintptr_t>(fault_context->Rsp);
    const auto stack_base = reinterpret_cast<std::uintptr_t>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    chain.add(static_cast<std::uintptr_t>(fault_context->Rip));
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
            const int reg = frame_register(image, entry);
            if (reg < 0) return kUnreadable;
            if (reg != 0) {
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
        if (!in_mod_code(frame, pc)) {
            return "other code (the system's or a library's that is no mod's) is between the boundary and the fault";
        }
        chain.add(pc);
    }
    return kUnreadable;
}

// For a fault the guard does not resume, the chain the kernel names the mods by (luma_guard_blame): the frames from the
// fault followed as why_not_frames follows them, except that code which is no mod's at the start (the game's or the
// system's, which the mod's code called) is passed over, and the walk ends at the first such frame after a mod's (the
// frames past it are not followed). It only reads the stack, inside the bounds why_not_frames keeps.
void chain_for_report(const LumaGuardFrame* frame, const CONTEXT* fault_context, Chain& chain) noexcept {
    const std::uintptr_t top = static_cast<std::uintptr_t>(frame->regs[LUMA_GUARD_SP]);
    const std::uintptr_t lowest = static_cast<std::uintptr_t>(fault_context->Rsp);
    const auto stack_base = reinterpret_cast<std::uintptr_t>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    CONTEXT context = *fault_context;
    bool in_mods = false;
    for (unsigned n = 0; n < kMostFrames && top >= kCallReturnSlot; ++n) {
        const std::uintptr_t pc = static_cast<std::uintptr_t>(context.Rip);
        if (n != 0 && pc == frame->call_return) return;  // the boundary: every frame was followed
        if (in_mod_code(frame, pc)) {
            chain.add(pc);
            in_mods = true;
        } else if (in_mods) {
            break;
        }
        const std::uintptr_t sp = static_cast<std::uintptr_t>(context.Rsp);
        if (sp < lowest || sp > top - kCallReturnSlot || (sp & 7u) != 0u) break;
        DWORD64 image = 0;
        PRUNTIME_FUNCTION const entry = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
        if (entry == nullptr) {
            if (n != 0u) break;
            context.Rip = *reinterpret_cast<const DWORD64*>(sp);
            context.Rsp = sp + 8u;
        } else {
            const int reg = frame_register(image, entry);
            if (reg < 0) break;
            if (reg != 0) {
                const auto value = static_cast<std::uintptr_t>((&context.Rax)[reg]);
                if (value < lowest || value >= stack_base) break;
            }
            void* handler_data = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, entry, &context, &handler_data, &establisher, nullptr);
        }
        if (static_cast<std::uintptr_t>(context.Rsp) <= sp) break;
    }
    chain.broken();
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

// For a mod with no unwind data (and on POSIX, where the call chain is not followed yet): the words on the mod's frames
// (luma_guard_words.h). `lr`: the faulting thread's link register (0 where the return address is on the stack).
[[maybe_unused]] const char* why_not_words(const LumaGuardFrame* frame, std::uintptr_t sp, std::uintptr_t lr) noexcept {
    switch (scan_words(sp, static_cast<std::uintptr_t>(frame->regs[LUMA_GUARD_SP]), LUMA_GUARD_CALL_BYTES, lr, frame->call_return,
                       CodeSpan{frame->code_begin, frame->code_end}, CodeSpan{g_game.begin, g_game.end}, CodeSpan{g_host.begin, g_host.end})) {
    case Words::ModOnly: return nullptr;
    case Words::GameBetween: return kGameBetween;
    case Words::Unreadable: break;
    }
    return "the stack between the fault and the mod's boundary is not one the guard can read";
}

// The note the crash report carries, written without allocating (the faulting thread may hold the heap's lock). The
// mod it names is the one the kernel's need walk turns off (the boundary's mod when the module has no walk); the
// kernel's words name the mod whose code faulted and the place.
void note(const LumaGuardThread* thread, const LumaGuardFrame* frame, std::uint32_t fault, std::uintptr_t pc, const char* why) noexcept {
    if (g_note_claimed.exchange(1u, std::memory_order_acq_rel) != 0u) return;
    g_note_pc = pc;
    char blamed[96] = {};
    char blame_text[224] = {};
    const bool named = g_blame != nullptr && g_blame(thread, frame, blamed, sizeof(blamed), blame_text, sizeof(blame_text)) == 1;
    std::size_t at = 0;
    put(g_note_mod, sizeof(g_note_mod), &at, named ? blamed : frame->mod);
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
    if (named) {
        put(g_note, sizeof(g_note), &at, "; ");
        put(g_note, sizeof(g_note), &at, blame_text);
    }
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

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
std::uintptr_t function_at(std::uintptr_t pc) {
    DWORD64 image = 0;
    const RUNTIME_FUNCTION* entry = RtlLookupFunctionEntry(pc, &image, nullptr);
    if (entry == nullptr || image == 0 || reinterpret_cast<std::uintptr_t>(entry) < image) return 0;
    // The mod's unwind data is read within its image only.
    const std::uint32_t begin = galaxy::luma_guard::pe_function_begin(
        reinterpret_cast<const std::uint8_t*>(image), image_size(image),
        reinterpret_cast<std::uintptr_t>(entry) - static_cast<std::uintptr_t>(image));
    return begin != 0 ? static_cast<std::uintptr_t>(image) + begin : 0;
}
#elif defined(_WIN32) || defined(__APPLE__)
// Windows on ARM64 has no guard yet; the Mac's compact unwind is not read: the cut names no function.
std::uintptr_t function_at(std::uintptr_t) { return 0; }
#else
// The largest function start at or below pc in the image's .eh_frame_hdr (PT_GNU_EH_FRAME), the table the linker
// writes for the unwinder. The header is the mod's: it is read only where it lies inside one of the image's readable
// loaded segments, and within it (luma_guard_unwind.h).
std::uintptr_t function_at(std::uintptr_t pc) {
    struct Find {
        std::uintptr_t pc;
        std::uintptr_t begin;
    } find{pc, 0};
    dl_iterate_phdr(
        [](dl_phdr_info* info, std::size_t, void* data) -> int {
            auto* f = static_cast<Find*>(data);
            const ElfW(Phdr)* hdr_segment = nullptr;
            bool here = false;
            for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
                const ElfW(Phdr)& header = info->dlpi_phdr[i];
                if (header.p_type == PT_GNU_EH_FRAME) hdr_segment = &header;
                if (header.p_type == PT_LOAD && f->pc >= info->dlpi_addr + header.p_vaddr &&
                    f->pc < info->dlpi_addr + header.p_vaddr + header.p_memsz) {
                    here = true;
                }
            }
            if (!here) return 0;
            if (hdr_segment == nullptr) return 1;
            const std::uintptr_t hdr = info->dlpi_addr + hdr_segment->p_vaddr;
            std::size_t size = 0;
            for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
                const ElfW(Phdr)& header = info->dlpi_phdr[i];
                if (header.p_type != PT_LOAD || (header.p_flags & PF_R) == 0) continue;
                const std::uintptr_t begin = info->dlpi_addr + header.p_vaddr;
                if (hdr < begin || hdr - begin >= header.p_memsz) continue;
                const std::uintptr_t room = header.p_memsz - (hdr - begin);
                size = static_cast<std::size_t>(hdr_segment->p_memsz < room ? hdr_segment->p_memsz : room);
                break;
            }
            f->begin = galaxy::luma_guard::eh_frame_hdr_function(reinterpret_cast<const std::uint8_t*>(hdr), size, hdr, f->pc);
            return 1;
        },
        &find);
    return find.begin;
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
    // A second fault while the first is handled: the chain the kernel is reading stays as it is.
    if (thread->handling != 0u) {
        note(thread, frame, fault, pc, "a second fault came while the first was handled");
        return EXCEPTION_CONTINUE_SEARCH;
    }
    Chain chain(thread, frame, pc);
    const char* why = why_not_here(thread, frame, fault, pc, static_cast<std::uintptr_t>(context->Rsp));
    const bool unwind = has_unwind_data(pc);
    if (why == nullptr) {
        if (unwind) {
            why = why_not_frames(frame, context, chain);
        } else {
            // No unwind data: the frames are read word by word, so only the fault is in the chain.
            chain.add(pc);
            chain.broken();
            why = why_not_words(frame, static_cast<std::uintptr_t>(context->Rsp), 0);
        }
    }
    if (why != nullptr) {
        // Not resumed: the chain for the crash record, from the fault through the code that is no mod's.
        Chain report(thread, frame, pc);
        if (unwind) {
            chain_for_report(frame, context, report);
        } else {
            report.add(pc);
            report.broken();
        }
        note(thread, frame, fault, pc, why);
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
// Windows on ARM64: no handler yet; install() says the guard is off.
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
    std::uintptr_t pc, sp, lr, fp;
};

Registers registers_of(const ucontext_t* context) noexcept {
#if defined(__APPLE__) && defined(__x86_64__)
    return {context->uc_mcontext->__ss.__rip, context->uc_mcontext->__ss.__rsp, 0, context->uc_mcontext->__ss.__rbp};
#elif defined(__APPLE__) && defined(__aarch64__)
    return {context->uc_mcontext->__ss.__pc, context->uc_mcontext->__ss.__sp, context->uc_mcontext->__ss.__lr,
            context->uc_mcontext->__ss.__fp};
#elif defined(__x86_64__)
    return {static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RIP]),
            static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RSP]), 0,
            static_cast<std::uintptr_t>(context->uc_mcontext.gregs[REG_RBP])};
#elif defined(__aarch64__)
    return {context->uc_mcontext.pc, context->uc_mcontext.sp, context->uc_mcontext.regs[30], context->uc_mcontext.regs[29]};
#endif
}

// The chain on POSIX, by the frame records the mods are built with (LumaLink builds every mod with frame pointers):
// each record holds the record of the frame that called it and the return address into that frame's code (aarch64:
// x29 and x30 as stp stores them; x86-64: rbp and the return address the call pushed above it). A record outside the
// mod's stack, one that does not move up the stack, or a return address into no mod's code ends the walk, and the
// chain says the frames past it were not followed. The word scan still decides whether the fault is resumed.
// A fault outside the mods' code (the game's or the system's, called by a mod; not resumed): the records of that code
// before the first mod frame are passed over, for the crash record's chain.
void chain_by_records(Chain& chain, const LumaGuardFrame* frame, const Registers& regs) noexcept {
    bool in_mods = in_mod_code(frame, regs.pc);
    if (in_mods) chain.add(regs.pc);
    if (regs.lr != 0 && in_mod_code(frame, regs.lr)) {  // a leaf's caller (or the function itself)
        chain.add(regs.lr);
        in_mods = true;
    }
    const std::uintptr_t top = static_cast<std::uintptr_t>(frame->regs[LUMA_GUARD_SP]);
    std::uintptr_t record = regs.fp;
    for (unsigned n = 0; n < 4096; ++n) {
        if (record < regs.sp || record + 16u > top || (record & 7u) != 0u) break;
        const std::uintptr_t ret = reinterpret_cast<const std::uintptr_t*>(record)[1];
        if (ret == frame->call_return) return;  // the boundary: every frame was followed
        if (in_mod_code(frame, ret)) {
            chain.add(ret);
            in_mods = true;
        } else if (in_mods) {
            break;
        }
        const std::uintptr_t next = reinterpret_cast<const std::uintptr_t*>(record)[0];
        if (next <= record) break;
        record = next;
    }
    chain.broken();
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
std::atomic<std::uint32_t> g_reported{0};

// The crash report of a fault that ends the game: what it was and where, and the guard's sentence when it saw the
// fault in a mod's code and did not resume it. Written once, before a handler installed earlier (the host's own crash
// log, which writes the stack) or the signal's default action. Only what a signal handler may use.
void report_fault(int signal, siginfo_t* info, void* raw) noexcept {
    if (g_reported.exchange(1u, std::memory_order_acq_rel) != 0u) return;
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
    if (noted) {
        (void)!write(2, "crash: guard: ", 14);
        (void)!write(2, g_note, std::strlen(g_note));
        (void)!write(2, "\n", 1);
    }
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
    if (thread->handling != 0u) {
        // A second fault while the first is handled: the chain the kernel is reading stays as it is.
        note(thread, frame, fault, regs.pc, "a second fault came while the first was handled");
        report_fault(signal, info, raw);
        galaxy::mod_crash::write(g_note_mod);
        pass_on(signal, info, raw);
        return;
    }
    Chain chain(thread, frame, regs.pc);
    chain_by_records(chain, frame, regs);
    const char* why = why_not_here(thread, frame, fault, regs.pc, regs.sp);
    if (why == nullptr) why = why_not_words(frame, regs.sp, regs.lr);
    if (why != nullptr) {
        // Not resumed: the game ends, so its crash lines go out now, whichever handler ends it, and the crash record
        // names the mod (mod_crash.h: the platform's handler after this one leaves it).
        chain.broken();
        note(thread, frame, fault, regs.pc, why);
        report_fault(signal, info, raw);
        galaxy::mod_crash::write(g_note_mod);
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

const LumaGuardHost kHost = {sizeof(LumaGuardHost), thread_enter, code_range, function_at};

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
    g_blame = reinterpret_cast<LumaGuardBlame>(find_symbol("luma_guard_blame"));
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

void add_library(void* library) noexcept {
    std::uintptr_t base = 0, begin = 0, end = 0;
    if (library == nullptr || code_range(library, &base, &begin, &end) != 0) return;
    if (!g_spans.add(begin, end)) {
        // No memory for a chunk of the table: said; this library's faults are then resumed only under its own boundary.
        static const char kNoRoom[] = "guard: no memory to keep a mod library's code; a fault in it under another mod's call ends the game\n";
#if defined(_WIN32)
        OutputDebugStringA(kNoRoom);
#else
        (void)!write(2, kNoRoom, sizeof(kNoRoom) - 1);
#endif
    }
}

void remove_library(void* library) noexcept {
    std::uintptr_t base = 0, begin = 0, end = 0;
    if (library == nullptr || code_range(library, &base, &begin, &end) != 0) return;
    g_spans.remove(begin);
}

void set_crash_line(CrashLine line) noexcept {
#if defined(LUMA_GUARD_HANDLER) && !defined(_WIN32)
    g_crash_line = line;
#else
    (void)line;
#endif
}

}  // namespace galaxy::luma_guard
