#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;

class WhirlPoolPoint {
public:
    WhirlPoolPoint(const TVec3f& rPosition, const TVec3f& rDirection, const TVec3f& rNormal, f32 radius, f32 texCoord, u8 alpha);

    TVec3f mPosition;
    TVec3f mSide;
    TVec3f mNormal;
    TVec3f mDirection;
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

    bool calcInfo(const TVec3f& rPosition, TVec3f* pAcceleration) const;
    void initPoints();
    void drawPlane(f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 u1) const;
    void loadMaterial() const;

    f32 mRadius;
    f32 mHeight;
    TVec3f mAxis;
    s32 mPointCount;
    WhirlPoolPoint** mPoints;
    f32 mRotation;
    f32 mTexOffsetU0;
    f32 mTexOffsetV0;
    JUTTexture* mTexture;
    TVec3f mClippingCenter;
};
