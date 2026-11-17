// SPDX-License-Identifier: GPL-3.0-only
// The set of mods a mod needs, directly or through others (LumaLoader modUsesThrough): sorted by address and built once,
// when the mod's imports are bound, from the mods it imports from and each one's own set (each was bound before it: a
// mod loads after every mod it needs). A test of the set is a binary search, so its time and stack do not grow with the
// shape of the need graph; it runs on every LumaCore.call_mod and in a fault handler (reads only). Pure, for the unit
// test (native/host/luma_guard_walk_tests.cpp).
#pragma once

#include <cstdint>

namespace luma::needs {

    namespace detail {
        template <typename Mod>
        inline bool before(const Mod* pA, const Mod* pB) {
            return reinterpret_cast<uintptr_t>(pA) < reinterpret_cast<uintptr_t>(pB);
        }

        template <typename Mod>
        inline void siftDown(Mod** pItems, uint32_t root, uint32_t count) {
            for (;;) {
                uint32_t child = root * 2u + 1u;
                if (child >= count) {
                    return;
                }
                if (child + 1u < count && before(pItems[child], pItems[child + 1u])) {
                    child++;
                }
                if (!before(pItems[root], pItems[child])) {
                    return;
                }
                Mod* const pSwap = pItems[root];
                pItems[root] = pItems[child];
                pItems[child] = pSwap;
                root = child;
            }
        }
    }  // namespace detail

    // Writes into pOut (room for `room`: useCount plus the size of every use's set) the mods in pUses and in their sets
    // (setOf(pUse, &pSet, &count) gives a use's), sorted by address with no repeats. Returns how many. No allocation.
    template <typename Mod, typename SetOf>
    uint32_t close(Mod* const* pUses, uint32_t useCount, SetOf setOf, Mod** pOut, uint32_t room) {
        uint32_t n = 0;
        for (uint32_t i = 0; i < useCount; i++) {
            if (pUses[i] == nullptr) {
                continue;
            }
            if (n < room) {
                pOut[n++] = pUses[i];
            }
            Mod* const* pSet = nullptr;
            uint32_t setCount = 0;
            setOf(pUses[i], &pSet, &setCount);
            for (uint32_t k = 0; k < setCount && n < room; k++) {
                pOut[n++] = pSet[k];
            }
        }
        // Heap sort (no recursion, no allocation), then drop repeats.
        for (uint32_t i = n / 2u; i > 0; i--) {
            detail::siftDown(pOut, i - 1u, n);
        }
        for (uint32_t end = n; end > 1u; end--) {
            Mod* const pTop = pOut[0];
            pOut[0] = pOut[end - 1u];
            pOut[end - 1u] = pTop;
            detail::siftDown(pOut, 0u, end - 1u);
        }
        uint32_t kept = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (kept == 0 || pOut[kept - 1u] != pOut[i]) {
                pOut[kept++] = pOut[i];
            }
        }
        return kept;
    }

    // Whether the sorted set holds pMod.
    template <typename Mod>
    bool has(Mod* const* pSet, uint32_t count, const Mod* pMod) {
        uint32_t low = 0, high = count;
        while (low < high) {
            const uint32_t mid = low + (high - low) / 2u;
            if (detail::before<Mod>(pSet[mid], pMod)) {
                low = mid + 1u;
            } else {
                high = mid;
            }
        }
        return pMod != nullptr && low < count && pSet[low] == pMod;
    }

    // Takes pMod out of the sorted set; returns the new count.
    template <typename Mod>
    uint32_t drop(Mod** pSet, uint32_t count, const Mod* pMod) {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (pSet[i] != pMod) {
                pSet[kept++] = pSet[i];
            }
        }
        return kept;
    }

}  // namespace luma::needs
