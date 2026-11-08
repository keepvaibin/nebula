#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ClippingActorInfo;
class ClippingActorInfoList;
class LiveActor;
class LodCtrl;
class ViewGroupCtrl;

// Every registered actor's info sits in exactly one of the four lists; the table finds an actor's info.
class ClippingActorHolder : public NameObj {
public:
    ClippingActorHolder();

    virtual void movement() override;

    void registerActor(LiveActor*);
    void initSystemInfo(LiveActor*, const TableIter&);
    void initViewGroupTable();
    void entryLodCtrl(LodCtrl*, const TableIter&);
    void validateClipping(LiveActor*);
    void invalidateClipping(LiveActor*);
    void addToClippingTarget(LiveActor*);
    void removeFromClippingTarget(LiveActor*);
    ClippingActorInfo* startGroupClipping(LiveActor*, const TableIter&);
    void setTypeToSphere(LiveActor*, f32, const Vec3f*);
    void setFarClipLevel(LiveActor*, s32);
    // Null: not registered.
    ClippingActorInfo* find(const LiveActor*) const;
    const ClippingActorInfo* findGround(const ClippingActorInfo*) const;

private:
    static void moveTo(ClippingActorInfo*, ClippingActorInfoList*);
    void insert(ClippingActorInfo*);

public:
    s32 mActorNum;
    ClippingActorInfoList* mTargets;       // judged each frame
    ClippingActorInfoList* mInvalid;       // clipping turned off
    ClippingActorInfoList* mNonTargets;    // dead or taken out
    ClippingActorInfoList* mGroupTargets;  // judged by their clipping group
    ViewGroupCtrl* mViewGroupCtrl;
    ClippingActorInfo** mPlanets;
    s32 mPlanetNum;

private:
    // Open addressing, a power of two at most half full.
    ClippingActorInfo** mTable;
    u32 mTableMask;
};
