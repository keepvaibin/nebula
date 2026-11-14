#include <stdint.h>
#pragma once

class NameObj;

#include "Game/System/NebulaList.hpp"
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

    class Index : public NebulaListLink< Index > {
    public:
        Index();

        const TableIter& getInfoIter() const {
            return mInfoIter;
        }

        TableIter mInfoIter;
        NameObj* mMade = nullptr;  // Nebula: the object its creator made (placement_of, placed_at)
    };

    class SameIdSet : public Identifier {
    public:
        SameIdSet();

        bool operator<(const SameIdSet& rOther) const {
            if (mPriority == rOther.mPriority) {
                return rOther.mList.getNum() < mList.getNum();
            }

            return mPriority < rOther.mPriority;
        }

        s32 mPriority;
        NebulaList< Index > mList;
    };

    PlacementInfoOrdered(int);

    void sort();
    void initPlacement();
    bool initPlacementStep(uint64_t deadline);
    void insert(const Identifier&, const TableIter&);
    SameIdSet* find(const Identifier&) const;
    SameIdSet* createSameIdSet(const Identifier&);
    Index* createIndex(const TableIter&);
    void attach(const Table*, PlacementInfoOrdered*);
    // The entries attach() sends to its after-scenario list.
    static s32 countAfterScenario(const Table*);
    // Nebula (docs/modding/language/classes.md, "placement"): the object made from the row (pTable, entry), or null.
    NameObj* findMade(const Table* pTable, s32 entry) const;
    // The row an object was made from. False: none of this list's.
    bool findPlaced(const void* pObject, TableIter* pOut) const;

    Index* mIndexArray;
    u32 mIndexCount;
    SameIdSet* mSetArray;
    SameIdSet** mOrderedSetArray;  // the first mSetNum are set
    int mCount;
    s32 mSetNum = 0;
    s32 mPlacementSet = 0;
    NebulaListLink< Index >* mPlacementLink = nullptr;
    bool mPlacementStarted = false;
    NameObj* mInitializing = nullptr;
};
