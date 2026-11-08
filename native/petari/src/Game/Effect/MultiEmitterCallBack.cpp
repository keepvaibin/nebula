#include "Game/Effect/MultiEmitterCallBack.hpp"
#include "Game/Effect/AutoEffectInfo.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"

namespace {
    Color8 getSyntheticColor(const Color8& rColor1, const Color8& rColor2) {
        u8 r = rColor1.r * rColor2.r / 255;
        u8 g = rColor1.g * rColor2.g / 255;
        u8 b = rColor1.b * rColor2.b / 255;

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

    NebulaColor8 prmColor;
    prmColor = pEmitter->mGlobalPrmClr;
    Color8 syntheticPrm = ::getSyntheticColor(prmColor, mPrmColor);
    pEmitter->setGlobalPrmColor(syntheticPrm.r, syntheticPrm.g, syntheticPrm.b);
    Color8 syntheticEnv = ::getSyntheticColor(prmColor, mEnvColor);
    pEmitter->setGlobalEnvColor(syntheticEnv.r, syntheticEnv.g, syntheticEnv.b);
}
