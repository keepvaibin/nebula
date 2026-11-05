#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class AreaPolygon;

class FollowCollisionArea : public NameObj {
public:
    FollowCollisionArea(const Vec3f& rSize, MtxPtr pMatrix, f32 offset);

    virtual ~FollowCollisionArea();
    virtual void movement();

    bool hitCheck(const Vec3f& rPos, f32 radius, Vec3f* pPoint, Vec3f* pNormal);

    u32 _C;
    f32 _10;
    Vec3f _14;
    s32 _20;
    s32 _24;
    bool _28;
    s32 _2C;
    AreaPolygon* _30;
    MtxPtr _34;
    Vec3f _38;
    f32 _44;
};
