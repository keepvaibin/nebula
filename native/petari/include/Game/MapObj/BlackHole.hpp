#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>


class ActorCameraInfo;
class ModelObj;

class BlackHole : public LiveActor {
public:
    BlackHole(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    bool tryStartDemoCamera();
    void initMapToolInfo(const TableIter&);
    void initModel();
    void initCubeBox();
    bool isInCubeBox(const Vec3f&) const;
    void updateModelScale(f32 rangeScale, f32 coreScale);
    void exeWait();
    void exeDemo();
    void exeDisappear();

    ModelObj* mBlackHoleModel;
    f32 mModelScale;
    f32 mRadius;
    Box3f* mCubeBox;
    Pos3f mCubeMtx;
    Pos3f mSuctionEffectMtx;
    ActorCameraInfo* mCameraInfo;
};
