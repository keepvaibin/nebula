#include "Game/Effect/SpinPullParticleCallBack.hpp"

#include "Game/Util/PlayerUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"

SpinPullParticleCallBack::SpinPullParticleCallBack() : MultiEmitterParticleCallBack() {
    _4 = 1.0f;
}

void SpinPullParticleCallBack::execute(EmitterCore* pEmitter, ParticleCore* pParticle) {
    Vec3f baseParticlePosition;
    baseParticlePosition.set(pParticle->mPosition);
    Vec3f pullvel;
    MR::calcPlayerSpinPullVelocity(&pullvel, baseParticlePosition);
    Vec3f baseParticleOffsetPosition;
    baseParticleOffsetPosition.set(pParticle->mOffsetPosition);

    f32 time = pParticle->mTime;
    pParticle->mOffsetPosition.set(baseParticleOffsetPosition + (((pullvel * _4) * time) * (time)));
}

void MerameraParticleCallBack::execute(EmitterCore* pEmitter, ParticleCore* pParticle) {
    Vec3f baseParticlePosition;
    baseParticlePosition.set(pParticle->mPosition);
    Vec3f pullvel;
    MR::calcPlayerSpinPullVelocity(&pullvel, baseParticlePosition);
    Vec3f baseParticleOffsetPosition;
    baseParticleOffsetPosition.set(pParticle->mOffsetPosition);
    pParticle->mOffsetPosition.set(baseParticleOffsetPosition + (pullvel * _4));
}
