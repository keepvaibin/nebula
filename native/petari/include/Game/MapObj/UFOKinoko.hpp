#pragma once

#include "Game/MapObj/MapObjActor.hpp"


class UFOKinoko : public MapObjActor {
public:
    UFOKinoko(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&);
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&);


    void startMove();

    void exeWait();
    void exeMove();
    void exeStop();
};
