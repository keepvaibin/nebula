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
    OceanRing(const char*);

    virtual ~OceanRing();
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void movement();
    virtual void draw() const;
    virtual void startClipped();
    virtual void endClipped();

    bool isInWater(const Vec3f&) const;
    bool calcWaterInfo(const Vec3f&, const Vec3f&, WaterInfo*) const;
    f32 calcNearestPos(const Vec3f&, Vec3f*, Vec3f*, Vec3f*) const;
    f32 calcWaveHeight(const Vec3f&, f32, Vec3f*) const;
    void calcStreamVec(const Vec3f&, f32, Vec3f*) const;
    void initPoints();
    void updatePoints();
    void updatePointsInLine(s32, s32, s32, s32);
    f32 calcCurrentWidthRate(f32) const;
    f32 calcCurrentFlowSpeedRate(f32) const;
    void calcClippingBox();
    WaterPoint* getPoint(int) const;
    WaterPoint* getPoint(int, int) const;

    s32 mWaterPointNum;
    s32 mSegCount;
    s32 mStride;
    WaterPoint** mWaterPoints;
    f32 mWidthMax;
    s32 mObjArg1;
    f32 mWaveTheta0;
    f32 mWaveTheta1;
    f32 mWaveHeight1;
    f32 mWaveHeight2;
    bool mIsClipped;
    f32 mNearPosToPlayer;
    Vec3f mNearestPos;
    Vec3f mNearestDir;
    OceanRingDrawer* mRingDrawer;
    Box3f mBox;
    Box3f mClippingBox;
    Vec3f mClippingCenter;
    Vec3f mNearestToWatchCam;
    AudSoundObject* mSoundObj1;
    Vec3f mNerarestToCam;
    AudSoundObject* mSoundObj2;
    OceanRingPipe* mOceanRingPipe;
    OceanRingBloomDrawer* mBloomDrawer;
};
