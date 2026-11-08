#include <stdint.h>
#pragma once

class NameObj;

#include "Game/Util/BothDirList.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/StringUtil.hpp"

typedef NameObj* (*CreationFuncPtr)(const char*);

class PlacementInfoOrdered {
public:
    class Identifier {
    public:
        Identifier() : mName(), mShapeId(-1) {
        }

        Identifier(const char* pName, s32 shapeId) : mName(pName), mShapeId(shapeId) {
        }

        bool operator==(const Identifier& rOther) const {
            return MR::isEqualString(mName, rOther.mName) && mShapeId == rOther.mShapeId;
        }

        const char* mName;
        s32 mShapeId;
    };

    class Index : public MR::BothDirLink< Index > {
    public:
        Index();

        ~Index();

        const TableIter& getInfoIter() const {
            return mInfoIter;
        }

        TableIter mInfoIter;
    };

    class SameIdSet : public Identifier {
    public:
        SameIdSet();

        ~SameIdSet();

        bool operator<(const SameIdSet& rOther) const {
            if (mPriority == rOther.mPriority) {
                return rOther.mList.mCount < mList.mCount;
            }

            return mPriority < rOther.mPriority;
        }

        s32 mPriority;
        MR::BothDirList< Index > mList;
    };

    PlacementInfoOrdered(int);

    void sort();
    void initPlacement();
    bool initPlacementStep(uint64_t deadline);
    void insert(const Identifier&, const TableIter&);
    SameIdSet* getSameIdSet(int index) const {
        return mOrderedSetArray[index];
    }

    s32 getUsedArrayNum() const;
    SameIdSet* find(const Identifier&) const;
    SameIdSet* createSameIdSet(const Identifier&);
    Index* createIndex(const TableIter&);
    void attach(const Table*, PlacementInfoOrdered*);
    // The entries attach() sends to its after-scenario list.
    static s32 countAfterScenario(const Table*);

    Index* mIndexArray;
    u32 mIndexCount;
    SameIdSet* mSetArray;
    SameIdSet** mOrderedSetArray;
    int mCount;
    s32 mPlacementSet = 0;
    MR::BothDirPtrLink* mPlacementLink = nullptr;
    bool mPlacementStarted = false;
    NameObj* mInitializing = nullptr;
};
