#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class ActorCameraInfo;

class CapsuleCage : public MapObjActor {
public:
    CapsuleCage(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void connectToScene(const MapObjActorInitInfo&);
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) {};
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) {};
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&);
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) {};

    void startOpen();

    void exeWait();
    void exeStartCamera();
    void exeOpen();
    void exeEndCamera();

    ActorCameraInfo* mCameraInfo;
};
