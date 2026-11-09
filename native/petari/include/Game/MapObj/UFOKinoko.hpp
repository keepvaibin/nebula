#pragma once

#include "Game/MapObj/MapObjActor.hpp"


class UFOKinoko : public MapObjActor {
public:
    UFOKinoko(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override;


    void startMove();

    void exeWait();
    void exeMove();
    void exeStop();
};
