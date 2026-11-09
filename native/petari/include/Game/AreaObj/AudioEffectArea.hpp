#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudioEffectArea : public AreaObj {
public:
    AudioEffectArea(int, const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    virtual const char* getManagerName() const override {
        return "AudioEffectArea";
    }

    s32 mEffectType;
    s32 mEffectParam;
};