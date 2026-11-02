#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/GCapture.hpp"

class MapPartsRailMover;
class MapPartsRotator;

namespace {
    struct BlueStarCupsulePlanetParam {
        const char* mBlueStarName;
        f32 mRadius;
    };
};  // namespace

class BlueStarCupsulePlanet : public LiveActor, public GCaptureTargetable {
public:
    BlueStarCupsulePlanet(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void updatePose();
    void decidedTarget();
    void releasedTarget();
    void getTargetPosition(TVec3f*);
    bool isReleaseForce() const;
    f32 getPointableRange() const;
    void emitNerveEffect();
    void exeWait();
    void exePointable();
    void exeHitPointer();
    void exeActive();
    bool isPointable() const;
    void activateMapPartsFunction();
    void deactivateMapPartsFunction();

    MapPartsRailMover* mRailMover;
    MapPartsRotator* mRotator;
    const char* mName;
    bool mIsStepUpdatable;
};
