#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class RotateMoveObj : public MapObjActor {
public:
    inline RotateMoveObj(const char* pName) : MapObjActor(pName), mIsGear(false) {
    }

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override;

    void setStateMove();
    void exeMove();
    void exeStop();
    void exeWait();
    void exeWaitForPlayerOn();

private:
    // The Ocean Ring gears play their moving effect only while turning.
    bool mIsGear;
};
