#pragma once

#include "JSystem/JAudio2/JASInstEffect.hpp"

struct JASInstParam;

class JASInstRand : public JASInstEffect {
public:
    JASInstRand() : JASInstEffect(), mType(VOLUME), mBaseValue(1.0f), mRange(0.0f){};

    virtual void effect(int pitch, int velocity, JASInstParam* pParams) const;

    inline void setBase(f32 base) {
        mBaseValue = base;
    }

    inline void setRange(f32 range) {
        mRange = range;
    }

    u32 mType;
    f32 mBaseValue;
    f32 mRange;
};
