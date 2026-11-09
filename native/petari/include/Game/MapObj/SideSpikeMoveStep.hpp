#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class SideSpikeMoveStep : public MapObjActor {
public:
    SideSpikeMoveStep(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exePointerStop();
    void endPointerStop();
    void exePointerStopSync();
    void endPointerStopSync();

private:
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStateBindStarPointer;
    Vec3f mRailClipping;
};
