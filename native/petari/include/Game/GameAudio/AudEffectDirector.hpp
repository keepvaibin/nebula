#pragma once

#include "Game/NameObj/NameObj.hpp"

class AudEffectDirector : public NameObj {
public:
    AudEffectDirector();

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;

    void setEffectType(s32, s32);
    void setAudioEffectParam(s32);
    void playEffectSound(s32);

    s32 mEffectType;
    s32 mAppliedType;
    s32 mPriority;
};
