#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class GameTexture;
class OceanBowlBloomDrawer;
class OceanBowlPoint;
class WaterInfo;

class OceanBowl : public LiveActor {
public:
    static const s32 cPointNum = 25;  // grid points per side

    OceanBowl(const char* pName);

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
    void resetWrappedPoint(OceanBowlPoint* pPoint, const Vec3f& rPos);
    SiteMaterial loadMaterial() const;
    SiteMaterial loadMaterialBloom() const;

    OceanBowlPoint* getPoint(int x, int y) const {
        return mPoints[x][y];
    }

    void setPoint(int x, int y, OceanBowlPoint* pPoint) {
        mPoints[x][y] = pPoint;
    }

    // Strip x, column y in the mVtxTex arrays: u, v of point (x + 1, y), then u, v of point (x, y).
    static s32 getStripTexCoordIndex(s32 x, s32 y) {
        return (x * cPointNum + y) * 4;
    }

    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    f32 mRadius;
    OceanBowlPoint* mPoints[cPointNum][cPointNum];  // [front][side]
    f32 mWaveX;
    f32 mWaveZ;
    f32 mTexU0;
    f32 mTexV0;
    f32 mTexU1;
    f32 mTexV1;
    f32 mTexU2;
    f32 mTexV2;
    f32 mTexStep;  // texture 3 (shore mask) step per point
    f32* mVtxTex0Array;
    f32* mVtxTex1Array;
    f32* mVtxTex2Array;
    f32* mVtxTex3Array;
    GameTexture* mWaterTex;
    GameTexture* mWaterIndirectTex;
    GameTexture* mMaskTex;
    OceanBowlBloomDrawer* mBloomDrawer;
    bool mIsClipped;  // the camera is in other water or the player is far
};
