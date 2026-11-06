#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"

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

    void setup(const Vec3f&, const Vec3f&, const Vec3f&);

    Vec3f _0;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f _30;
};

class ElectricRailShadowDrawer : public ShadowVolumeDrawer {
public:
    ElectricRailShadowDrawer(const LiveActor*, ElectricRailSeparator*, s32);

    virtual void loadModelDrawMtx(TypedDraw& rDraw) const;
    virtual void drawShape(TypedDraw& rDraw) const;
    virtual bool isDraw() const;

    void writeShadowVolumeShape(ElectricRailSeparator*, s32);

    const LiveActor* mHost;
    // The shadow volume as the renderer draws it.
    SiteMesh< VertexP > mMesh;
};

class ElectricRail : public ElectricRailDrawn {
public:
    ElectricRail(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void drawWith(MaterialDraw& rMaterial) const;
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void disappear();
    void initMapToolInfo(const JMapInfoIter&);
    void initSensor();
    void initPoints();
    void initSeparators();
    void initMesh();
    void drawRail(TypedDraw& rDraw, f32) const;
    void drawPlane(f32, f32, f32, f32);
    void drawPlaneGX(TypedDraw&, f32, f32, f32, f32) const;
    void initShadow(const JMapInfoIter&);
    void updateHitSensorPos();
    void calcGravity(Vec3f*, const Vec3f&) const;

    void exeDisappear();
    void exeWait();
    void exeDisappeared();

    ElectricRailPoint* mPoints;
    s32 mPointCount;
    Vec3f* _94;
    ElectricRailSeparator* mSeparators;
    s32 mSeparatorCount;
    // The rail as the renderer draws it while it waits.
    SiteMesh< VertexPT > mMesh;
    s32 mRailHeight;  // Obj_arg0
    f32 mEaseIn;
    ElectricRailShadowDrawer* mShadowDrawer;
    bool mIsCalcGravity;  // Obj_arg4
};
