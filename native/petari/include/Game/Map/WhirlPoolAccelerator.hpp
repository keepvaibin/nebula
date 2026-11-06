#pragma once

#include "Game/Render/TypedDraw.hpp"

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;

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
    bool _39;
};

class WhirlPoolAccelerator : public LiveActor {
public:
    WhirlPoolAccelerator(const char* pName);

    virtual ~WhirlPoolAccelerator();
    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;

    bool calcInfo(const Vec3f& rPosition, Vec3f* pAcceleration) const;
    void initPoints();
    void drawPlane(TypedDraw& rDraw, f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 u1) const;
    SiteMaterial loadMaterial() const;

    f32 mRadius;
    f32 mHeight;
    Vec3f mAxis;
    s32 mPointCount;
    WhirlPoolPoint** mPoints;
    f32 mRotation;
    f32 mTexOffsetU0;
    f32 mTexOffsetV0;
    JUTTexture* mTexture;
    Vec3f mClippingCenter;
};
