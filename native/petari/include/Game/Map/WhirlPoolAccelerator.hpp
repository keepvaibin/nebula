#pragma once

#include "Game/Render/TypedDraw.hpp"

#include "Game/LiveActor/LiveActor.hpp"

class GameTexture;

class WhirlPoolPoint {
public:
    WhirlPoolPoint(const Vec3f& rPosition, const Vec3f& rDirection, const Vec3f& rNormal, f32 radius, f32 texCoord, u8 alpha);

    Vec3f mPosition;
    Vec3f mSide;
    Vec3f mNormal;
    Vec3f mDirection;
    f32 mRadius;
    f32 mTexCoord;
    u8 mAlpha;
};

// The six-sided outline both whirlpool surfaces sweep, as (side, normal) weights; the first corner repeats to close it.
const f32 cWhirlPoolOutline[7][2] = {
    {0.5f, 0.70710677f}, {-0.5f, 0.70710677f}, {-1.0f, 0.0f}, {-0.5f, -0.70710677f},
    {0.5f, -0.70710677f}, {1.0f, 0.0f},        {0.5f, 0.70710677f},
};

class WhirlPoolAccelerator : public LiveActor {
public:
    WhirlPoolAccelerator(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void draw() const;

    bool calcInfo(const Vec3f& rPosition, Vec3f* pAcceleration) const;
    void initPoints();
    void drawPlane(TypedDraw& rDraw, f32 side0, f32 normal0, f32 side1, f32 normal1, f32 u0, f32 u1) const;
    SiteMaterial loadMaterial() const;

    f32 mRadius;
    f32 mHeight;
    Vec3f mAxis;
    s32 mPointCount;
    WhirlPoolPoint** mPoints;
    f32 mRotation;
    f32 mTexOffsetU0;
    f32 mTexOffsetV0;
    GameTexture* mTexture;
    Vec3f mClippingCenter;
};
