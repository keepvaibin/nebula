#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/Util/NativeMesh.hpp"

class ElectricRailPoint : public LiveActor {
public:
    ElectricRailPoint(const char* name = "\223\144\214\202\203\214\201\133\203\213\223\137" /* 電撃レール点 */);

    virtual ~ElectricRailPoint();
    virtual void init(const JMapInfoIter&);
    virtual void attackSensor(HitSensor*, HitSensor*);

    bool mHasShadow;
    bool mIsActive;
};

class ElectricRailSeparator {
public:
    ElectricRailSeparator();

    void setup(const TVec3f&, const TVec3f&, const TVec3f&);

    TVec3f _0;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    TVec3f _30;
};

class ElectricRailShadowDrawer : public ShadowVolumeDrawer {
public:
    ElectricRailShadowDrawer(const LiveActor*, ElectricRailSeparator*, s32);

    virtual void loadModelDrawMtx() const;
    virtual void drawShape() const;
    virtual bool isDraw() const;

    void drawShadowVolumeShape(NativeMesh&, ElectricRailSeparator*, s32) const;

    const LiveActor* mHost;
    // The shadow volume as the renderer draws it.
    NativeMesh mMesh;
};

class ElectricRail : public LiveActor {
public:
    ElectricRail(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void draw() const;
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void disappear();
    void initMapToolInfo(const JMapInfoIter&);
    void initSensor();
    void initPoints();
    void initSeparators();
    void initMesh();
    void drawRail(f32) const;
    void drawPlane(NativeMesh&, f32, f32, f32, f32) const;
    void drawPlaneGX(f32, f32, f32, f32) const;
    void initShadow(const JMapInfoIter&);
    void updateHitSensorPos();
    void calcGravity(TVec3f*, const TVec3f&) const;

    void exeDisappear();
    void exeWait();
    void exeDisappeared();

    ElectricRailPoint* mPoints;
    s32 mPointCount;
    TVec3f* _94;
    ElectricRailSeparator* mSeparators;
    s32 mSeparatorCount;
    // The rail as the renderer draws it while it waits.
    NativeMesh mMesh;
    s32 mRailHeight;  // Obj_arg0
    f32 mEaseIn;
    ElectricRailShadowDrawer* mShadowDrawer;
    bool mIsCalcGravity;  // Obj_arg4
};
