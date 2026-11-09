#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/Map/WaterPoint.hpp"

class AudSoundObject;
class OceanRingDrawer;
class OceanRingPipe;
class OceanRingBloomDrawer;

class OceanRing : public LiveActor {
public:
    OceanRing(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void movement() override;
    virtual void draw() const override;
    virtual void startClipped() override;
    virtual void endClipped() override;

    bool isInWater(const Vec3f& rPos) const;
    bool calcWaterInfo(const Vec3f& rPos, const Vec3f& rGravity, WaterInfo* pInfo) const;
    f32 calcNearestPos(const Vec3f& rPos, Vec3f* pNearPos, Vec3f* pDir, Vec3f* pCrossPos) const;
    f32 calcWaveHeight(const Vec3f& rPos, f32 coord, Vec3f* pNormal) const;
    void calcStreamVec(const Vec3f& rPos, f32 coord, Vec3f* pStreamVec) const;
    void initPoints();
    void updatePoints();
    void updatePointsInLine(s32 first, s32 end, s32 fadeInSegs, s32 fadeOutSegs);
    f32 calcCurrentWidthRate(f32 coord) const;
    f32 calcCurrentFlowSpeedRate(f32 coord) const;
    void calcClippingBox();
    WaterPoint* getPoint(int index) const;
    WaterPoint* getPoint(int point, int seg) const;

    s32 mWaterPointNum;
    s32 mSegCount;
    s32 mStride;  // points per cross-section
    WaterPoint** mWaterPoints;
    f32 mWidthMax;
    s32 mUpdateSegsAhead;  // Obj_arg1
    f32 mWaveTheta0;
    f32 mWaveTheta1;
    f32 mWaveHeight1;
    f32 mWaveHeight2;
    bool mIsClipped;  // the player is too far for the waves to update
    f32 mPlayerCoord;
    Vec3f mNearestPos;  // nearest the player
    Vec3f mNearestDir;
    OceanRingDrawer* mRingDrawer;
    Box3f mUpdateBox;
    Box3f mClippingBox;
    Vec3f mClippingCenter;
    Vec3f mNearestToWatchCam;
    AudSoundObject* mWatchSoundObj;
    Vec3f mNearestToCam;
    AudSoundObject* mCamSoundObj;
    OceanRingPipe* mOceanRingPipe;
    OceanRingBloomDrawer* mBloomDrawer;
};
