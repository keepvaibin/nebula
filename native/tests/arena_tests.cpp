// SPDX-License-Identifier: GPL-3.0-only
// NebulaArena (Game/System/NebulaArena.cpp): blocks of every size class are aligned, disjoint and in their arena; a freed
// block comes back zeroed; the host commits whole steps; a bad size or alignment, and an allocation before the root
// arena, panic; owned objects and child arenas go with their arena, once; destroying an arena tells every table that
// keeps addresses in it and gives its addresses back; the current arena after a destroy and in a scope.
#include "Game/System/NebulaArena.hpp"

#include "Game/Render/RenderTexture.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/LumaAnim.hpp"
#include "Game/System/LumaContentFx.hpp"
#include "Game/System/LumaContentModel.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaDraw.hpp"
#include "Game/System/LumaDrawInst.hpp"
#include "Game/System/LumaDrawView.hpp"
#include "Game/System/LumaPlayer.hpp"
#include "Game/System/LumaUi.hpp"

#include "test_support.h"

#include <algorithm>
#include <chrono>
#include <stdint.h>
#include <vector>
#include <stdio.h>
#include <string.h>

extern "C" {
extern uint64_t arena_test_reserved;
extern uint64_t arena_test_committed;
extern uint64_t arena_test_commit_calls;
extern uint64_t arena_test_odd_commits;
extern void (*arena_test_on_commit)(void);
}

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    // Each table told of a range as an arena goes: which, and the range.
    struct Released {
        int mTable;
        uintptr_t mStart;
        uintptr_t mEnd;
    };
    Released sReleased[256];
    int sReleasedCount;

    void note(int table, const void* pStart, const void* pEnd) {
        if (sReleasedCount < 256) {
            sReleased[sReleasedCount++] = {table, reinterpret_cast< uintptr_t >(pStart), reinterpret_cast< uintptr_t >(pEnd)};
        }
    }

    enum { cForget = 9, cUnload = 10 };

    // How many of the nine tables, the textures and the files were told of the arena that started at `start`.
    int toldOf(const void* start) {
        unsigned seen = 0;
        for (int i = 0; i < sReleasedCount; i++) {
            if (sReleased[i].mStart == reinterpret_cast< uintptr_t >(start)) {
                seen |= 1u << sReleased[i].mTable;
            }
        }
        int count = 0;
        for (; seen != 0; seen &= seen - 1) {
            count++;
        }
        return count;
    }

    int sDestroyed;

    class Owned : public ArenaOwned {
    public:
        explicit Owned(int tag) : mTag(tag) {
        }
        ~Owned() override {
            sDestroyed++;
            mTag = -1;
        }
        int mTag;
    };

    bool aligned(const void* p, u32 alignment) {
        return (reinterpret_cast< uintptr_t >(p) & (alignment - 1)) == 0;
    }

    bool inside(const NebulaArena* pArena, const void* p, u32 size) {
        return p >= pArena->getStartAddr() && static_cast< const u8* >(p) + size <= pArena->getEndAddr();
    }
}  // namespace

// The tables NebulaArena::destroy tells; the textures and file views.
namespace lumacore {
    void released(const void* pStart, const void* pEnd) {
        note(0, pStart, pEnd);
    }
}  // namespace lumacore
namespace luma {
    namespace content {
        namespace fx {
            void released(const void* pStart, const void* pEnd) {
                note(1, pStart, pEnd);
            }
        }  // namespace fx
        namespace model {
            void released(const void* pStart, const void* pEnd) {
                note(2, pStart, pEnd);
            }
        }  // namespace model
    }  // namespace content
    namespace draw {
        void released(const void* pStart, const void* pEnd) {
            note(3, pStart, pEnd);
        }
        namespace inst {
            void released(const void* pStart, const void* pEnd) {
                note(4, pStart, pEnd);
            }
        }  // namespace inst
        namespace view {
            void released(const void* pStart, const void* pEnd) {
                note(5, pStart, pEnd);
            }
        }  // namespace view
    }  // namespace draw
    namespace ui {
        void released(const void* pStart, const void* pEnd) {
            note(6, pStart, pEnd);
        }
    }  // namespace ui
    namespace anim {
        void released(const void* pStart, const void* pEnd) {
            note(7, pStart, pEnd);
        }
    }  // namespace anim
    namespace player {
        void released(const void* pStart, const void* pEnd) {
            note(8, pStart, pEnd);
        }
    }  // namespace player
}  // namespace luma
namespace RenderTexture {
    void forget(uintptr_t start, uintptr_t end) {
        note(cForget, reinterpret_cast< const void* >(start), reinterpret_cast< const void* >(end));
    }
}  // namespace RenderTexture
namespace GameAssets {
    void unloadArena(const NebulaArena* pArena) {
        note(cUnload, pArena->getStartAddr(), pArena->getEndAddr());
    }
}  // namespace GameAssets

int main() {
    // Before the root arena there is nowhere to put a block: a stop that says so, never a null `new` hands back.
    expect(NEBULA_TEST_PANICS((void)operator new(16)), "new before the root arena panics");
    expect(strstr(nebula_test_panic_text, "arena") != nullptr, "the panic names the arena");
    expect(NebulaArena::current() == nullptr, "no arena yet");

    NebulaArena* const pRoot = NebulaArena::create(nullptr);
    expect(NebulaArena::root() == pRoot && NebulaArena::current() == pRoot, "the root is current");
    const uint64_t rootReserved = arena_test_reserved;
    expect(rootReserved != 0, "the root reserved its addresses");

    // Every class (16-byte steps to 1 KiB, then four to a doubling) at each alignment: aligned, inside, writable, disjoint.
    NebulaArena* const pScene = NebulaArena::create(pRoot);
    {
        static const u32 cAlign[] = {0, 1, 4, 16, 32, 64, 256, 4096};  // 0: the game's "any"
        static void* sBlocks[2048];
        static u32 sSizes[2048];
        u32 count = 0;
        for (u32 size = 0; size <= 70000 && count < 2040; size = size < 1100 ? size + 7 : size * 5 / 4) {
            for (u32 a = 0; a < sizeof(cAlign) / sizeof(cAlign[0]) && count < 2040; a++) {
                u8* const p = static_cast< u8* >(pScene->alloc(size, static_cast< int >(cAlign[a])));
                const u32 want = cAlign[a] < 16 ? 16 : cAlign[a];
                expect(aligned(p, want), "a block is aligned");
                expect(inside(pScene, p, size), "a block lies in its arena");
                expect(NebulaArena::findFromRoot(p) == pScene, "findFromRoot finds a block's arena");
                memset(p, static_cast< int >(count & 0xFF), size);
                sBlocks[count] = p;
                sSizes[count] = size;
                count++;
            }
        }
        bool intact = true;
        for (u32 i = 0; i < count; i++) {
            const u8* const p = static_cast< const u8* >(sBlocks[i]);
            for (u32 b = 0; b < sSizes[i]; b++) {
                if (p[b] != static_cast< u8 >(i & 0xFF)) {
                    intact = false;
                    break;
                }
            }
        }
        expect(intact, "no block overlaps another");
        expect(arena_test_odd_commits == 0, "the host commits whole MiB steps");
    }

    // A freed block comes back for the same size, zeroed as new memory is.
    {
        u8* const p = static_cast< u8* >(pScene->alloc(200, 16));
        memset(p, 0xAB, 200);
        pScene->free(p);
        u8* const q = static_cast< u8* >(pScene->alloc(200, 16));
        expect(q == p, "a freed block is taken again for its size");
        bool zero = true;
        for (u32 b = 0; b < 200; b++) {
            zero = zero && q[b] == 0;
        }
        expect(zero, "a reused block is zeroed");
        // Freed through the static free, which finds the block's arena by its address.
        NebulaArena::free(q, nullptr);
        expect(pScene->alloc(200, 16) == q, "the static free finds the block's arena");
    }

    // A large block commits as it grows; its ends are writable.
    {
        const uint64_t before = arena_test_committed;
        u8* const p = static_cast< u8* >(pScene->alloc(64u << 20, 16));
        p[0] = 1;
        p[(64u << 20) - 1] = 2;
        expect(arena_test_committed - before >= (64ull << 20), "64 MiB committed for a 64 MiB block");
    }

    // A size past the largest block (a corrupt length) and an alignment that is not a power of two panic.
    expect(NEBULA_TEST_PANICS(pScene->alloc(0x7FFFFFF0u, 16)), "a size past the largest block panics");
    expect(NEBULA_TEST_PANICS(pScene->alloc(32, 24)), "alignment 24 panics");
    expect(!NEBULA_TEST_PANICS(pScene->alloc(32, 0)), "alignment 0 is the game's any: 16");
    expect(NEBULA_TEST_PANICS(pScene->alloc(32, -4)), "a negative alignment panics");

    // Plain new goes to the current arena; a scope makes another current and gives the first back.
    {
        Owned* const pInRoot = new Owned(1);
        expect(NebulaArena::findFromRoot(pInRoot) == pRoot, "plain new uses the current arena");
        {
            NebulaArena::CurrentScope scope(pScene);
            expect(NebulaArena::current() == pScene, "a scope makes its arena current");
            Owned* const pInScene = new Owned(2);
            expect(NebulaArena::findFromRoot(pInScene) == pScene, "new in a scope uses its arena");
        }
        expect(NebulaArena::current() == pRoot, "the scope gives the arena back");
        delete pInRoot;
    }

    // Objects and child arenas go with their arena, each once; one deleted first is not destroyed again; every table is
    // told of each arena's range; its addresses go back to the host.
    {
        NebulaArena* const pChild = NebulaArena::create(pScene);
        NebulaArena* const pGrandchild = NebulaArena::create(pChild);
        Owned* const pA = new (pChild) Owned(3);
        Owned* const pB = new (pGrandchild) Owned(4);
        Owned* const pGone = new (pChild) Owned(5);
        void* const pBlock = pGrandchild->alloc(64, 16);
        sDestroyed = 0;
        delete pGone;
        expect(sDestroyed == 1, "a deleted object is destroyed");
        expect(pA->mTag == 3 && pB->mTag == 4, "the others stay");

        const uint64_t reservedBefore = arena_test_reserved;
        const uint64_t span = static_cast< uint64_t >(static_cast< const u8* >(pChild->getEndAddr()) -
                                                      static_cast< const u8* >(pChild->getStartAddr()));
        const void* const childStart = pChild->getStartAddr();
        const void* const grandchildStart = pGrandchild->getStartAddr();
        sReleasedCount = 0;
        sDestroyed = 0;
        NebulaArena::destroy(pChild);
        expect(sDestroyed == 2, "both remaining objects destroyed, once each");
        expect(NebulaArena::findFromRoot(pBlock) == nullptr, "a child arena's block is in no arena");
        expect(NebulaArena::findFromRoot(const_cast< void* >(childStart)) == nullptr, "the arena is gone");
        expect(arena_test_reserved == reservedBefore - 2 * span, "both arenas' addresses go back to the host");
        expect(toldOf(childStart) == 11, "every table, the textures and the files told of the child's range");
        expect(toldOf(grandchildStart) == 11, "every table, the textures and the files told of the grandchild's range");
    }

    // Destroying the current arena makes the root's first child current, or the root.
    {
        NebulaArena* const pNext = NebulaArena::create(pRoot);
        pNext->makeCurrent();
        NebulaArena::destroy(pNext);
        expect(NebulaArena::current() == pScene, "the root's first child is current after a destroy");
        pScene->makeCurrent();
        NebulaArena::destroy(pScene);
        expect(NebulaArena::current() == pRoot, "the root is current when it has no child");
        expect(arena_test_reserved == rootReserved, "only the root's addresses are held");
    }

    // Many arenas made and destroyed in a mixed order: findFromRoot finds the arena that holds an address, its first and
    // last byte included, and none past its end or for a destroyed one (P-013: a halving search over the arenas by
    // address). Timed against a walk over every arena, as findFromRoot was before.
    {
        const int cMany = 300;
        std::vector< NebulaArena* > live;
        u64 random = 0x2545F4914F6CDD1Dull;
        auto next = [&random]() {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            return random;
        };
        auto walk = [&live, pRoot](const void* pData) -> NebulaArena* {
            if (pData >= pRoot->getStartAddr() && pData < pRoot->getEndAddr()) {
                return pRoot;
            }
            for (NebulaArena* pArena : live) {
                if (pData >= pArena->getStartAddr() && pData < pArena->getEndAddr()) {
                    return pArena;
                }
            }
            return nullptr;
        };
        std::vector< const void* > gone;
        std::vector< NebulaArena* > parents;  // each live arena's parent
        for (int i = 0; i < cMany; i++) {
            NebulaArena* const pParent = live.empty() || next() % 2 == 0 ? pRoot : live[next() % live.size()];
            live.push_back(NebulaArena::create(pParent));
            parents.push_back(pParent);
            if (next() % 3 == 0) {
                // A childless one goes: its children would go with it.
                const size_t index = next() % live.size();
                NebulaArena* const pArena = live[index];
                if (std::find(parents.begin(), parents.end(), pArena) == parents.end()) {
                    gone.push_back(pArena->getStartAddr());
                    live.erase(live.begin() + static_cast< long >(index));
                    parents.erase(parents.begin() + static_cast< long >(index));
                    NebulaArena::destroy(pArena);
                }
            }
        }
        bool found = true;
        for (NebulaArena* pArena : live) {
            const u8* const pStart = static_cast< const u8* >(pArena->getStartAddr());
            const u8* const pEnd = static_cast< const u8* >(pArena->getEndAddr());
            const void* const probes[] = {pStart, pStart + 1, pEnd - 1, pEnd, pStart - 1, pStart + (pEnd - pStart) / 2};
            for (const void* pProbe : probes) {
                found = found && NebulaArena::findFromRoot(const_cast< void* >(pProbe)) == walk(pProbe);
            }
            found = found && NebulaArena::findFromRoot(const_cast< u8* >(pStart)) == pArena &&
                    NebulaArena::findFromRoot(const_cast< u8* >(pEnd - 1)) == pArena;
        }
        for (const void* pStart : gone) {
            found = found && NebulaArena::findFromRoot(const_cast< void* >(pStart)) == walk(pStart);
        }
        found = found && NebulaArena::findFromRoot(nullptr) == nullptr &&
                NebulaArena::findFromRoot(reinterpret_cast< void* >(~static_cast< uintptr_t >(0))) == nullptr;
        expect(found, "findFromRoot finds each address's arena among many, made and destroyed in a mixed order");

        // Each live arena's addresses, asked in a mixed order, as deletes ask.
        std::vector< void* > asks;
        for (int i = 0; i < 1 << 16; i++) {
            NebulaArena* const pArena = live[next() % live.size()];
            asks.push_back(static_cast< u8* >(const_cast< void* >(pArena->getStartAddr())) + next() % (1u << 30));
        }
        uintptr_t sum = 0;
        double searchBest = 1e30;
        double walkBest = 1e30;
        for (int run = 0; run < 5; run++) {
            const auto start = std::chrono::steady_clock::now();
            for (void* pAsk : asks) {
                sum += reinterpret_cast< uintptr_t >(NebulaArena::findFromRoot(pAsk));
            }
            const auto middle = std::chrono::steady_clock::now();
            for (void* pAsk : asks) {
                sum -= reinterpret_cast< uintptr_t >(walk(pAsk));
            }
            const auto end = std::chrono::steady_clock::now();
            const double searchTime = std::chrono::duration< double, std::nano >(middle - start).count() / asks.size();
            const double walkTime = std::chrono::duration< double, std::nano >(end - middle).count() / asks.size();
            searchBest = searchTime < searchBest ? searchTime : searchBest;
            walkBest = walkTime < walkBest ? walkTime : walkBest;
        }
        expect(sum == 0, "the search and the walk find the same arenas");
        printf("findFromRoot over %zu arenas: walk %.1f ns, search %.1f ns a lookup\n", live.size() + 1, walkBest, searchBest);

        // Last made first: each arena's children were made after it, so each goes childless.
        while (!live.empty()) {
            NebulaArena::destroy(live.back());
            live.pop_back();
        }
        expect(arena_test_reserved == rootReserved, "every arena's addresses go back to the host");
    }

    // Two users of the game's memory at once (P-012): a new that starts while another is inside the arena (here from
    // the host's commit, as a second thread's would) stops and says so, rather than sharing a free list. Last: the outer
    // new is left where the stop took it.
    {
        arena_test_on_commit = [] { arena_test_on_commit = nullptr; (void)operator new(16); };
        const bool stopped = NEBULA_TEST_PANICS((void)operator new(256u << 20));
        arena_test_on_commit = nullptr;
        expect(stopped, "a new inside another new stops");
        expect(strstr(nebula_test_panic_text, "two threads") != nullptr, "the stop names two users at once");
    }

    if (sFailures == 0) {
        printf("arena tests: all pass\n");
    }
    return sFailures == 0 ? 0 : 1;
}
