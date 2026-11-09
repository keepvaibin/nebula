#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/BlueStarCupsulePlanet.hpp"

class MapPartsRailMover;
class ActorCameraInfo;

class GCaptureTarget : public LiveActor, public GCaptureTargetable {
public:
    GCaptureTarget(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;

    void decidedTarget() override;
    void releasedTarget() override;
    void emitNerveEffect();
    void getTargetPosition(Vec3f*) override;
    bool isReleaseForce() const override;
    f32 getPointableRange() const override;
    f32 releaseDistance() const override;

    void exeTryDemoAppear();
    void exeAppear();
    void exeWait();
    void exePointable();
    void exeHitPointer();
    void exeActive();

    MapPartsRailMover* mRailMover;
    f32 mStarAnimSpeed;
    ActorCameraInfo* mCameraInfo;
    f32 mReleaseDistance;
    f32 mPointableRange;
    bool mFarAwayColor;
};
