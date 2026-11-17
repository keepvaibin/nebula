// SPDX-License-Identifier: GPL-3.0-only
// The guard's word scan (luma_guard.cpp, why_not_words), as a function of its inputs alone so a test can give it a
// stack of its own making (luma_guard_words_tests.cpp).
//
// A fault in a mod with no unwind data (and, on POSIX, every fault) is resumed only when no frame of the game or the
// host lies between it and the mod's boundary. Without unwind data the frames cannot be followed, so the words on the
// stack are read: a word into the game's or the host's code below a word into the mod's code means the mod called the
// game, which called the mod again. A word a returned call left in a mod's frame counts too: the scan errs toward not
// resuming.
//
// The scan covers the mod's frames only: from the fault's stack pointer up to the lowest of luma_guard_call's own words
// (`call_bytes` below the boundary's stack pointer, LUMA_GUARD_CALL_BYTES). Those words are the kernel's: its saved
// registers (one holds the mod's function address on some paths of the game's own code) and the return address into
// the game's guard::run. Read as the mod's, they made a fault in a mod's service event never resumable (M-027).
#pragma once

#include <cstdint>

namespace galaxy::luma_guard {

struct CodeSpan {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    [[nodiscard]] constexpr bool has(std::uintptr_t at) const noexcept { return at >= begin && at < end; }
};

enum class Words { ModOnly, GameBetween, Unreadable };

// `sp`: the fault's stack pointer; `boundary`: the stack pointer luma_guard_call saved (regs[LUMA_GUARD_SP]); `lr`: the
// faulting thread's link register (0 where return addresses are on the stack); `call_return`: luma_guard_call's own
// return address, which is skipped; `mod`: the mod's code; `game`, `host`: the game module's and the executable's.
[[nodiscard]] inline Words scan_words(std::uintptr_t sp, std::uintptr_t boundary, std::uintptr_t call_bytes, std::uintptr_t lr,
                                      std::uintptr_t call_return, CodeSpan mod, CodeSpan game, CodeSpan host) noexcept {
    if (boundary < call_bytes) return Words::Unreadable;
    const std::uintptr_t top = boundary - call_bytes;
    bool game_seen = false;
    const auto look = [&](std::uintptr_t value) {
        if (value == call_return) return false;
        if (game.has(value) || host.has(value)) {
            game_seen = true;
        } else if (game_seen && mod.has(value)) {
            return true;
        }
        return false;
    };
    if (lr != 0 && look(lr)) return Words::GameBetween;
    for (std::uintptr_t at = sp & ~std::uintptr_t{7}; at < top; at += sizeof(std::uintptr_t)) {
        if (look(*reinterpret_cast<const std::uintptr_t*>(at))) return Words::GameBetween;
    }
    return Words::ModOnly;
}

}  // namespace galaxy::luma_guard
