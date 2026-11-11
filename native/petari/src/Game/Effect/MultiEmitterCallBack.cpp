#include "Game/Effect/MultiEmitterCallBack.hpp"
#include "Game/Effect/AutoEffectInfo.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f1, "getLightTint.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f2, "getLightTint.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f3, "getLightTint.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f4, "getLightTint.f4", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f5, "getLightTint.f5", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f6, "getLightTint.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f7, "getLightTint.f7", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f8, "getLightTint.f8", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getLightTint_f9, "getLightTint.f9", 0.5f);

namespace {
    Color8 getSyntheticColor(const Color8& rColor1, const Color8& rColor2) {
        u8 r = rColor1.r * rColor2.r / 255;
        u8 g = rColor1.g * rColor2.g / 255;
        u8 b = rColor1.b * rColor2.b / 255;

        return Color8(r, g, b, 255);
    }

    // The light colour a light-affected effect sits in. The game feeds none, so white: the effect keeps its own colours.
    // A source (the blended ambient of the actor's light, ActorLightCtrl mLightInfo.mColor) goes here, not into setColor.
    Color8 getEffectLight(const MultiEmitter* /*pEmitter*/) {
        return Color8(255, 255, 255, 255);
    }

    // Per channel from white (affect 0) to the light colour (affect 1); LightAffectValue in the effect table.
    Color8 getLightTint(const Color8& rLight, f32 affect) {
        const f32 t = MR::clamp(affect, 0.0f, 1.0f);
        const u8 r = static_cast< u8 >(cNum_getLightTint_f1 + (static_cast< f32 >(rLight.r) - cNum_getLightTint_f2) * t + cNum_getLightTint_f3);
        const u8 g = static_cast< u8 >(cNum_getLightTint_f4 + (static_cast< f32 >(rLight.g) - cNum_getLightTint_f5) * t + cNum_getLightTint_f6);
        const u8 b = static_cast< u8 >(cNum_getLightTint_f7 + (static_cast< f32 >(rLight.b) - cNum_getLightTint_f8) * t + cNum_getLightTint_f9);
        return Color8(r, g, b, 255);
    }

    s16 toS16(f32 deg) {
        return (s32)(DEGREE_TO_S16 * deg);
    }
};  // namespace

MultiEmitterCallBack::MultiEmitterCallBack(const MultiEmitter* pEmitter, const Vec3f& rOffset)
    : MultiEmitterCallBackBase(), mEmitter(pEmitter), mHostTranslation(), mHostRotation(), mHostScale(), mHostMtx(), mOffset(rOffset),
      mBaseScale(1.0f), mPrmColor(-1, -1, -1, -1), mEnvColor(-1, -1, -1, -1), mFlags() {
}

MultiEmitterCallBack::MultiEmitterCallBack(const MultiEmitter* pEmitter, const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale,
                                           const Vec3f& rOffset)
    : MultiEmitterCallBackBase(), mEmitter(pEmitter), mHostTranslation(pTranslation), mHostRotation(pRotation), mHostScale(pScale), mHostMtx(),
      mOffset(rOffset), mBaseScale(1.0f), mPrmColor(-1, -1, -1, -1), mEnvColor(-1, -1, -1, -1), mFlags() {
}

MultiEmitterCallBack::MultiEmitterCallBack(const MultiEmitter* pEmitter, MtxPtr pMtx, const Vec3f& rOffset)
    : MultiEmitterCallBackBase(), mEmitter(pEmitter), mHostTranslation(), mHostRotation(), mHostScale(), mHostMtx(pMtx), mOffset(rOffset),
      mBaseScale(1.0f), mPrmColor(-1, -1, -1, -1), mEnvColor(-1, -1, -1, -1), mFlags() {
}

MultiEmitterCallBack::MultiEmitterCallBack(const MultiEmitter* pEmitter, MtxPtr pMtx, const Vec3f* pScale, const Vec3f& rOffset)
    : MultiEmitterCallBackBase(), mEmitter(pEmitter), mHostTranslation(), mHostRotation(), mHostScale(pScale), mHostMtx(pMtx), mOffset(rOffset),
      mBaseScale(1.0f), mPrmColor(-1, -1, -1, -1), mEnvColor(-1, -1, -1, -1), mFlags() {
}

void MultiEmitterCallBack::execute(EmitterCore* pEmitter) {
    follow(pEmitter, false);
    setColor(pEmitter);
}

void MultiEmitterCallBack::init(EmitterCore* pEmitter) {
    follow(pEmitter, true);
}

void MultiEmitterCallBack::setHostSRT(const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale) {
    mHostTranslation = pTranslation;
    mHostRotation = pRotation;
    mHostScale = pScale;
    mHostMtx = nullptr;
}

void MultiEmitterCallBack::setHostMtx(MtxPtr pMtx) {
    mHostMtx = pMtx;
    mHostTranslation = nullptr;
    mHostRotation = nullptr;
    mHostScale = nullptr;
}

void MultiEmitterCallBack::setBaseScale(f32 scale) {
    turnFlagOn(BASE_SCALE);
    mBaseScale = scale;
}

void MultiEmitterCallBack::forceFollowOn() {
    turnFlagOn(FORCE_FOLLOW_ON);
}

void MultiEmitterCallBack::forceFollowOff() {
    turnFlagOn(FORCE_FOLLOW_OFF);
}

void MultiEmitterCallBack::forceScaleOn() {
    turnFlagOn(FORCE_SCALE_ON);
}

MultiEmitterCallBack::Follow MultiEmitterCallBack::follows(bool atCreate) const {
    Follow follow;
    if (!atCreate && isFlagOn(FORCE_FOLLOW_OFF)) {
        return follow;
    }

    if (const AutoEffectInfo* pInfo = mEmitter->mAutoInfo; pInfo != nullptr) {
        const u32 flag = pInfo->mFlag;
        if (atCreate) {
            follow.mTranslation = (flag & AutoEffectInfo::FLAG_AFFECT_TRANS) != 0;
            follow.mRotation = (flag & AutoEffectInfo::FLAG_AFFECT_ROTATE) != 0;
            follow.mScale = (flag & AutoEffectInfo::FLAG_AFFECT_SCALE) != 0;
        } else {
            follow.mTranslation = (flag & AutoEffectInfo::FLAG_FOLLOW_TRANS) != 0;
            follow.mRotation = (flag & AutoEffectInfo::FLAG_FOLLOW_ROTATE) != 0;
            follow.mScale = (flag & AutoEffectInfo::FLAG_FOLLOW_SCALE) != 0;
        }
    }

    const bool hostMtx = mHostMtx != nullptr;
    follow.mTranslation = (hostMtx || mHostTranslation != nullptr) && (follow.mTranslation || isFlagOn(FORCE_FOLLOW_ON));
    follow.mRotation = (hostMtx || mHostRotation != nullptr) && follow.mRotation;
    follow.mScale = (hostMtx || mHostScale != nullptr) && (follow.mScale || isFlagOn(FORCE_SCALE_ON));
    return follow;
}

void MultiEmitterCallBack::follow(EmitterCore* pEmitter, bool atCreate) {
    const Follow follow = follows(atCreate);
    if (!follow.any() && !isFlagOn(BASE_SCALE)) {
        return;
    }

    if (mHostMtx != nullptr) {
        followHostMtx(pEmitter, follow, atCreate);
    } else {
        followHostSRT(pEmitter, follow, atCreate);
    }
}

void MultiEmitterCallBack::followHostMtx(EmitterCore* pEmitter, const Follow& rFollow, bool atCreate) {
    Pos3f mtx = mHostMtx;
    Rot3f rotation;
    Vec3f scale;
    Vec3f translation;
    ParticleMath::splitSRT(mtx, rotation, &scale, &translation);

    if (rFollow.mTranslation) {
        Vec3f offset;
        rotation.mult33(mOffset, offset);
        if (rFollow.mScale) {
            offset.mul(scale);
        }

        translation.add(offset);
        pEmitter->mGlobalTrs.set(translation);
    }

    if (rFollow.mRotation) {
        if (MR::Effect::isEffect2D(mEmitter)) {
            Rot3f flip;
            flip.identity();
            flip.setEulerZ(PI);
            rotation.concat(flip);
        }

        ParticleMath::splitR(rotation, pEmitter->mGlobalRot);
    }

    setScale(pEmitter, scale, rFollow.mScale, atCreate);
}

void MultiEmitterCallBack::followHostSRT(EmitterCore* pEmitter, const Follow& rFollow, bool atCreate) {
    if (rFollow.mTranslation) {
        Vec3f offset;
        if (rFollow.mRotation) {
            Rot3f rotation;
            rotation.identity();
            rotation.setRotateDegree(*mHostRotation);
            rotation.mult33(mOffset, offset);
        } else {
            offset.set(mOffset);
        }

        if (rFollow.mScale) {
            offset.mul(*mHostScale);
        }

        offset.add(*mHostTranslation);
        pEmitter->mGlobalTrs.set(offset);
    }

    if (rFollow.mRotation) {
        ParticleMath::xyzRotateMtx(toS16(mHostRotation->x), toS16(mHostRotation->y), toS16(mHostRotation->z), pEmitter->mGlobalRot);
    }

    setScale(pEmitter, Vec3f(1.0f, 1.0f, 1.0f), rFollow.mScale, atCreate);
}

// A host scale vector wins over the matrix's.
void MultiEmitterCallBack::setScale(EmitterCore* pEmitter, const Vec3f& rHostScale, bool followScale, bool atCreate) {
    Vec3f scale(rHostScale);

    if (followScale) {
        if (mHostScale != nullptr) {
            scale.set(*mHostScale);
        }

        if (isFlagOn(BASE_SCALE)) {
            scale.scale(mBaseScale);
        }
        pEmitter->setGlobalScale(scale);
    } else if (atCreate && isFlagOn(BASE_SCALE)) {
        scale.setAll< f32 >(mBaseScale);
        pEmitter->setGlobalScale(scale);
    }
}

// With a light affect the emitter's own colours are tinted; both tints take its prm colour.
void MultiEmitterCallBack::setColor(EmitterCore* pEmitter) {
    if (MR::isNearZero(mEmitter->mLightAffect)) {
        pEmitter->setGlobalPrmColor(mPrmColor.r, mPrmColor.g, mPrmColor.b);
        pEmitter->setGlobalEnvColor(mEnvColor.r, mEnvColor.g, mEnvColor.b);
        return;
    }

    // The tint is the light mixed by the effect's light affect value times the effect's own colours, which are kept in
    // mPrmColor/mEnvColor. It must not be read back from the emitter: that product was written there last step, so a
    // tint of 200 went 200, 156, 122, ... black in 19 steps.
    const Color8 tint = ::getLightTint(::getEffectLight(mEmitter), mEmitter->mLightAffect);
    Color8 syntheticPrm = ::getSyntheticColor(tint, mPrmColor);
    pEmitter->setGlobalPrmColor(syntheticPrm.r, syntheticPrm.g, syntheticPrm.b);
    Color8 syntheticEnv = ::getSyntheticColor(tint, mEnvColor);
    pEmitter->setGlobalEnvColor(syntheticEnv.r, syntheticEnv.g, syntheticEnv.b);
}
