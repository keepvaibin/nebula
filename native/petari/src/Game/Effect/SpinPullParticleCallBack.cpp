#include "Game/Effect/SpinPullParticleCallBack.hpp"

#include "Game/Util/PlayerUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"

SpinPullParticleCallBack::SpinPullParticleCallBack() : MultiEmitterParticleCallBack() {
}

void SpinPullParticleCallBack::execute(EmitterCore* /*pEmitter*/, ParticleCore* pParticle) {
    Vec3f pullVelocity;
    MR::calcPlayerSpinPullVelocity(&pullVelocity, pParticle->mPosition);

    f32 time = pParticle->mTime;
    pParticle->mOffsetPosition.set(pParticle->mOffsetPosition + ((pullVelocity * time) * time));
}

void MerameraParticleCallBack::execute(EmitterCore* /*pEmitter*/, ParticleCore* pParticle) {
    Vec3f pullVelocity;
    MR::calcPlayerSpinPullVelocity(&pullVelocity, pParticle->mPosition);
    pParticle->mOffsetPosition.set(pParticle->mOffsetPosition + pullVelocity);
}
