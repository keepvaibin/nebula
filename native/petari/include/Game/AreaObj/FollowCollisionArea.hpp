#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class AreaPolygon;

class FollowCollisionArea : public NameObj {
public:
    FollowCollisionArea(const Vec3f& rSize, MtxPtr pMatrix, f32 offset);

    virtual void movement();

    bool hitCheck(const Vec3f& rPos, f32 radius, Vec3f* pPoint, Vec3f* pNormal);

    u32 mOutsideAxisCount;
    f32 mInsideDepth;
    Vec3f mCenter;
    s32 mPushType;
    s32 mTeresaType;
    bool mIsTeresaPass;
    s32 mHitRadius;
    AreaPolygon* mPolygon;
    MtxPtr mFollowMtx;
    Vec3f mSize;
    f32 mOffset;
};
