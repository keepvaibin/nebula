#include "Game/System/DrawRunList.hpp"

#if defined(NEBULA_PLAIN_PATHS)
#include "Game/System/NebulaArena.hpp"

#include <stdint.h>

namespace {
    // The gap between neighbours' order keys after a relabel: 2^32 inserts between two packets before the next one.
    const u64 cOrderGap = 1ull << 32;

    u32 hashOf(const void* pKey) {
        return static_cast< u32 >((static_cast< u64 >(reinterpret_cast< uintptr_t >(pKey)) * 0x9E3779B97F4A7C15ull) >> 32);
    }

    // Room in the arena that holds `pOwner` (the root's when none does).
    template < class T >
    T* makeRoom(const void* pOwner, u32 count) {
        NebulaArena* const pHome = NebulaArena::findFromRoot(const_cast< void* >(pOwner));
        return new (pHome != nullptr ? pHome : NebulaArena::root(), 0) T[count];
    }
}  // namespace

s32 DrawRunList::Table::find(const void* pKey) const {
    if (mKeys == nullptr) {
        return -1;
    }

    for (u32 at = hashOf(pKey) & mMask;; at = (at + 1) & mMask) {
        if (mKeys[at] == pKey) {
            return mSlots[at];
        }
        if (mKeys[at] == nullptr) {
            return -1;
        }
    }
}

void DrawRunList::Table::set(const void* pKey, s32 slot) {
    u32 at = hashOf(pKey) & mMask;
    while (mKeys[at] != nullptr && mKeys[at] != pKey) {
        at = (at + 1) & mMask;
    }
    mKeys[at] = pKey;
    mSlots[at] = slot;
}

// Linear probing's erase: the keys after the hole that would not be found past it move back into it.
void DrawRunList::Table::erase(const void* pKey) {
    u32 at = hashOf(pKey) & mMask;
    while (mKeys[at] != pKey) {
        if (mKeys[at] == nullptr) {
            return;
        }
        at = (at + 1) & mMask;
    }

    u32 hole = at;
    for (u32 next = (hole + 1) & mMask; mKeys[next] != nullptr; next = (next + 1) & mMask) {
        const u32 home = hashOf(mKeys[next]) & mMask;
        // Moves back when its home is not in (hole, next], going round.
        const bool stays = hole <= next ? (hole < home && home <= next) : (hole < home || home <= next);
        if (!stays) {
            mKeys[hole] = mKeys[next];
            mSlots[hole] = mSlots[next];
            hole = next;
        }
    }
    mKeys[hole] = nullptr;
}

DrawRunList::DrawRunList()
    : mPackets(nullptr), mLinks(nullptr), mMax(0), mCount(0), mHead(-1), mTail(-1), mFirstRun(-1), mLastRun(-1), mFree(-1), mByEntry(),
      mByCtrl(), mOrdered(nullptr), mOrderedRoom(0), mOrderedStale(false) {
}

DrawRunList::~DrawRunList() {
    delete[] mPackets;
    delete[] mLinks;
    delete[] mByEntry.mKeys;
    delete[] mByEntry.mSlots;
    delete[] mByCtrl.mKeys;
    delete[] mByCtrl.mSlots;
    delete[] mOrdered;
}

const DrawRunList::Packet* DrawRunList::ordered() const {
    if (mOrderedStale) {
        if (mOrderedRoom < mCount) {
            delete[] mOrdered;
            mOrdered = makeRoom< Packet >(this, static_cast< u32 >(mMax));
            mOrderedRoom = mMax;
        }
        s32 at = 0;
        for (s32 slot = mHead; slot >= 0; slot = mLinks[slot].mNext) {
            mOrdered[at++] = mPackets[slot];
        }
        mOrderedStale = false;
    }
    return mOrdered;
}

void DrawRunList::reserve(s32 maxPackets) {
    if (maxPackets <= mMax) {
        return;
    }

    Packet* const pPackets = makeRoom< Packet >(this, static_cast< u32 >(maxPackets));
    Link* const pLinks = makeRoom< Link >(this, static_cast< u32 >(maxPackets));
    for (s32 i = 0; i < mMax; i++) {
        pPackets[i] = mPackets[i];
        pLinks[i] = mLinks[i];
    }
    // The new slots join the free list, the lowest first.
    for (s32 i = maxPackets - 1; i >= mMax; i--) {
        pLinks[i].mNext = mFree;
        mFree = i;
    }
    delete[] mPackets;
    delete[] mLinks;
    mPackets = pPackets;
    mLinks = pLinks;
    mMax = maxPackets;

    u32 size = 16;
    while (size < static_cast< u32 >(maxPackets) * 2u) {
        size *= 2;
    }
    rebuildTables(size);
}

void DrawRunList::rebuildTables(u32 size) {
    Table* const tables[] = {&mByEntry, &mByCtrl};
    for (Table* pTable : tables) {
        delete[] pTable->mKeys;
        delete[] pTable->mSlots;
        pTable->mKeys = makeRoom< const void* >(this, size);
        pTable->mSlots = makeRoom< s32 >(this, size);
        pTable->mMask = size - 1;
        for (u32 i = 0; i < size; i++) {
            pTable->mKeys[i] = nullptr;
        }
    }

    for (s32 slot = mHead; slot >= 0; slot = mLinks[slot].mNext) {
        pushChain(&mByEntry, &Link::mNextSameEntry, mPackets[slot].mEntry, slot);
        if (mPackets[slot].mLightCtrl != nullptr) {
            pushChain(&mByCtrl, &Link::mNextSameCtrl, mPackets[slot].mLightCtrl, slot);
        }
    }
}

s32 DrawRunList::take() {
    if (mFree < 0) {
        reserve(mMax > 0 ? mMax * 2 : 16);
    }

    const s32 slot = mFree;
    mFree = mLinks[slot].mNext;
    return slot;
}

void DrawRunList::relabel() {
    u64 order = 0;
    for (s32 slot = mHead; slot >= 0; slot = mLinks[slot].mNext) {
        order += cOrderGap;
        mLinks[slot].mOrder = order;
    }
}

// Into the list before `before`, or at its end for -1.
void DrawRunList::link(s32 slot, s32 before) {
    Link& rLink = mLinks[slot];
    if (before < 0) {
        if (mTail >= 0 && mLinks[mTail].mOrder > ~0ull - cOrderGap) {
            relabel();
        }
        rLink.mOrder = mTail >= 0 ? mLinks[mTail].mOrder + cOrderGap : cOrderGap;
        rLink.mPrev = mTail;
        rLink.mNext = -1;
        if (mTail >= 0) {
            mLinks[mTail].mNext = slot;
        } else {
            mHead = slot;
        }
        mTail = slot;
        return;
    }

    const s32 after = mLinks[before].mPrev;
    if (mLinks[before].mOrder - (after >= 0 ? mLinks[after].mOrder : 0) < 2) {
        relabel();
    }
    const u64 low = after >= 0 ? mLinks[after].mOrder : 0;
    rLink.mOrder = low + (mLinks[before].mOrder - low) / 2;
    rLink.mPrev = after;
    rLink.mNext = before;
    mLinks[before].mPrev = slot;
    if (after >= 0) {
        mLinks[after].mNext = slot;
    } else {
        mHead = slot;
    }
}

void DrawRunList::pushChain(Table* pTable, s32 Link::* pNextOf, const void* pKey, s32 slot) {
    mLinks[slot].*pNextOf = pTable->find(pKey);
    pTable->set(pKey, slot);
}

void DrawRunList::dropChain(Table* pTable, s32 Link::* pNextOf, const void* pKey, s32 slot) {
    const s32 head = pTable->find(pKey);
    if (head == slot) {
        const s32 next = mLinks[slot].*pNextOf;
        if (next >= 0) {
            pTable->set(pKey, next);
        } else {
            pTable->erase(pKey);
        }
        return;
    }

    for (s32 at = head; at >= 0; at = mLinks[at].*pNextOf) {
        if (mLinks[at].*pNextOf == slot) {
            mLinks[at].*pNextOf = mLinks[slot].*pNextOf;
            return;
        }
    }
}

s32 DrawRunList::firstOfChain(const Table& rTable, s32 Link::* pNextOf, const void* pKey) const {
    s32 first = -1;
    for (s32 at = rTable.find(pKey); at >= 0; at = mLinks[at].*pNextOf) {
        if (first < 0 || mLinks[at].mOrder < mLinks[first].mOrder) {
            first = at;
        }
    }
    return first;
}

void DrawRunList::add(const void* pEntry, const void* pCtrl, SameLight sameLight, Blending blending) {
    mOrderedStale = true;
    // DrawBufferShapeDrawer::findLightSortIndex, over the runs instead of every packet.
    s32 before = -1;
    bool loads = false;
    if (pCtrl != nullptr) {
        if (blending(pCtrl)) {
            loads = true;
        } else {
            bool found = false;
            for (s32 run = mFirstRun; run >= 0; run = mLinks[run].mNextRun) {
                if (found) {
                    before = run;
                    break;
                }
                if (sameLight(pCtrl, mPackets[run].mLightLoader)) {
                    found = true;
                }
            }
            loads = !found;
        }
    }

    const s32 slot = take();
    Packet& rPacket = mPackets[slot];
    rPacket.mEntry = pEntry;
    rPacket.mLightCtrl = pCtrl;
    rPacket.mLightLoader = loads ? pCtrl : nullptr;
    link(slot, before);

    // A packet that starts a run is always the last packet, so its run is the last run.
    Link& rLink = mLinks[slot];
    rLink.mPrevRun = -1;
    rLink.mNextRun = -1;
    if (loads) {
        rLink.mPrevRun = mLastRun;
        if (mLastRun >= 0) {
            mLinks[mLastRun].mNextRun = slot;
        } else {
            mFirstRun = slot;
        }
        mLastRun = slot;
    }

    pushChain(&mByEntry, &Link::mNextSameEntry, pEntry, slot);
    if (pCtrl != nullptr) {
        pushChain(&mByCtrl, &Link::mNextSameCtrl, pCtrl, slot);
    }
    mCount++;
}

void DrawRunList::removeSlot(s32 slot) {
    Link& rLink = mLinks[slot];
    Packet& rPacket = mPackets[slot];

    if (rPacket.mLightLoader != nullptr) {
        // The next packet starts the run in its place when it has a control and does not start one already.
        const s32 next = rLink.mNext;
        const bool hands = next >= 0 && mPackets[next].mLightLoader == nullptr && mPackets[next].mLightCtrl != nullptr;
        const s32 instead = hands ? next : -1;
        if (hands) {
            mPackets[next].mLightLoader = mPackets[next].mLightCtrl;
            mLinks[next].mPrevRun = rLink.mPrevRun;
            mLinks[next].mNextRun = rLink.mNextRun;
        }

        const s32 prevRun = rLink.mPrevRun;
        const s32 nextRun = rLink.mNextRun;
        if (prevRun >= 0) {
            mLinks[prevRun].mNextRun = hands ? instead : nextRun;
        } else {
            mFirstRun = hands ? instead : nextRun;
        }
        if (nextRun >= 0) {
            mLinks[nextRun].mPrevRun = hands ? instead : prevRun;
        } else {
            mLastRun = hands ? instead : prevRun;
        }
    }

    if (rLink.mPrev >= 0) {
        mLinks[rLink.mPrev].mNext = rLink.mNext;
    } else {
        mHead = rLink.mNext;
    }
    if (rLink.mNext >= 0) {
        mLinks[rLink.mNext].mPrev = rLink.mPrev;
    } else {
        mTail = rLink.mPrev;
    }

    dropChain(&mByEntry, &Link::mNextSameEntry, rPacket.mEntry, slot);
    if (rPacket.mLightCtrl != nullptr) {
        dropChain(&mByCtrl, &Link::mNextSameCtrl, rPacket.mLightCtrl, slot);
    }

    rLink.mNext = mFree;
    mFree = slot;
    mCount--;
}

void DrawRunList::remove(const void* pEntry) {
    const s32 slot = firstOfChain(mByEntry, &Link::mNextSameEntry, pEntry);
    if (slot >= 0) {
        removeSlot(slot);
        mOrderedStale = true;
    }
}

void DrawRunList::resetLightSort(const void* pCtrl, SameLight sameLight, Blending blending) {
    s32 slot = -1;
    if (pCtrl != nullptr) {
        slot = firstOfChain(mByCtrl, &Link::mNextSameCtrl, pCtrl);
    } else {
        for (slot = mHead; slot >= 0 && mPackets[slot].mLightCtrl != nullptr; slot = mLinks[slot].mNext) {
        }
    }
    if (slot < 0) {
        return;
    }

    const void* const pEntry = mPackets[slot].mEntry;
    remove(pEntry);
    add(pEntry, pCtrl, sameLight, blending);
}
#endif
