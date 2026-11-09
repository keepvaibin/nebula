#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MiniatureGalaxy;
class PartsModel;

class AstroDomeComet : public LiveActor {
public:
    AstroDomeComet(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initSubModel();
    void tryPointing();
    void exeSelect();
    void exeConfirm();

    PartsModel* mBloomModel;
    MiniatureGalaxy* mMiniGalaxy;
    LiveActor** mPointingActors;
};
