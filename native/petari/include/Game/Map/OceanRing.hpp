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
    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void movement();
    virtual void draw() const;
    virtual void startClipped();
    virtual void endClipped();

    bool isInWater(const TVec3f&) const;
    bool calcWaterInfo(const TVec3f&, const TVec3f&, WaterInfo*) const;
    f32 calcNearestPos(const TVec3f&, TVec3f*, TVec3f*, TVec3f*) const;
    f32 calcWaveHeight(const TVec3f&, f32, TVec3f*) const;
    void calcStreamVec(const TVec3f&, f32, TVec3f*) const;
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
    TVec3f mNearestPos;
    TVec3f mNearestDir;
    OceanRingDrawer* mRingDrawer;
    TBox3f mBox;
    TBox3f mClippingBox;
    TVec3f _108;
    TVec3f mNearestToWatchCam;
    AudSoundObject* mSoundObj1;
    TVec3f mNerarestToCam;
    AudSoundObject* mSoundObj2;
    OceanRingPipe* mOceanRingPipe;
    OceanRingBloomDrawer* mBloomDrawer;
};
