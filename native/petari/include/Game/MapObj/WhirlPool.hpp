#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;

class WhirlPoolPoint;

class WhirlPool : public LiveActor {
public:
    WhirlPool(const char* pName);

    virtual ~WhirlPool();
    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;

    void initPoints();
    void drawPlane(f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 u1, f32 u2, f32 u3) const;
    void loadMaterial() const;

    f32 mRadius;
    f32 mHeight;
    TVec3f mAxis;
    s32 mPointCount;
    WhirlPoolPoint** mPoints;
    f32 mRotation;
    f32 mTexOffsetU0;
    f32 mTexOffsetV0;
    f32 mTexOffsetU1;
    f32 mTexOffsetV1;
    f32 mTexOffsetU2;
    f32 mTexOffsetV2;
    JUTTexture* mTexture;
    JUTTexture* mIndirectTexture;
    TVec3f mClippingCenter;
};
