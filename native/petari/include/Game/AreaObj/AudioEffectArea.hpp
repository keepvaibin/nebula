#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class AudioEffectArea : public AreaObj {
public:
    AudioEffectArea(int, const char*);

    virtual void init(const JMapInfoIter&);
    virtual void movement();

    virtual const char* getManagerName() const {
        return "AudioEffectArea";
    }

    s32 _3C;
    s32 _40;
};