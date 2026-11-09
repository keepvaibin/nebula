#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include <nebula_geometry.hpp>

class ElectricRailMovingPoint : public LiveActor {
public:
    ElectricRailMovingPoint(const char* pName = "移動電撃レール点");

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    bool mHasShadow;
    bool mIsElectric;  // pushes instead once the rail vanishes
};

class ElectricRailMoving : public ElectricRailDrawn {
public:
    ElectricRailMoving(const char*);

    virtual void init(const TableIter&) override;
    virtual void drawWith(MaterialDraw& rMaterial) const override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void disappear();
    bool isTouchRail(const HitSensor*, Vec3f*, Vec3f*) const;
    void initMapToolInfo(const TableIter&);
    void initSensor();
    void initRail(const TableIter&);
    void initPoints();
    void initMesh();
    void drawRail(TypedDraw& rDraw, f32 width) const;
    void drawPlane(f32 side0, f32 up0, f32 side1, f32 up1);
    void drawPlaneQuad(TypedDraw& rDraw, f32 side0, f32 up0, f32 side1, f32 up1) const;
    // Two strip corners of one rail position at rVtx.
    void setVertexAttribute(int pointIndex, int level, f32 side0, f32 up0, f32 side1, f32 up1, VertexPT*& rVtx) const;
    void updateHitSensorPos();
    bool updatePointPos();
    void updatePointPosAndModel();
    bool isValidCoord(f32 coord) const;
    f32 getRepeatedCoord(f32 coord) const;
    void calcPointPos(Vec3f* pOutPos, f32 coord) const;
    void move();
    void exeDisappear();
    void exeDisappeared();
    void exeWait();

    s32 mSegmentNum;
    f32 mMovementSpeed;
    f32 mSegmentLength;
    s32 mStackHeight;  // rails stacked against gravity (a sensor each)
    Pos3f mTexMtx;
    u8 mAlpha;
    Vec3f* mSensorOffsets;
    ElectricRailMovingPoint* mMovingPoints;
    s32 mPointNum;
    // The rail as the renderer draws it while it waits.
    SiteMesh< VertexPT > mMesh;
    s32 mRailPosNum;
    Vec3f* mRailPositions;
    f32 mCoord;
    f32 mRailWidth;
};
