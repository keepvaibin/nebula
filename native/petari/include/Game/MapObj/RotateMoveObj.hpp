#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class RotateMoveObj : public MapObjActor {
public:
    inline RotateMoveObj(const char* pName) : MapObjActor(pName), mIsGear(false) {
    }

    virtual void init(const TableIter&);
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&);
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&);

    void setStateMove();
    void exeMove();
    void exeStop();
    void exeWait();
    void exeWaitForPlayerOn();

private:
    // The Ocean Ring gears play their moving effect only while turning.
    bool mIsGear;
};
