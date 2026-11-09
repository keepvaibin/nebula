#pragma once

#include "Game/Effect/MultiEmitterParticleCallBack.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

// Particles pulled toward the player's spin, by the square of their age.
class SpinPullParticleCallBack : public MultiEmitterParticleCallBack {
public:
    SpinPullParticleCallBack();

    virtual void execute(EmitterCore*, ParticleCore*) override;
};

// Pulled by the spin velocity alone.
class MerameraParticleCallBack : public SpinPullParticleCallBack {
public:
    virtual void execute(EmitterCore*, ParticleCore*) override;
};
