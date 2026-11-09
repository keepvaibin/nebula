#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailMover;
class ModelObj;
class VolumeModelDrawer;

class EyeBeamer : public LiveActor {
public:
    EyeBeamer(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void initStartNerve(const TableIter& rIter);
    void initModel();
    void initRailMoveFunction(const TableIter& rIter);
    void updatePoseAndTrans();
    void updateWaterSurfaceMtx();
    void requestStartPatrol();
    bool tryGotoPatrol();
    bool tryPatrol();
    void setPoseTurned(f32 angle);
    void exeDemoStartWait();
    void exeDemoWait();
    void exeDemoTurn();
    void exeDemoGotoPatrol();
    void exeWait();
    void exeTurn();
    void exeGotoPatrol();
    void exePatrol();
    bool isInBeamRange(const Vec3f& rPos) const;
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
    f32 mBeamLength;          // Obj_arg0
    f32 mWaterSurfaceHeight;  // Obj_arg1; below 0 has no surface
    bool mIsInMercatorCube;
};
