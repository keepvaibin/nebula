#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class J3DMaterial;
class J3DShape;
class J3DShapeDraw;

class PlantLeaf : public LiveActor {
public:
    PlantLeaf(f32, const Vec3f&, const Vec3f&, f32);

    virtual void init(const JMapInfoIter&);
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mBaseMtx;
    }

    void updateGrowUp(const Vec3f&, const Vec3f&, f32, f32);
    bool updateSpring(const Vec3f&, f32, f32);
    bool updateSpring(f32);

    MtxPtr getPosMtx() const {
        return (MtxPtr)&mPosMtx;
    }

    f32 mSpringCoord;
    f32 mSpringSpeed;
    f32 mLeafCoord;
    f32 mLeafSize;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Pos3f mBaseMtx;
    Pos3f mPosMtx;
};

class PlantLeafDrawInit : public LiveActor {
public:
    PlantLeafDrawInit(const char*);

    virtual void init(const JMapInfoIter&);

    void initDraw() const;

    J3DMaterial* mMaterial;
    J3DShape* mShape;
    J3DShapeDraw* mShapeDraw;
};
