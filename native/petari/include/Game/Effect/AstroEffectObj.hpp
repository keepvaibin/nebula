#pragma once

#include "Game/Effect/SimpleEffectObj.hpp"

class AstroEffectObj : public SimpleEffectObj {
public:
    AstroEffectObj(const char*);

    virtual void init(const TableIter&) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void setStateMove() override;

    void selectNrvWait();

    s32 mDomeId;
};
