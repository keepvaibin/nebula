#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class Material;
class Mesh;

class PlantLeaf : public LiveActor {
public:
    PlantLeaf(f32, const Vec3f&, const Vec3f&, f32);

    virtual void init(const TableIter&) override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }

    void updateGrowUp(const Vec3f&, const Vec3f&, f32, f32);
    bool updateSpring(const Vec3f&, f32, f32);
    bool updateSpring(f32);

    f32 mSpringCoord;
    f32 mSpringSpeed;
    f32 mLeafCoord;
    f32 mLeafSize;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Pos3f mBaseMtx;
};

class PlantLeafDrawInit : public LiveActor {
public:
    PlantLeafDrawInit(const char*);

    virtual void init(const TableIter&) override;

    Material* mMaterial;
    Mesh* mShape;
};
