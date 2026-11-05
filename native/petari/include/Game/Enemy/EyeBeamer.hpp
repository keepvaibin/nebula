#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailMover;
class ModelObj;
class VolumeModelDrawer;

class EyeBeamer : public LiveActor {
public:
    EyeBeamer(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void initStartNerve(const JMapInfoIter& rIter);
    void initModel();
    void initRailMoveFunction(const JMapInfoIter& rIter);
    void updatePoseAndTrans();
    void updateWaterSurfaceMtx();
    void requestStartPatrol();
    bool tryGotoPatrol();
    bool tryPatrol();
    void exeDemoStartWait();
    void exeDemoWait();
    void exeDemoTurn();
    void exeDemoGotoPatrol();
    void exeWait();
    void exeTurn();
    void exeGotoPatrol();
    void exePatrol();
    bool isInBeamRange(const Vec3f& rVec) const;
    bool isOnBeam() const;

    MapPartsRailMover* mRailMover;
    ModelObj* mBeamBloom;
    ModelObj* mBeamMdl;
    VolumeModelDrawer* mBeamVolumeDrawer;
    Pos3f _9C;
    Quat4f _CC;
    Quat4f _DC;
    Vec3f _EC;
    Vec3f _F8;
    Vec3f _104;
    Pos3f mWaterSurfaceMtx;
    Vec3f _140;
    u32 _14C;
    u32 _150;
    u32 _154;
    u32 _158;
    f32 _15C;
    f32 _160;
    bool mIsInMercatorCube;
};
