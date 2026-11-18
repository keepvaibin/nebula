#pragma once

// The packets of one shape drawer in draw order, the plain module's (P-038). Packets of one light are kept together,
// each run starting at a packet that loads the light; the order is the one the hook module's array keeps, operation
// for operation (DrawBufferShapeDrawer::add, remove and resetLightSort there). Here the packets are linked through
// their slots and the runs are linked beside them, and two tables find a packet by its entry and by its light control,
// so an add walks the runs only (one a light), and a remove and a light re-sort walk nothing. Every packet carries an
// order key, so "the first packet in draw order" with an entry or a control is the least key among a few.
// A draw walks ordered(): the packets in draw order one after another, copied from the links at the first read after
// a change. Packets change on few frames (an actor clipping in or out, a light change) and are drawn on every one, so
// the frame's walk reads one array in order instead of following links through scattered slots.
class DrawRunList {
public:
    struct Packet {
        const void* mEntry;        // the mesh entry
        const void* mLightCtrl;    // its actor's light control, or null
        const void* mLightLoader;  // mLightCtrl when the packet starts a light run, else null
    };

    // Whether a packet of light control pCtrl joins the run that pLoader's packet starts (ActorLightCtrl::isSameLight).
    typedef bool (*SameLight)(const void* pCtrl, const void* pLoader);
    // Whether pCtrl is blending to a new light: a packet of it starts a run of its own at the end.
    typedef bool (*Blending)(const void* pCtrl);

    DrawRunList();
    ~DrawRunList();
    DrawRunList(const DrawRunList&) = delete;
    DrawRunList& operator=(const DrawRunList&) = delete;

    // Room for maxPackets packets (the list also grows by itself when full).
    void reserve(s32 maxPackets);
    // At the end of the first run whose light pCtrl shares, else at the end of the list starting a run (a blending
    // control's packet always), or at the end with no run of its own for a null control.
    void add(const void* pEntry, const void* pCtrl, SameLight sameLight, Blending blending);
    // The first packet with pEntry; when it started a run, the packet after it starts the run in its place.
    void remove(const void* pEntry);
    // The first packet with pCtrl, removed (by its entry) and added again.
    void resetLightSort(const void* pCtrl, SameLight sameLight, Blending blending);

    // The first packet's slot in draw order, -1 for none; the next one's after `slot`.
    s32 first() const {
        return mHead;
    }
    s32 next(s32 slot) const {
        return mLinks[slot].mNext;
    }
    const Packet& at(s32 slot) const {
        return mPackets[slot];
    }
    s32 count() const {
        return mCount;
    }
    // The count() packets in draw order, one after another; valid until the next add, remove or re-sort.
    const Packet* ordered() const;

private:
    struct Link {
        s32 mPrev;
        s32 mNext;  // also the free list's
        s32 mPrevRun;
        s32 mNextRun;
        s32 mNextSameEntry;  // the chains a table's slot starts: the packets of one entry, of one control
        s32 mNextSameCtrl;
        u64 mOrder;  // rises along the list
    };

    // A pointer to the slot that starts its chain; open addressing, half full at most.
    struct Table {
        const void** mKeys;
        s32* mSlots;
        u32 mMask;

        s32 find(const void* pKey) const;
        void set(const void* pKey, s32 slot);
        void erase(const void* pKey);
    };

    s32 take();
    void link(s32 slot, s32 before);
    void relabel();
    void pushChain(Table* pTable, s32 Link::* pNextOf, const void* pKey, s32 slot);
    void dropChain(Table* pTable, s32 Link::* pNextOf, const void* pKey, s32 slot);
    s32 firstOfChain(const Table& rTable, s32 Link::* pNextOf, const void* pKey) const;
    void removeSlot(s32 slot);
    void rebuildTables(u32 size);

    Packet* mPackets;
    Link* mLinks;
    s32 mMax;
    s32 mCount;
    s32 mHead;
    s32 mTail;
    s32 mFirstRun;
    s32 mLastRun;
    s32 mFree;
    Table mByEntry;
    Table mByCtrl;
    // ordered()'s copy, and whether a change came after it.
    mutable Packet* mOrdered;
    mutable s32 mOrderedRoom;
    mutable bool mOrderedStale;
};
