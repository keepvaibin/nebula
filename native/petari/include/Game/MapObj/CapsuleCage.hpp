#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class ActorCameraInfo;

class CapsuleCage : public MapObjActor {
public:
    CapsuleCage(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override {};
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override {};
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override {};

    void startOpen();

    void exeWait();
    void exeStartCamera();
    void exeOpen();
    void exeEndCamera();

    ActorCameraInfo* mCameraInfo;
};
