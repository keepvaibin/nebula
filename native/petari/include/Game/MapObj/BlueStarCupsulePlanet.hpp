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

    virtual void init(const TableIter&) override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void updatePose();
    void decidedTarget() override;
    void releasedTarget() override;
    void getTargetPosition(Vec3f*) override;
    bool isReleaseForce() const override;
    f32 getPointableRange() const override;
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
