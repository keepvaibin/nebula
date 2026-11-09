#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class CollisionParts;
class VolumeModelDrawer;

class BeamGoRoundBeam : public LiveActor {
public:
    BeamGoRoundBeam(MtxPtr);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void calcAndSetBaseMtx() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    VolumeModelDrawer* mModelDrawer;
    ModelObj* mBloomModel;
    MtxPtr mBeamJointMtx;
};

class BeamGoRoundPlanet : public MapObjActor {
public:
    BeamGoRoundPlanet(const char*);

    virtual void init(const TableIter&) override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;

    void initBeam();
    void exeWait();

    BeamGoRoundBeam** mBeams;
    CollisionParts* mMove1Collision;
    CollisionParts* mMove2Collision;
};
