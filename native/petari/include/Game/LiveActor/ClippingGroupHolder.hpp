#pragma once

#include "Game/NameObj/NameObj.hpp"

class ClippingActorInfo;
class PlacementId;

// Actors placed with one clipping group: they run and stop together.
class ClippingInfoGroup : public NameObj {
public:
    ClippingInfoGroup(const char* pGroupName, s32 capacity);

    void setGroupNo(const TableIter&);
    void registerInfo(ClippingActorInfo*);
    // Every live member with clipping on is out of range.
    bool isClippedNowAll() const;
    void startClippedAll();
    void endClippedAll();
    void updateDrawnClippedAll();

    s32 mCapacity;
    s32 mCount;
    ClippingActorInfo** mInfos;
    PlacementId* mGroupId;
    bool mIsClipped;
};

class ClippingGroupHolder : public NameObj {
public:
    ClippingGroupHolder();

    virtual void movement();

    void createAndAdd(ClippingActorInfo*, const TableIter&, s32 capacity);
    ClippingInfoGroup* createGroup(ClippingActorInfo*, const TableIter&, s32 capacity);

    ClippingInfoGroup* findGroup(const TableIter&);
    ClippingInfoGroup* getGroup(s32 index) const {
        return mInfoGroups[index];
    }

    s32 mNumGroups;
    ClippingInfoGroup** mInfoGroups;
    s32 mMaxGroups;
};
