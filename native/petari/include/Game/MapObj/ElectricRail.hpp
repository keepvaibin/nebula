#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"

class ElectricRailPoint : public LiveActor {
public:
    ElectricRailPoint(const char* name = "電撃レール点");

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    bool mHasShadow;
    bool mIsActive;
};

class ElectricRailSeparator {
public:
    ElectricRailSeparator();

    void setup(const Vec3f& rPos, const Vec3f& rRailDir, const Vec3f& rOffsetDir);

    Vec3f mPos;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mOffsetDir;
};

class ElectricRailShadowDrawer : public ShadowVolumeDrawer {
public:
    ElectricRailShadowDrawer(const LiveActor*, ElectricRailSeparator*, s32);

    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;
    virtual void drawShape(TypedDraw& rDraw) const override;
    virtual bool isDraw() const override;

    void writeShadowVolumeShape(ElectricRailSeparator*, s32);

    const LiveActor* mHost;
    // The shadow volume as the renderer draws it.
    SiteMesh< VertexP > mMesh;
};

class ElectricRail : public ElectricRailDrawn {
public:
    ElectricRail(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void drawWith(MaterialDraw& rMaterial) const override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void disappear();
    void deactivate();
    void initMapToolInfo(const TableIter&);
    void initSensor();
    void initPoints();
    void initSeparators();
    void initMesh();
    void drawRail(TypedDraw& rDraw, f32 radius) const;
    void drawPlane(f32 side0, f32 up0, f32 side1, f32 up1);
    void drawPlaneQuad(TypedDraw& rDraw, f32 side0, f32 up0, f32 side1, f32 up1) const;
    void initShadow(const TableIter&);
    void updateHitSensorPos();
    void calcGravity(Vec3f* pOut, const Vec3f& rPos) const;

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
