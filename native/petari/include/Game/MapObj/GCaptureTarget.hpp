#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/BlueStarCupsulePlanet.hpp"

class MapPartsRailMover;
class ActorCameraInfo;

class GCaptureTarget : public LiveActor, public GCaptureTargetable {
public:
    GCaptureTarget(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    void decidedTarget();
    void releasedTarget();
    void emitNerveEffect();
    void getTargetPosition(TVec3f*);
    bool isReleaseForce() const;
    f32 getPointableRange() const;
    f32 releaseDistance() const;

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
