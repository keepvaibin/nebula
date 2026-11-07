#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include <nebula_geometry.hpp>

class ElectricRailMovingPoint : public LiveActor {
public:
    ElectricRailMovingPoint(const char* pName = "移動電撃レール点");

    virtual ~ElectricRailMovingPoint();
    virtual void init(const TableIter&);
    virtual void attackSensor(HitSensor*, HitSensor*);

    u8 _8C;
    u8 _8D;
};

class ElectricRailMoving : public ElectricRailDrawn {
public:
    ElectricRailMoving(const char*);

    virtual ~ElectricRailMoving();
    virtual void init(const TableIter&);
    virtual void drawWith(MaterialDraw& rMaterial) const;
    virtual void attackSensor(HitSensor*, HitSensor*);

    void disappear();
    bool isTouchRail(const HitSensor*, Vec3f*, Vec3f*) const;
    void initMapToolInfo(const TableIter&);
    void initSensor();
    void initRail(const TableIter&);
    void initPoints();
    void initMesh();
    void drawRail(TypedDraw& rDraw, f32) const;
    void drawPlane(f32, f32, f32, f32);
    void drawPlaneGX(TypedDraw&, f32, f32, f32, f32) const;
    // A segment's two corners into `pMesh`, or drawn now when it is null.
    void setVertexAttribute(int, int, f32, f32, f32, f32, VertexPT*& rVtx) const;
    void updateHitSensorPos();
    bool updatePointPos();
    void updatePointPosAndModel();
    bool isValidCoord(f32) const;
    f32 getRepeatedCoord(f32) const;
    void calcPointPos(Vec3f*, f32) const;
    void move();
    void exeDisappear();
    void exeDisappeared();
    void exeWait();

    s32 mSegmentNum;
    f32 mMovementSpeed;
    f32 mSegmentLength;
    s32 mStackHeight;
    Pos3f _9C;
    u8 _CC;
    u8 _CD;
    u8 _CE;
    u8 _CF;
    Vec3f* mSensorOffsets;
    ElectricRailMovingPoint* mMovingPoints;
    s32 _D8;
    // The rail as the renderer draws it while it waits.
    SiteMesh< VertexPT > mMesh;
    s32 _E4;
    Vec3f* _E8;
    f32 _EC;
    f32 _F0;
};
