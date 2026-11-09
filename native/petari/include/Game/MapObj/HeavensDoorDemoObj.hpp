#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class HeavensDoorDemoObj : public MapObjActor {
public:
    HeavensDoorDemoObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;

    void exeAppear();
    void exeWait();
    void exeVanish();
    void startInsideCageDemo();

    bool mIsInsideCageDemo;
};
