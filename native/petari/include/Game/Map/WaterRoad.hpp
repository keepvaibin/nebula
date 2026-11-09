#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <nebula_geometry.hpp>

class GameTexture;
class WaterRoadModelInfo;
class AudSoundObject;
class ActorCameraInfo;

class WaterRoad : public LiveActor {
public:
    WaterRoad(const char*);

    virtual void init(const TableIter& rIter);
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

    WaterRoadModelInfo* mModelInfoHigh;
    WaterRoadModelInfo* mModelInfoLow;
    f32 mRadius;
    u8 mAlpha;
    Vec3f mBaseUp;
    Pos3f mBaseMtx;
    LiveActor* mRider;
    Pos3f mRiderMtx;
    Vec2f mTexUV0;
    Vec2f mTexUV1;
    Vec2f mTexUV2;
    GameTexture* mTexture;
    GameTexture* mIndirect;
    GameTexture* mEnvMap;
    Box3f mBoundingBox;
    Vec3f mClippingCenter;
    Vec3f mCamRailNearestPos;
    AudSoundObject* mSoundObj;
    ActorCameraInfo* mCameraInfo;
};

class WaterRoadModelInfo {
public:
    WaterRoadModelInfo(WaterRoad*, bool);

    void initPoints(WaterRoad*);
    void initMesh();
    void drawGD() const;
    void drawDirect(const WaterRoad*) const;
    s32 calcDemoDrawPointNum(const WaterRoad*) const;
    SiteMaterial loadMaterialHigh(const WaterRoad*) const;
    SiteMaterial loadMaterialLow() const;
    template < class T >
    void writeMesh(SiteMesh< T >& rMesh);

    s32 calcPointIndex(int i, int j) const {
        return j + i * mNumLoopPoints;
    }

    bool mIsLow;
    s32 mNumPoints;
    s32 mNumLinePoints;
    s32 mNumLoopPoints;
    Vec3f* mPoints;
    Vec3s* mNormals;
    f32* mRailCoords;
    // The tube as the renderer draws it (its points and normals by index, texture coordinates its own).
    SiteMesh< VertexPNT3 > mMeshHigh;
    SiteMesh< VertexPNT2 > mMeshLow;
    // The road whose textures it draws with.
    const WaterRoad* mRoad;
};
