#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry.hpp>

class AreaPolygon;

class FollowCollisionArea : public NameObj {
public:
    FollowCollisionArea(const TVec3f& rSize, MtxPtr pMatrix, f32 offset);

    virtual ~FollowCollisionArea();
    virtual void movement();

    bool hitCheck(const TVec3f& rPos, f32 radius, TVec3f* pPoint, TVec3f* pNormal);

    u32 _C;
    f32 _10;
    TVec3f _14;
    s32 _20;
    s32 _24;
    bool _28;
    s32 _2C;
    AreaPolygon* _30;
    MtxPtr _34;
    TVec3f _38;
    f32 _44;
};
