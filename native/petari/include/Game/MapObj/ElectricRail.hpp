#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"

class ElectricRailPoint : public LiveActor {
public:
    ElectricRailPoint(const char* name = "電撃レール点");

    virtual ~ElectricRailPoint();
    virtual void init(const TableIter&);
    virtual void attackSensor(HitSensor*, HitSensor*);

    bool mHasShadow;
    bool mIsActive;
};

class ElectricRailSeparator {
public:
    ElectricRailSeparator();

    void setup(const Vec3f&, const Vec3f&, const Vec3f&);

    Vec3f mPos;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mOffsetDir;
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

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void drawWith(MaterialDraw& rMaterial) const;
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void disappear();
    void initMapToolInfo(const TableIter&);
    void initSensor();
    void initPoints();
    void initSeparators();
    void initMesh();
    void drawRail(TypedDraw& rDraw, f32) const;
    void drawPlane(f32, f32, f32, f32);
    void drawPlaneGX(TypedDraw&, f32, f32, f32, f32) const;
    void initShadow(const TableIter&);
    void updateHitSensorPos();
    void calcGravity(Vec3f*, const Vec3f&) const;

    void exeDisappear();
    void exeWait();
    void exeDisappeared();

    ElectricRailPoint* mPoints;
    s32 mPointCount;
    Vec3f* mSensorPositions;
    ElectricRailSeparator* mSeparators;
    s32 mSeparatorCount;
    // The rail as the renderer draws it while it waits.
    SiteMesh< VertexPT > mMesh;
    s32 mRailHeight;  // Obj_arg0
    f32 mEaseIn;
    ElectricRailShadowDrawer* mShadowDrawer;
    bool mIsCalcGravity;  // Obj_arg4
};
