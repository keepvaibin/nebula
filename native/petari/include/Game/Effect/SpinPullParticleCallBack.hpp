#pragma once

#include "Game/Effect/MultiEmitterParticleCallBack.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class SpinPullParticleCallBack : public MultiEmitterParticleCallBack {
public:
    SpinPullParticleCallBack();

    virtual void execute(JPABaseEmitter*, JPABaseParticle*);

    f32 _4;
};

class MerameraParticleCallBack : public SpinPullParticleCallBack {
public:
    virtual void execute(JPABaseEmitter*, JPABaseParticle*);
};