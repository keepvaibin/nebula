#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MiniatureGalaxy;
class PartsModel;

class AstroDomeComet : public LiveActor {
public:
    AstroDomeComet(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initSubModel();
    void tryPointing();
    void exeSelect();
    void exeConfirm();

    PartsModel* mBloomModel;
    MiniatureGalaxy* mMiniGalaxy;
    LiveActor** mPointingActors;
};
