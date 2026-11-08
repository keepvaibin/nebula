#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudioEffectArea : public AreaObj {
public:
    AudioEffectArea(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();

    virtual const char* getManagerName() const {
        return "AudioEffectArea";
    }

    s32 mEffectType;
    s32 mEffectParam;
};