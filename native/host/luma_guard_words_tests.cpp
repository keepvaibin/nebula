// Tests for luma_guard_words.h: the guard's word scan on stacks made here, laid out as luma_guard_call lays out its
// own words on each calling convention (native/petari/src/Game/System/LumaGuard.cpp), with the mod's frames below.
#include "luma_guard_words.h"

#include <luma_guard.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using galaxy::luma_guard::CodeSpan;
using galaxy::luma_guard::scan_words;
using galaxy::luma_guard::Words;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// Code ranges far from any real address: only the values matter.
constexpr CodeSpan kMod{0x10000000u, 0x10010000u};
constexpr CodeSpan kGame{0x20000000u, 0x20100000u};
constexpr CodeSpan kHost{0x30000000u, 0x30100000u};
constexpr std::uintptr_t kCallReturn = 0x200000a0u;  // luma_guard_call's return address (in the game module)
constexpr std::uintptr_t kModFunction = 0x10000400u;  // the mod's handler, as a saved register may hold it
constexpr std::uintptr_t kModReturn = 0x10000480u;    // a return address into the mod (the mod called its own)
constexpr std::uintptr_t kGameWord = 0x20000777u;     // a game address a returned call left behind
constexpr std::uintptr_t kGuardRun = 0x20000555u;     // the return address into guard::run

// A stack, lowest address first: `mods` (the mod's frames, from the fault's stack pointer up), then `kernel`
// (luma_guard_call's own words, call_bytes of them), then the word the boundary's stack pointer points at.
struct Stack {
    std::vector<std::uintptr_t> words;
    std::uintptr_t sp = 0;
    std::uintptr_t boundary = 0;
    Stack(const std::vector<std::uintptr_t>& mods, const std::vector<std::uintptr_t>& kernel) {
        words = mods;
        words.insert(words.end(), kernel.begin(), kernel.end());
        words.push_back(0);  // above the boundary: the caller's
        sp = reinterpret_cast<std::uintptr_t>(words.data());
        boundary = sp + (mods.size() + kernel.size()) * sizeof(std::uintptr_t);
    }
};

Words scan(const Stack& s, std::uintptr_t call_bytes, std::uintptr_t lr = 0) {
    return scan_words(s.sp, s.boundary, call_bytes, lr, kCallReturn, kMod, kGame, kHost);
}

}  // namespace

int main() {
    bool ok = true;

    // win-x64: luma_guard_call's 56 bytes below the boundary: the call's return slot, 32 bytes of shadow space, rbx,
    // its own return address. A game word left in the shadow space and the mod's function address in the saved rbx:
    // the kernel's words, not the mod's frames.
    {
        const Stack s({0, 0, 0}, {kCallReturn, kGameWord, 0, 0, 0, kModFunction, kGuardRun});
        ok &= expect(scan(s, 56) == Words::ModOnly, "win-x64: luma_guard_call's own words are not read (M-027)");
        ok &= expect(scan(s, 0) == Words::GameBetween, "win-x64: read whole, the kernel's words looked like a call back into the mod");
    }
    // sysv-x64: 24 bytes: the call's return slot, rbx, its own return address.
    {
        const Stack s({kGameWord, 0}, {kCallReturn, kModFunction, kGuardRun});
        ok &= expect(scan(s, 24) == Words::ModOnly, "sysv-x64: luma_guard_call's own words are not read (M-027)");
        ok &= expect(scan(s, 0) == Words::GameBetween, "sysv-x64: read whole, a game word below the saved rbx looked like a call back");
    }
    // aarch64: 32 bytes: fp, lr (into guard::run), x19, padding. No stale word is needed: x30 is a game address below
    // x19.
    {
        const Stack s({0, 0}, {0, kGuardRun, kModFunction, 0});
        ok &= expect(scan(s, 32) == Words::ModOnly, "aarch64: luma_guard_call's own words are not read (M-027)");
        ok &= expect(scan(s, 0) == Words::GameBetween, "aarch64: read whole, x30 below x19 looked like a call back");
    }

    // What the scan is for, unchanged: the mod called the game, which called the mod again, and the fault is in the
    // inner call. Within the mod's frames: a return address into the mod (outer), above one into the game.
    {
        const Stack s({0, kGameWord, 0, kModReturn, 0, 0, 0}, {kCallReturn, 0, 0, 0, 0, kModFunction, kGuardRun});
        ok &= expect(scan(s, 56) == Words::GameBetween, "a game frame between the fault and the boundary is still found");
    }
    {
        const Stack s({0, 0}, {0, kGuardRun, 0, 0});
        ok &= expect(scan(s, 32, kGameWord) == Words::ModOnly, "a game address in lr alone is no frame between");
        const Stack t({0, kModReturn}, {0, kGuardRun, 0, 0});
        ok &= expect(scan(t, 32, kGameWord) == Words::GameBetween, "lr into the game below a mod return address is one");
    }
    // The host's code counts as the game's; luma_guard_call's return address in the mod's frames is skipped.
    {
        const Stack s({0x30000010u, kModReturn}, {kCallReturn, 0, kGuardRun});
        ok &= expect(scan(s, 24) == Words::GameBetween, "a host word below a mod return address is a frame between");
        const Stack t({kCallReturn, kModReturn}, {kCallReturn, 0, kGuardRun});
        ok &= expect(scan(t, 24) == Words::ModOnly, "luma_guard_call's return address is not the game's frame");
    }
    // The constant the guard uses is this system's layout (the same stacks as above, by LUMA_GUARD_CALL_BYTES).
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
    ok &= expect(LUMA_GUARD_CALL_BYTES == 56, "win-x64: LUMA_GUARD_CALL_BYTES is luma_guard_call's 56 bytes");
#elif defined(__x86_64__)
    ok &= expect(LUMA_GUARD_CALL_BYTES == 24, "sysv-x64: LUMA_GUARD_CALL_BYTES is luma_guard_call's 24 bytes");
#elif defined(__aarch64__) || defined(_M_ARM64)
    ok &= expect(LUMA_GUARD_CALL_BYTES == 32, "aarch64: LUMA_GUARD_CALL_BYTES is luma_guard_call's 32 bytes");
#endif

    // A boundary below the kernel's own words cannot be a stack.
    ok &= expect(scan_words(0, 16, 24, 0, kCallReturn, kMod, kGame, kHost) == Words::Unreadable, "a boundary under 24 bytes is unreadable");

    if (ok) std::cout << "luma_guard_words: all passed\n";
    return ok ? 0 : 1;
}
