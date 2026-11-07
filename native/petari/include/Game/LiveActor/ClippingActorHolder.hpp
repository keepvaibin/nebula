#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ClippingActorInfo;
class ClippingActorInfoList;
class LiveActor;
class LodCtrl;
class ViewGroupCtrl;

class ClippingActorHolder : public NameObj {
public:
    /// @brief Creates a new `ClippingActorHolder`.
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
    ClippingActorInfo* find(const LiveActor*) const;
    const ClippingActorInfo* findGround(const ClippingActorInfo*) const;

    s32 mActorNum;
    ClippingActorInfoList* _10;
    ClippingActorInfoList* _14;
    ClippingActorInfoList* _18;
    ClippingActorInfoList* _1C;
    ViewGroupCtrl* mViewGroupCtrl;
    ClippingActorInfo** mPlanets;
    s32 mPlanetNum;
};
