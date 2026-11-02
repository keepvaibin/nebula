#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class SideSpikeMoveStep : public MapObjActor {
public:
    SideSpikeMoveStep(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeDPDStop();
    void endDPDStop();
    void exeDPDStopSync();
    void endDPDStopSync();

private:
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStateBindStarPointer;
    TVec3f mRailClipping;
};
