#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;
class OceanBowlBloomDrawer;
class OceanBowlPoint;
class WaterInfo;

class OceanBowl : public LiveActor {
public:
    OceanBowl(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;

    bool isInWater(const TVec3f& rPos) const;
    bool calcWaterInfo(const TVec3f& rPos, const TVec3f& rGravity, WaterInfo* pInfo) const;
    void initPoints();
    void updatePoints();
    void moveToLeft();
    void moveToRight();
    void moveToUpper();
    void moveToLower();
    void loadMaterial() const;
    void loadMaterialBloom() const;
    // void drawDebug();

    // getPoint__9OceanBowlCFii from the debug maps, always inlined
    OceanBowlPoint* getPoint(int x, int y) const {
        return mPoints[x][y];
    }

    // setPoint__9OceanBowlFiiP14OceanBowlPoint from the debug maps, always inlined
    void setPoint(int x, int y, OceanBowlPoint* pPoint) {
        mPoints[x][y] = pPoint;
    }

    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
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
    f32 _A98;
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
