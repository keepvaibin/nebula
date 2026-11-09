// SPDX-License-Identifier: GPL-3.0-only
// Replacing a function of the game where MinHook isn't: Linux and Mac. A hook overwrites the start of the function with a
// jump to the replacement. What it overwrote moves to a small piece of new code, its trampoline, which goes on to the rest
// of the function, so that the replacement can call the original.
//
// Hooks are only made while plugins start, on one thread, with the game not running yet, so no thread can be inside what's
// being overwritten.
#include "galaxy/mod_hook.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <pthread.h>
#endif

#if defined(__x86_64__)
extern "C" {
#include "../../third_party/minhook/src/hde/hde64.h"
}
#endif

namespace galaxy::mods {
namespace {

constexpr std::size_t kTrampolineSize = 128;

std::uint8_t* new_code_page_near(const void* target) {
    const auto page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__APPLE__) && defined(__aarch64__)
    flags |= MAP_JIT;
#endif
    // Within two gigabytes when it can be: a relative jump is then enough, and relative addresses in moved code stay valid.
    const auto start = reinterpret_cast<std::uintptr_t>(target) & ~(page - 1);
    for (std::uintptr_t step = page * 16; step < 0x7000'0000u; step *= 2) {
        for (const std::uintptr_t hint : {start + step, start - step}) {
            void* const got = mmap(reinterpret_cast<void*>(hint), page, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
            if (got == MAP_FAILED) continue;
            const auto at = reinterpret_cast<std::uintptr_t>(got);
            const std::uintptr_t distance = at > start ? at - start : start - at;
            if (distance < 0x7000'0000u) return static_cast<std::uint8_t*>(got);
            munmap(got, page);
        }
    }
    void* const got = mmap(nullptr, page, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
    return got == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(got);
}

// One page, many trampolines.
std::uint8_t* new_trampoline(const void* target) {
    static std::uint8_t* page = nullptr;
    static std::size_t used = 0;
    const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    if (page != nullptr) {
        const auto at = reinterpret_cast<std::uintptr_t>(page);
        const auto wanted = reinterpret_cast<std::uintptr_t>(target);
        const std::uintptr_t distance = at > wanted ? at - wanted : wanted - at;
        if (used + kTrampolineSize <= page_size && distance < 0x7000'0000u) {
            std::uint8_t* const out = page + used;
            used += kTrampolineSize;
            return out;
        }
    }
    page = new_code_page_near(target);
    used = kTrampolineSize;
    return page;
}

bool make_writable(void* target, std::size_t size) {
    const auto page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto start = reinterpret_cast<std::uintptr_t>(target) & ~(page - 1);
    const std::size_t length = (reinterpret_cast<std::uintptr_t>(target) + size - start + page - 1) & ~(page - 1);
#if defined(__APPLE__)
    // Copy on write: a signed library's pages can't simply be made writable.
    return vm_protect(mach_task_self(), start, length, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS;
#else
    void* const at = reinterpret_cast<void*>(start);
    if (mprotect(at, length, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) return true;
    // Android doesn't let an app run a library's page once it is changed (SELinux: no execmod). The page becomes memory of
    // its own instead, at the same address and with the same bytes, which may be changed and run. mremap puts it in place
    // in one step: if it can't, the library's page stays as it was.
    void* const copy = mmap(nullptr, length, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (copy == MAP_FAILED) return false;
    std::memcpy(copy, at, length);
    if (mremap(copy, length, length, MREMAP_MAYMOVE | MREMAP_FIXED, at) == at) return true;
    munmap(copy, length);
    return false;
#endif
}

void make_executable(void* target, std::size_t size) {
    const auto page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto start = reinterpret_cast<std::uintptr_t>(target) & ~(page - 1);
    const std::size_t length = (reinterpret_cast<std::uintptr_t>(target) + size - start + page - 1) & ~(page - 1);
    mprotect(reinterpret_cast<void*>(start), length, PROT_READ | PROT_EXEC);
}

void code_written(void* at, std::size_t size) {
#if defined(__APPLE__)
    sys_icache_invalidate(at, size);
#else
    __builtin___clear_cache(static_cast<char*>(at), static_cast<char*>(at) + size);
#endif
}

void trampolines_writable(bool writable) {
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(writable ? 0 : 1);
#else
    (void)writable;
#endif
}

#if defined(__x86_64__)

constexpr std::size_t kNearJump = 5;
constexpr std::size_t kFarJump = 14;

void write_far_jump(std::uint8_t* at, const void* to) {
    // jmp [rip+0], then the address.
    at[0] = 0xFF;
    at[1] = 0x25;
    std::memset(at + 2, 0, 4);
    std::memcpy(at + 6, &to, 8);
}

bool near_enough(const void* from, std::size_t length, const void* to) {
    const auto next = reinterpret_cast<std::intptr_t>(from) + static_cast<std::intptr_t>(length);
    const std::intptr_t distance = reinterpret_cast<std::intptr_t>(to) - next;
    return distance >= INT32_MIN && distance <= INT32_MAX;
}

// Moves whole instructions from `target` to `out` until at least `wanted` bytes have moved. A relative address is rewritten
// for its new place. Anything it can't move safely stops the hook.
bool move_instructions(const std::uint8_t* target, std::size_t wanted, std::uint8_t* out, std::size_t* moved) {
    std::size_t from = 0;
    std::size_t to = 0;
    while (from < wanted) {
        hde64s hs{};
        const std::size_t length = hde64_disasm(target + from, &hs);
        if ((hs.flags & F_ERROR) != 0 || length == 0) return false;
        if (to + length + 4 > kTrampolineSize - kFarJump) return false;
        if ((hs.flags & F_RELATIVE) != 0 && (hs.flags & F_IMM8) != 0) {
            // A short jump: as the long form of the same jump. Where it lands has to be past what's being moved.
            const std::uint8_t opcode = target[from];
            const std::int8_t distance = static_cast<std::int8_t>(target[from + 1]);
            const std::intptr_t lands = static_cast<std::intptr_t>(from + length) + distance;
            if (lands >= 0 && lands < static_cast<std::intptr_t>(wanted)) return false;
            std::size_t head = 0;
            if (opcode == 0xEB) {
                out[to] = 0xE9;
                head = 1;
            } else if (opcode >= 0x70 && opcode <= 0x7F) {
                out[to] = 0x0F;
                out[to + 1] = static_cast<std::uint8_t>(opcode + 0x10);
                head = 2;
            } else {
                return false;  // loop and jrcxz have no long form
            }
            const std::intptr_t aimed = reinterpret_cast<std::intptr_t>(target + from + length) + distance;
            const std::intptr_t now = aimed - reinterpret_cast<std::intptr_t>(out + to + head + 4);
            if (now < INT32_MIN || now > INT32_MAX) return false;
            const auto rel = static_cast<std::int32_t>(now);
            std::memcpy(out + to + head, &rel, 4);
            from += length;
            to += head + 4;
            continue;
        }
        std::memcpy(out + to, target + from, length);
        if ((hs.modrm & 0xC7) == 0x05 && (hs.flags & F_MODRM) != 0) {
            // [rip+disp32]: its displacement is just before any immediate.
            const std::size_t immediate = (hs.flags & F_IMM8) ? 1 : (hs.flags & F_IMM16) ? 2 : (hs.flags & F_IMM32) ? 4 : (hs.flags & F_IMM64) ? 8 : 0;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, out + to + length - immediate - 4, 4);
            const std::intptr_t aimed = reinterpret_cast<std::intptr_t>(target + from + length) + displacement;
            const std::intptr_t now = aimed - reinterpret_cast<std::intptr_t>(out + to + length);
            if (now < INT32_MIN || now > INT32_MAX) return false;
            displacement = static_cast<std::int32_t>(now);
            std::memcpy(out + to + length - immediate - 4, &displacement, 4);
        } else if ((hs.flags & F_RELATIVE) != 0) {
            // call or jmp rel32, jcc rel32.
            std::int32_t displacement = 0;
            std::memcpy(&displacement, out + to + length - 4, 4);
            const std::intptr_t aimed = reinterpret_cast<std::intptr_t>(target + from + length) + displacement;
            const std::intptr_t now = aimed - reinterpret_cast<std::intptr_t>(out + to + length);
            if (now < INT32_MIN || now > INT32_MAX) return false;
            displacement = static_cast<std::int32_t>(now);
            std::memcpy(out + to + length - 4, &displacement, 4);
        }
        from += length;
        to += length;
    }
    *moved = from;
    write_far_jump(out + to, target + from);
    return true;
}

bool install(void* target, void* replacement, void** original) {
    auto* const code = static_cast<std::uint8_t*>(target);
    std::uint8_t* const trampoline = new_trampoline(target);
    if (trampoline == nullptr) return false;
    const bool near = near_enough(target, kNearJump, replacement);
    const std::size_t wanted = near ? kNearJump : kFarJump;
    std::size_t moved = 0;
    if (!move_instructions(code, wanted, trampoline, &moved)) return false;
    code_written(trampoline, kTrampolineSize);
    if (!make_writable(target, moved)) return false;
    if (near) {
        code[0] = 0xE9;
        const auto distance = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(replacement) - reinterpret_cast<std::intptr_t>(code + kNearJump));
        std::memcpy(code + 1, &distance, 4);
    } else {
        write_far_jump(code, replacement);
    }
    for (std::size_t i = wanted; i < moved; ++i) code[i] = 0xCC;
    make_executable(target, moved);
    code_written(target, moved);
    *original = trampoline;
    return true;
}

#elif defined(__aarch64__)

constexpr std::size_t kJumpWords = 4;  // ldr x16, #8; br x16; the address

void write_jump(std::uint32_t* at, const void* to) {
    at[0] = 0x58000050u;  // ldr x16, #8
    at[1] = 0xD61F0200u;  // br x16
    std::memcpy(at + 2, &to, 8);
}

std::int64_t sign_extend(std::uint64_t value, int bits) {
    const std::uint64_t sign = 1ull << (bits - 1);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

// One instruction moved from `pc` to `out`: its relative address made absolute, in as many words as that takes. How many
// words it wrote; 0: it can't be moved.
std::size_t move_one(std::uint32_t instruction, const std::uint32_t* pc, std::uint32_t* out) {
    const auto here = reinterpret_cast<std::uint64_t>(pc);
    // B, BL: a jump to the address (BL returns to after what's written).
    if ((instruction & 0x7C000000u) == 0x14000000u) {
        const std::uint64_t to = here + static_cast<std::uint64_t>(sign_extend(instruction & 0x03FFFFFFu, 26) * 4);
        if ((instruction & 0x80000000u) == 0u) {
            write_jump(out, reinterpret_cast<const void*>(to));
            return 4;
        }
        out[0] = 0x58000070u;  // ldr x16, #12
        out[1] = 0x10000000u | (4u << 5) | 30u;  // adr x30, #16: after the address
        out[2] = 0xD61F0200u;  // br x16
        std::memcpy(out + 3, &to, 8);
        return 5;
    }
    // ADR, ADRP: the value, loaded.
    if ((instruction & 0x1F000000u) == 0x10000000u) {
        const std::uint32_t rd = instruction & 31u;
        const std::uint64_t immediate = ((instruction >> 29) & 3u) | (((instruction >> 5) & 0x7FFFFu) << 2);
        std::uint64_t value = 0;
        if ((instruction & 0x80000000u) != 0u) {
            value = (here & ~0xFFFull) + static_cast<std::uint64_t>(sign_extend(immediate, 21) * 4096);
        } else {
            value = here + static_cast<std::uint64_t>(sign_extend(immediate, 21));
        }
        out[0] = 0x58000040u | rd;  // ldr rd, #8
        out[1] = 0x14000003u;       // b past the value
        std::memcpy(out + 2, &value, 8);
        return 4;
    }
    // LDR (literal), for 32 and 64 bits: the address, then the load through it.
    if ((instruction & 0xBF000000u) == 0x18000000u) {
        const std::uint32_t rt = instruction & 31u;
        const std::uint64_t to = here + static_cast<std::uint64_t>(sign_extend((instruction >> 5) & 0x7FFFFu, 19) * 4);
        const std::uint32_t is64 = (instruction >> 30) & 1u;
        out[0] = 0x58000060u | 16u;  // ldr x16, #12
        out[1] = (is64 != 0u ? 0xF9400000u : 0xB9400000u) | (16u << 5) | rt;
        out[2] = 0x14000003u;  // past the address
        std::memcpy(out + 3, &to, 8);
        return 5;
    }
    // The other loads by a relative address (signed words, floating point): nothing here moves them.
    if ((instruction & 0x3B000000u) == 0x18000000u) return 0;
    // B.cond, CBZ, CBNZ, TBZ, TBNZ: the same test over a jump.
    const bool condition = (instruction & 0xFF000010u) == 0x54000000u;
    const bool compare = (instruction & 0x7E000000u) == 0x34000000u;
    const bool test = (instruction & 0x7E000000u) == 0x36000000u;
    if (condition || compare || test) {
        const int bits = test ? 14 : 19;
        const std::uint32_t field = (instruction >> 5) & ((1u << bits) - 1u);
        const std::uint64_t to = here + static_cast<std::uint64_t>(sign_extend(field, bits) * 4);
        out[0] = (instruction & ~(((1u << bits) - 1u) << 5)) | (2u << 5);  // the test, over the next word
        out[1] = 0x14000005u;                                              // not taken: past the jump
        write_jump(out + 2, reinterpret_cast<const void*>(to));
        return 6;
    }
    // Anything else doesn't depend on where it is.
    out[0] = instruction;
    return 1;
}

bool install(void* target, void* replacement, void** original) {
    auto* const code = static_cast<std::uint32_t*>(target);
    auto* const trampoline = reinterpret_cast<std::uint32_t*>(new_trampoline(target));
    if (trampoline == nullptr) return false;
    trampolines_writable(true);
    std::size_t to = 0;
    bool moved_all = true;
    for (std::size_t from = 0; from < kJumpWords; ++from) {
        const std::size_t words = move_one(code[from], code + from, trampoline + to);
        if (words == 0 || to + words + kJumpWords > kTrampolineSize / 4) {
            moved_all = false;
            break;
        }
        to += words;
    }
    if (moved_all) write_jump(trampoline + to, code + kJumpWords);
    trampolines_writable(false);
    if (!moved_all) return false;
    code_written(trampoline, kTrampolineSize);
    if (!make_writable(target, kJumpWords * 4)) return false;
    write_jump(code, replacement);
    make_executable(target, kJumpWords * 4);
    code_written(target, kJumpWords * 4);
    *original = trampoline;
    return true;
}

#else

bool install(void*, void*, void**) {
    return false;
}

#endif

}  // namespace

bool hook_function(void* target, void* replacement, void** original) {
    return install(target, replacement, original);
}

}  // namespace galaxy::mods
