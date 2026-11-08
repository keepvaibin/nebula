#pragma once

#include "Game/Effect/MultiEmitterParticleCallBack.hpp"

class LiveActor;

class GravityDustParticleCallBack : public MultiEmitterParticleCallBack {
public:
    GravityDustParticleCallBack(const LiveActor* pActor);

    virtual void execute(EmitterCore* pEmitter, ParticleCore* pParticle);

    const LiveActor* mActor;
};
