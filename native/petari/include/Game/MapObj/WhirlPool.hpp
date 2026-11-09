#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class GameTexture;

class WhirlPoolPoint;

class WhirlPool : public LiveActor {
public:
    WhirlPool(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void draw() const override;

    void initPoints();
    void drawPlane(TypedDraw& rDraw, f32 side0, f32 normal0, f32 side1, f32 normal1, f32 scrollAU0, f32 scrollAU1, f32 scrollBU0,
                   f32 scrollBU1) const;
    SiteMaterial loadMaterial() const;

    f32 mRadius;
    f32 mHeight;
    Vec3f mAxis;
    s32 mPointCount;
    WhirlPoolPoint** mPoints;
    f32 mRotation;
    f32 mTexOffsetU0;
    f32 mTexOffsetV0;
    f32 mTexOffsetU1;
    f32 mTexOffsetV1;
    f32 mTexOffsetU2;
    f32 mTexOffsetV2;
    GameTexture* mTexture;
    GameTexture* mIndirectTexture;
    Vec3f mClippingCenter;
};
