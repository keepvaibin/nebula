#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;
class OceanBowlBloomDrawer;
class OceanBowlPoint;
class WaterInfo;

class OceanBowl : public LiveActor {
public:
    OceanBowl(const char*);

    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void draw() const;

    bool isInWater(const Vec3f& rPos) const;
    bool calcWaterInfo(const Vec3f& rPos, const Vec3f& rGravity, WaterInfo* pInfo) const;
    void initPoints();
    void updatePoints();
    void moveToLeft();
    void moveToRight();
    void moveToUpper();
    void moveToLower();
    SiteMaterial loadMaterial() const;
    SiteMaterial loadMaterialBloom() const;
    // void drawDebug();

    // getPoint__9OceanBowlCFii from the debug maps, always inlined
    OceanBowlPoint* getPoint(int x, int y) const {
        return mPoints[x][y];
    }

    // setPoint__9OceanBowlFiiP14OceanBowlPoint from the debug maps, always inlined
    void setPoint(int x, int y, OceanBowlPoint* pPoint) {
        mPoints[x][y] = pPoint;
    }

    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    f32 mRadius;
    OceanBowlPoint* mPoints[25][25];
    f32 mWaveX;
    f32 mWaveZ;
    f32 mTexU0;
    f32 mTexV0;
    f32 mTexU1;
    f32 mTexV1;
    f32 mTexU2;
    f32 mTexV2;
    f32 mTexStep;
    // Texture coordinate arrays.
    f32* mVtxTex0Array;
    f32* mVtxTex1Array;
    f32* mVtxTex2Array;
    f32* mVtxTex3Array;
    JUTTexture* mWaterTex;
    JUTTexture* mWaterIndirectTex;
    JUTTexture* mMaskTex;
    OceanBowlBloomDrawer* mBloomDrawer;
    bool mIsClipped;
};
