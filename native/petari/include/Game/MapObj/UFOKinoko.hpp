#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class NameObjArchiveListCollector;

class UFOKinoko : public MapObjActor {
public:
    UFOKinoko(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&);
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&);

    static void makeArchiveList(NameObjArchiveListCollector*, const TableIter&);

    void startMove();

    void exeWait();
    void exeMove();
    void exeStop();
};
