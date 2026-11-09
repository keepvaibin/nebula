#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class PartsModel;

class PeachCastleGardenPlanet : public MapObjActor {
public:
    PeachCastleGardenPlanet(const char*);

    virtual void init(const TableIter&) override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;

    void startDamage();
    void exeWait();
    void exeDamage();

};
