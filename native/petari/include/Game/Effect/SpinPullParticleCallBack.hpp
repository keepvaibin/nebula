#pragma once

#include "Game/Effect/MultiEmitterParticleCallBack.hpp"
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

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