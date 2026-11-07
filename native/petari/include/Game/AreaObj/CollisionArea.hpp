#pragma once

#include "Game/AreaObj/AreaObj.hpp"
#include "Game/MapObj/DynamicCollisionObj.hpp"

class AreaPolygon : public DynamicCollisionObj {
public:
    AreaPolygon();

    virtual ~AreaPolygon();

    virtual void init(const TableIter& rIter);

    void setMtx(MtxPtr pMatrix, const Vec3f& rSize, f32 offset);
    void setSurfaceAndSync(s32 surface);
    void setSurface(s32 surface);
    void invalidate();
    void validate();

    u8 _120[4];
    AreaForm* mForm;
    MtxPtr _128;
    Vec3f _12C;
    f32 _138;
};

class CollisionArea : public AreaObj {
public:
    CollisionArea(int formType, const char* pName);

    virtual ~CollisionArea();

    virtual void init(const TableIter& rIter);
    virtual void movement();

    bool hitCheck(const Vec3f& rPos, f32 radius, Vec3f* pPoint, Vec3f* pNormal);

    u32 _3C;
    f32 _40;
    Vec3f _44;
    s32 _50;
    s32 _54;
    bool _58;
    u8 _59[3];
    s32 _5C;
    s32 _60;
    AreaPolygon* mPolygon;
    bool mIsValid;
    u8 _69[3];
};