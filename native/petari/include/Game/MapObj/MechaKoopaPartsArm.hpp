#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class MechaKoopaPartsArm : public MapObjActor {
public:
    MechaKoopaPartsArm(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override;

    void exeWait();
    void exeMoveStart();
    void endMoveStart();
    void exeMove();
    void exeBreakStart();
    void exeBreak();
    void startBreak();

    bool mIsRightArm;
};
