#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class CollisionParts;
class VolumeModelDrawer;

class BeamGoRoundBeam : public LiveActor {
public:
    BeamGoRoundBeam(MtxPtr);

    virtual void init(const TableIter&);
    virtual void draw() const;
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    VolumeModelDrawer* mModelDrawer;
    ModelObj* mBloomModel;
    MtxPtr mBeamJointMtx;
};

class BeamGoRoundPlanet : public MapObjActor {
public:
    BeamGoRoundPlanet(const char*);

    virtual void init(const TableIter&);
    virtual void connectToScene(const MapObjActorInitInfo&);

    void initBeam();
    void exeWait();

    BeamGoRoundBeam** mBeams;
    CollisionParts* mMove1Collision;
    CollisionParts* mMove2Collision;
};
