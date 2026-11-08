#pragma once

#include "Game/Effect/SimpleEffectObj.hpp"

class AstroEffectObj : public SimpleEffectObj {
public:
    AstroEffectObj(const char*);

    virtual void init(const TableIter&);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void setStateMove();

    void selectNrvWait();

    s32 mDomeId;
};
