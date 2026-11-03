#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/NativeMesh.hpp"
#include <JSystem/JGeometry.hpp>

class JUTTexture;
class WaterRoadModelInfo;
class AudSoundObject;
class ActorCameraInfo;

class WaterRoad : public LiveActor {
public:
    WaterRoad(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void movement();
    virtual void draw() const;
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeWaitInvalid();
    void exeRideStart();
    void exeRideWait();
    void exeRideSpin();
    void exeDemoStart();
    void exeDemoGrowUp();
    void exeDemoWaitEnd();
    void exeDisappear();

    bool updateRide();
    void updateDemo(f32);

    s32 _8C;  // arg0, unused?
    WaterRoadModelInfo* mModelInfoHigh;
    WaterRoadModelInfo* mModelInfoLow;
    f32 mRadius;
    u8 mAlpha;
    TVec3f mBaseUp;
    TPos3f mBaseMtx;
    LiveActor* mRider;
    TPos3f mRiderMtx;
    TVec2f mTexUV0;
    TVec2f mTexUV1;
    TVec2f mTexUV2;
    JUTTexture* mTexture;
    JUTTexture* mIndirect;
    JUTTexture* mEnvMap;
    TBox3f mBoundingBox;
    TVec3f mClippingCenter;
    TVec3f mCamRailNearestPos;
    AudSoundObject* mSoundObj;
    ActorCameraInfo* mCameraInfo;
};

class WaterRoadModelInfo {
public:
    WaterRoadModelInfo(WaterRoad*, bool);

    void initPoints(WaterRoad*);
    void initMesh();
    void sendGD();
    void drawGD() const;
    void drawDirect(const WaterRoad*) const;
    s32 calcDemoDrawPointNum(const WaterRoad*) const;
    void loadMaterialHigh(const WaterRoad*) const;
    void loadMaterialLow() const;

    s32 calcPointIndex(int i, int j) const {
        return j + i * mNumLoopPoints;
    }

    bool mIsLow;
    s32 mNumPoints;
    s32 mNumLinePoints;
    s32 mNumLoopPoints;
    TVec3f* mPoints;
    TVec3s* mNormals;
    f32* mRailCoords;
    // The tube as the renderer draws it (its points and normals by index, texture coordinates its own).
    NativeMesh mMesh;
    // The road whose textures it draws with.
    const WaterRoad* mRoad;
};
