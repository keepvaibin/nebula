#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailMover;
class ModelObj;
class VolumeModelDrawer;

class EyeBeamer : public LiveActor {
public:
    EyeBeamer(const char*);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void initStartNerve(const TableIter& rIter);
    void initModel();
    void initRailMoveFunction(const TableIter& rIter);
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
    Pos3f mBeamMtx;
    Quat4f mInitQuat;
    Quat4f mPoseQuat;
    Vec3f mBasePos;
    Vec3f mGotoStartPos;
    Vec3f mRailStartPos;
    Pos3f mWaterSurfaceMtx;
    Vec3f mClipCenter;
    f32 mBeamLength;
    f32 mWaterSurfaceHeight;
    bool mIsInMercatorCube;
};
