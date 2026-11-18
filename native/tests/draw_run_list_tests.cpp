// SPDX-License-Identifier: GPL-3.0-only
// The plain module's shape-drawer packets (Game/System/DrawRunList.cpp, P-038): random adds, removes, light changes and
// re-sorts give the same packets in the same order, with the same packets loading the light, as the hook module's
// array (DrawBufferShapeDrawer::add, remove and resetLightSort, copied here as the reference); and a clip-in or a
// clip-out among many packets costs about what it costs among few, where the array's cost grows with the packets.
#include "Game/System/DrawRunList.hpp"
#include "Game/System/NebulaArena.hpp"

#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <vector>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    uint32_t sSeed = 2024;

    u32 random(u32 below) {
        sSeed = sSeed * 1664525u + 1013904223u;
        return static_cast< u32 >((static_cast< u64 >(sSeed >> 8) * below) >> 24);
    }

    // An actor's light control as the sort sees it: its area light and whether it is blending.
    struct Ctrl {
        int mLight;
        bool mBlending;
    };

    bool sameLight(const void* pCtrl, const void* pLoader) {
        const Ctrl* a = static_cast< const Ctrl* >(pCtrl);
        const Ctrl* b = static_cast< const Ctrl* >(pLoader);
        return !a->mBlending && !b->mBlending && a->mLight == b->mLight;
    }

    bool blending(const void* pCtrl) {
        return static_cast< const Ctrl* >(pCtrl)->mBlending;
    }

    // DrawBufferShapeDrawer's array, as the hook module keeps it.
    struct ArrayDrawer {
        std::vector< DrawRunList::Packet > mPackets;

        s32 findLightSortIndex(const void* pCtrl, bool* pSetLightLoader) const {
            *pSetLightLoader = false;
            const s32 count = static_cast< s32 >(mPackets.size());
            if (pCtrl == nullptr) {
                return count;
            }
            if (blending(pCtrl)) {
                *pSetLightLoader = true;
                return count;
            }
            bool foundLight = false;
            for (s32 idx = 0; idx < count; idx++) {
                if (mPackets[idx].mLightLoader != nullptr) {
                    if (foundLight) {
                        return idx;
                    }
                    if (sameLight(pCtrl, mPackets[idx].mLightLoader)) {
                        foundLight = true;
                    }
                }
            }
            if (!foundLight) {
                *pSetLightLoader = true;
            }
            return count;
        }

        void add(const void* pEntry, const void* pCtrl) {
            bool setLightLoader = false;
            const s32 index = findLightSortIndex(pCtrl, &setLightLoader);
            DrawRunList::Packet packet = {pEntry, pCtrl, setLightLoader ? pCtrl : nullptr};
            mPackets.insert(mPackets.begin() + index, packet);
        }

        void remove(const void* pEntry) {
            const s32 count = static_cast< s32 >(mPackets.size());
            for (s32 removeIdx = 0; removeIdx < count; removeIdx++) {
                if (mPackets[removeIdx].mEntry == pEntry) {
                    const bool loadsLight = mPackets[removeIdx].mLightLoader != nullptr;
                    mPackets.erase(mPackets.begin() + removeIdx);
                    if (loadsLight && removeIdx < count - 1) {
                        mPackets[removeIdx].mLightLoader = mPackets[removeIdx].mLightCtrl;
                    }
                    return;
                }
            }
        }

        void resetLightSort(const void* pCtrl) {
            for (const DrawRunList::Packet& rPacket : mPackets) {
                if (rPacket.mLightCtrl == pCtrl) {
                    const void* const pEntry = rPacket.mEntry;
                    remove(pEntry);
                    add(pEntry, pCtrl);
                    return;
                }
            }
        }
    };

    bool same(const ArrayDrawer& rArray, const DrawRunList& rList) {
        if (static_cast< s32 >(rArray.mPackets.size()) != rList.count()) {
            return false;
        }
        s32 slot = rList.first();
        for (const DrawRunList::Packet& rPacket : rArray.mPackets) {
            if (slot < 0) {
                return false;
            }
            const DrawRunList::Packet& rOther = rList.at(slot);
            if (rOther.mEntry != rPacket.mEntry || rOther.mLightCtrl != rPacket.mLightCtrl || rOther.mLightLoader != rPacket.mLightLoader) {
                return false;
            }
            slot = rList.next(slot);
        }
        return slot < 0;
    }

    // Actors of `shapes` packets each (a drawer that holds several of a model's materials), some with no light
    // control; lights change, actors clip in and out, and a few share an entry (two materials on one mesh).
    void testSameOrder(u32 shapes, u32 seed) {
        sSeed = seed;
        const u32 actors = 300;
        std::vector< Ctrl > ctrls(actors);
        std::vector< char > entries(actors * shapes);
        std::vector< bool > shown(actors, false);
        for (Ctrl& rCtrl : ctrls) {
            rCtrl.mLight = static_cast< int >(random(5));
            rCtrl.mBlending = random(10) == 0;
        }

        ArrayDrawer array;
        DrawRunList list;
        list.reserve(8);  // small: the list grows by itself
        bool ok = true;
        for (u32 step = 0; step < 20000 && ok; step++) {
            const u32 actor = random(actors);
            const void* const pCtrl = actor % 17 == 0 ? nullptr : &ctrls[actor];
            const u32 op = random(10);
            if (op < 4) {
                if (!shown[actor]) {
                    for (u32 s = 0; s < shapes; s++) {
                        // Every 23rd actor's last shape uses its first shape's entry.
                        const u32 e = actor % 23 == 0 && s == shapes - 1 ? 0 : s;
                        array.add(&entries[actor * shapes + e], pCtrl);
                        list.add(&entries[actor * shapes + e], pCtrl, sameLight, blending);
                    }
                    shown[actor] = true;
                }
            } else if (op < 8) {
                if (shown[actor]) {
                    for (u32 s = 0; s < shapes; s++) {
                        const u32 e = actor % 23 == 0 && s == shapes - 1 ? 0 : s;
                        array.remove(&entries[actor * shapes + e]);
                        list.remove(&entries[actor * shapes + e]);
                    }
                    shown[actor] = false;
                } else {
                    // An entry that is not there.
                    array.remove(&entries[actor * shapes]);
                    list.remove(&entries[actor * shapes]);
                }
            } else {
                // The actor moves to another light or starts or ends a blend, and re-sorts (ActorLightCtrl).
                ctrls[actor].mLight = static_cast< int >(random(5));
                ctrls[actor].mBlending = random(4) == 0;
                array.resetLightSort(pCtrl);
                list.resetLightSort(pCtrl, sameLight, blending);
            }
            ok = same(array, list);
        }
        char what[96];
        snprintf(what, sizeof(what), "the list keeps the array's packets and order (%u shapes)", shapes);
        expect(ok, what);
    }

    // Seconds a clip-out and clip-in of one packet in a drawer of `count` packets takes, over `rounds`.
    template < class Drawer, class Add, class Remove >
    double churn(Drawer& rDrawer, std::vector< Ctrl >& rCtrls, std::vector< char >& rEntries, u32 count, u32 rounds, Add add, Remove remove) {
        for (u32 i = 0; i < count; i++) {
            add(rDrawer, &rEntries[i], &rCtrls[i]);
        }
        const auto start = std::chrono::steady_clock::now();
        for (u32 r = 0; r < rounds; r++) {
            const u32 i = (r * 7919u) % count;
            remove(rDrawer, &rEntries[i]);
            add(rDrawer, &rEntries[i], &rCtrls[i]);
        }
        return std::chrono::duration< double >(std::chrono::steady_clock::now() - start).count() / rounds;
    }

    void testCost() {
        const u32 many = 20000;
        std::vector< Ctrl > ctrls(many);
        std::vector< char > entries(many);
        for (u32 i = 0; i < many; i++) {
            ctrls[i].mLight = static_cast< int >(i % 4);
            ctrls[i].mBlending = false;
        }
        auto listAdd = [](DrawRunList& rList, const void* pEntry, const void* pCtrl) { rList.add(pEntry, pCtrl, sameLight, blending); };
        auto listRemove = [](DrawRunList& rList, const void* pEntry) { rList.remove(pEntry); };
        auto arrayAdd = [](ArrayDrawer& rArray, const void* pEntry, const void* pCtrl) { rArray.add(pEntry, pCtrl); };
        auto arrayRemove = [](ArrayDrawer& rArray, const void* pEntry) { rArray.remove(pEntry); };

        DrawRunList fewList, manyList;
        ArrayDrawer manyArray;
        fewList.reserve(500);
        manyList.reserve(static_cast< s32 >(many));
        const double fewTime = churn(fewList, ctrls, entries, 500, 200000, listAdd, listRemove);
        const double manyTime = churn(manyList, ctrls, entries, many, 200000, listAdd, listRemove);
        const double arrayTime = churn(manyArray, ctrls, entries, many, 2000, arrayAdd, arrayRemove);
        printf("a clip-out and clip-in: list %.0f ns among 500 packets, %.0f ns among %u; array %.0f ns among %u\n", fewTime * 1e9,
               manyTime * 1e9, many, arrayTime * 1e9, many);
        expect(manyTime < fewTime * 10.0 + 200e-9, "the list's cost does not grow with the packets");
        expect(manyTime * 10.0 < arrayTime, "the list is far cheaper than the array among many packets");
    }
}  // namespace

int main() {
    NebulaArena::create(nullptr);
    for (u32 shapes = 1; shapes <= 3; shapes++) {
        testSameOrder(shapes, 77u + shapes);
    }
    testCost();
    if (sFailures != 0) {
        printf("%d failed\n", sFailures);
        return 1;
    }
    printf("draw run list: ok\n");
    return 0;
}
