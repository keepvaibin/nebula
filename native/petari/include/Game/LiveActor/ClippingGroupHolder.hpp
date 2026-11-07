#pragma once

#include "Game/NameObj/NameObj.hpp"

class ClippingActorInfo;
class PlacementId;

class ClippingInfoGroup : public NameObj {
public:
    ClippingInfoGroup(const char*, int);

    virtual ~ClippingInfoGroup();

    void setGroupNo(const TableIter&);
    void registerInfo(ClippingActorInfo*);
    bool isClippedNowAll() const;
    void startClippedAll();
    void endClippedAll();

    int _C;
    s32 _10;
    ClippingActorInfo** _14;
    PlacementId* _18;
    u8 _1C;
};

class ClippingGroupHolder : public NameObj {
public:
    ClippingGroupHolder();

    virtual ~ClippingGroupHolder();
    virtual void movement();

    void createAndAdd(ClippingActorInfo*, const TableIter&, int);
    ClippingInfoGroup* createGroup(ClippingActorInfo*, const TableIter&, int);

    ClippingInfoGroup* findGroup(const TableIter&);
    ClippingInfoGroup* getGroup(int index) const {
        return mInfoGroups[index];
    }

    s32 mNumGroups;
    ClippingInfoGroup** mInfoGroups;
    s32 mMaxGroups = 0x40;
};
