#pragma once

#include "Game/AreaObj/AreaObj.hpp"
#include "Game/MapObj/DynamicCollisionObj.hpp"

class AreaPolygon : public DynamicCollisionObj {
public:
    AreaPolygon();

    virtual void init(const TableIter& rIter) override;

    void setMtx(MtxPtr pMatrix, const Vec3f& rSize, f32 offset);
    void setSurfaceAndSync(s32 surface);
    void setSurface(s32 surface);
    void invalidate();
    void validate();

    AreaForm* mForm;
    MtxPtr mFollowMtx;
    Vec3f mSize;
    f32 mOffset;
};

class CollisionArea : public AreaObj {
public:
    CollisionArea(int formType, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;

    bool hitCheck(const Vec3f& rPos, f32 radius, Vec3f* pPoint, Vec3f* pNormal);

    u32 mOutsideAxisCount;
    f32 mInsideDepth;
    Vec3f mCenter;
    s32 mPushType;
    s32 mTeresaType;
    bool mIsTeresaPass;
    s32 mHitRadius;
    s32 mSurfaceMask;
    AreaPolygon* mPolygon;
    bool mIsValid;
};