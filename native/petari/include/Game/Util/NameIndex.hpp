#pragma once

#include <nebula_types.h>
#include <cstring>
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"

namespace MR {
    u32 getHashCode(const char*);
    u32 getHashCodeLower(const char*);

    // A name's key in a NameIndex: its hash and whether two names are the same. Exact: the whole name.
    struct NameKeyExact {
        static u32 hash(const char* pName) {
            return getHashCode(pName);
        }
        static bool equal(const char* pA, const char* pB) {
            return isEqualString(pA, pB);
        }
    };

    // ASCII case folded, as isEqualStringCase compares (names it calls equal hash alike).
    struct NameKeyCase {
        static u32 hash(const char* pName) {
            return getHashCodeLower(pName);
        }
        static bool equal(const char* pA, const char* pB) {
            return isEqualStringCase(pA, pB);
        }
    };

    // Names to u32 values: open addressing with linear probing, a power-of-two table at most half full. The first name
    // added stays (a later equal name is refused). Names (never null) are held by pointer and must outlive the index.
    template < class Key >
    class NameIndexOf {
    public:
        static constexpr u32 cNoValue = 0xFFFFFFFFu;

        NameIndexOf() : mSlots(nullptr), mMask(0), mCount(0) {
        }

        explicit NameIndexOf(u32 capacity) : mSlots(nullptr), mMask(0), mCount(0) {
            reset(capacity);
        }

        ~NameIndexOf() {
            delete[] mSlots;
        }

        NameIndexOf(const NameIndexOf&) = delete;
        NameIndexOf& operator=(const NameIndexOf&) = delete;

        // Empty, with room for `capacity` names; the table is made again only when it is too small.
        void reset(u32 capacity) {
            u32 size = 8;
            while (size < 2 * capacity) {
                size *= 2;
            }
            if (mSlots == nullptr || size > mMask + 1) {
                delete[] mSlots;
                mSlots = allocSlots(size);
                mMask = size - 1;
            }
            for (u32 i = 0; i <= mMask; i++) {
                mSlots[i].mName = nullptr;
            }
            mCount = 0;
        }

        // False when an equal name is held already (it keeps its value).
        bool add(const char* pName, u32 value) {
            return add(pName, Key::hash(pName), value);
        }

        bool add(const char* pName, u32 hash, u32 value) {
            // A null name is the end of a probe run, so it can not be held.
            if (pName == nullptr) {
                return false;
            }
            if (2 * (mCount + 1) > mMask + 1) {
                grow();
            }
            u32 i = hash & mMask;
            for (; mSlots[i].mName != nullptr; i = (i + 1) & mMask) {
                if (mSlots[i].mHash == hash && Key::equal(mSlots[i].mName, pName)) {
                    return false;
                }
            }
            mSlots[i].mName = pName;
            mSlots[i].mHash = hash;
            mSlots[i].mValue = value;
            mCount++;
            return true;
        }

        u32 find(const char* pName) const {
            return find(pName, Key::hash(pName));
        }

        u32 find(const char* pName, u32 hash) const {
            const s32 slot = slotOf(pName, hash);
            return slot < 0 ? cNoValue : mSlots[slot].mValue;
        }

        // The first name added with this hash: for data that names its entries by hash alone.
        u32 findHash(u32 hash) const {
            if (mCount == 0) {
                return cNoValue;
            }
            for (u32 i = hash & mMask; mSlots[i].mName != nullptr; i = (i + 1) & mMask) {
                if (mSlots[i].mHash == hash) {
                    return mSlots[i].mValue;
                }
            }
            return cNoValue;
        }

        // The entry named pOld takes the name pNew, keeping its value; false when pOld is not held or pNew is.
        bool rename(const char* pOld, const char* pNew) {
            const s32 slot = slotOf(pOld, Key::hash(pOld));
            if (slot < 0 || find(pNew) != cNoValue) {
                return false;
            }
            const u32 value = mSlots[slot].mValue;
            remove(slot);
            return add(pNew, value);
        }

        u32 count() const {
            return mCount;
        }

        // A name holding this value, or null: a walk of every slot, for a rare reverse lookup.
        const char* nameOf(u32 value) const {
            for (u32 i = 0; mSlots != nullptr && i <= mMask; i++) {
                if (mSlots[i].mName != nullptr && mSlots[i].mValue == value) {
                    return mSlots[i].mName;
                }
            }
            return nullptr;
        }

    private:
        struct Slot {
            const char* mName;
            u32 mHash;
            u32 mValue;
        };

        // In the index's own arena (the current one for an index outside them all): an index that outlives a scene,
        // as a holder's does, keeps its table when the scene's arena is reset.
        Slot* allocSlots(u32 size) {
            return new (NebulaArena::findFromRoot(this), alignof(Slot)) Slot[size];
        }

        s32 slotOf(const char* pName, u32 hash) const {
            if (mCount == 0) {
                return -1;
            }
            for (u32 i = hash & mMask; mSlots[i].mName != nullptr; i = (i + 1) & mMask) {
                if (mSlots[i].mHash == hash && Key::equal(mSlots[i].mName, pName)) {
                    return static_cast< s32 >(i);
                }
            }
            return -1;
        }

        // Backward-shift deletion: later entries of the run move up so every probe still reaches them.
        void remove(u32 hole) {
            u32 i = hole;
            while (true) {
                i = (i + 1) & mMask;
                if (mSlots[i].mName == nullptr) {
                    break;
                }
                const u32 home = mSlots[i].mHash & mMask;
                if (((i - home) & mMask) >= ((i - hole) & mMask)) {
                    mSlots[hole] = mSlots[i];
                    hole = i;
                }
            }
            mSlots[hole].mName = nullptr;
            mCount--;
        }

        // Twice the size, entries in the order they were added along each probe run.
        void grow() {
            Slot* pOld = mSlots;
            const u32 oldSize = mSlots == nullptr ? 0 : mMask + 1;
            const u32 size = oldSize == 0 ? 8 : 2 * oldSize;
            mSlots = allocSlots(size);
            mMask = size - 1;
            for (u32 i = 0; i < size; i++) {
                mSlots[i].mName = nullptr;
            }
            mCount = 0;
            if (pOld != nullptr) {
                // From the start of a run (a slot after an empty one), so equal hashes keep their order.
                u32 start = 0;
                while (start < oldSize && pOld[(start + oldSize - 1) % oldSize].mName != nullptr) {
                    start++;
                }
                for (u32 k = 0; k < oldSize; k++) {
                    const Slot& rSlot = pOld[(start + k) % oldSize];
                    if (rSlot.mName != nullptr) {
                        add(rSlot.mName, rSlot.mHash, rSlot.mValue);
                    }
                }
            }
            delete[] pOld;
        }

        Slot* mSlots;
        u32 mMask;
        u32 mCount;
    };

    typedef NameIndexOf< NameKeyExact > NameIndex;
};  // namespace MR
