#pragma once

#include "Game/Sound/Sound.hpp"

class EffectSound;
struct SoundParams;

class AudSeStrategy : public SoundStrategy< EffectSound > {
public:
    AudSeStrategy() {};

    virtual void calc(EffectSound*);
    virtual void mix(EffectSound*, SoundParams*);
};

class AudSeStrategyMgr : public SoundStrategyMgr< EffectSound > {
public:
    AudSeStrategyMgr() {};

    virtual SoundStrategy< EffectSound >* newStrategy(SoundID);
    virtual bool deleteStrategy(SoundStrategy< EffectSound >*);
};
