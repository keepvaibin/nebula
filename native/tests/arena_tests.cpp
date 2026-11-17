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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
extern uint64_t arena_test_reserved;
extern uint64_t arena_test_committed;
extern uint64_t arena_test_commit_calls;
extern uint64_t arena_test_odd_commits;
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
        static const u32 cAlign[] = {1, 4, 16, 32, 64, 256, 4096};
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
    expect(NEBULA_TEST_PANICS(pScene->alloc(32, 0)), "alignment 0 panics");
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

    if (sFailures == 0) {
        printf("arena tests: all pass\n");
    }
    return sFailures == 0 ? 0 : 1;
}
