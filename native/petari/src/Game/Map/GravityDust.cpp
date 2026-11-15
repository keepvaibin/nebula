#include "Game/Map/GravityDust.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    const f32 sGravity = 1.0f;
    const f32 sFrictionRate = 0.995f;
};  // namespace

GravityDustParticleCallBack::GravityDustParticleCallBack(const LiveActor* pActor) : MultiEmitterParticleCallBack(), mActor(pActor) {
}

void GravityDustParticleCallBack::execute(EmitterCore* pEmitter, ParticleCore* pParticle) {
    Vec3f offsetPosition;
    Vec3f baseAxis;

    if (pParticle->checkStatus(4)) {
        return;
    }

    offsetPosition.set(pParticle->mOffsetPosition);
    // The particle's user word holds its speed's bits.
    f32 speed = __builtin_bit_cast(f32, static_cast< u32 >(pParticle->getUserWork()));
    baseAxis.set(pParticle->mBaseAxis);
    baseAxis.scale(speed);
    Vec3f gravityVector(0.0f, 0.0f, 0.0f);
    Vec3f currentPosition;
    currentPosition.set< f32 >(pParticle->getCalcCurrentPositionX(pEmitter), pParticle->getCalcCurrentPositionY(pEmitter),
                               pParticle->getCalcCurrentPositionZ(pEmitter));
    MR::calcGravityVectorOrZero(mActor, currentPosition, &gravityVector, nullptr, 0);

    if (MR::isNearZero(gravityVector)) {
        return;
    }

    gravityVector.scale(::sGravity);
    baseAxis.scale(::sFrictionRate);
    baseAxis.add(gravityVector);
    offsetPosition.add(baseAxis);
    pParticle->mOffsetPosition.set(offsetPosition);
    speed = baseAxis.length();

    if (MR::isNearZero(speed)) {
        return;
    }

    pParticle->setUserWork(__builtin_bit_cast(u32, speed));

    if (MR::isNearZero(baseAxis)) {
        return;
    }

    MR::normalize(&baseAxis);
    pParticle->mBaseAxis.set(baseAxis);
}
