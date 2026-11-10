#include "Game/Sound/SoundAudience.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundParams.hpp"
#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ease_f1, "ease.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_ease_f2, "ease.f2", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ease_f3, "ease.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_ease_f4, "ease.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_awayFromZero_f1, "awayFromZero.f1", 0.002f);
NEBULA_TUNABLE_NAMED(f32, cNum_awayFromZero_f2, "awayFromZero.f2", 0.002f);
NEBULA_TUNABLE_NAMED(f32, cNum_awayFromZero_f3, "awayFromZero.f3", 0.002f);
NEBULA_TUNABLE_NAMED(f32, cNum_awayFromZero_f4, "awayFromZero.f4", 0.002f);
NEBULA_TUNABLE_NAMED(f32, cNum_mixChannelOut_f1, "mixChannelOut.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_mixChannelOut_f2, "mixChannelOut.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcDeltaPriority__f1, "calcDeltaPriority_.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcDeltaPriority__f2, "calcDeltaPriority_.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcVolume__f1, "calcVolume_.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcVolume__f2, "calcVolume_.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPan__f1, "calcPan_.f1", 0.5f);

namespace {
    // Eases a value toward its target by a fifth once they are 0.2 or more apart.
    f32 ease(f32 target, f32 current) {
        if (target > current) {
            if (target - current >= cNum_ease_f1) {
                return current + (target - current) / cNum_ease_f2;
            }
        } else {
            if (current - target >= cNum_ease_f3) {
                return target + (current - target) / cNum_ease_f4;
            }
        }
        return target;
    }

    // A component under 0.002 from zero becomes 0.002 with its sign.
    f32 awayFromZero(f32 value) {
        if (value > -cNum_awayFromZero_f1 && value < cNum_awayFromZero_f2) {
            return value < 0.0f ? -cNum_awayFromZero_f3 : cNum_awayFromZero_f4;
        }
        return value;
    }
}  // namespace

void SoundListener::resetMtx(const Mtx pMtx) {
    mMtx.set(pMtx);
    Pos3f lookAt;
    lookAt.setPositionFromLookAt(mMtx);
    lookAt.getTrans(mPos);
    mPrevPos.set(mPos);
    mVelocity.zero();
}

void SoundListener::setMtx(const MtxPtr pMtx) {
    mPrevPos.set(mPos);
    mMtx.set(pMtx);
    Pos3f lookAt;
    lookAt.setPositionFromLookAt(mMtx);
    lookAt.getTrans(mPos);
    mVelocity.sub(mPos, mPrevPos);
}

void SoundListener::convertAbsToRel(const Vec3f& rPos, SoundDopplerRelPos* pRel) const {
    Vec3f diff;
    diff.sub(rPos, mPos);
    diff.x = awayFromZero(diff.x);
    diff.y = awayFromZero(diff.y);
    diff.z = awayFromZero(diff.z);

    NebulaMtxMultVecSR(mMtx, diff, pRel->mPos);
    pRel->mDir.set(diff);
    pRel->mDist = pRel->mDir.normalize();
}

SoundAudience::SoundAudience() : mFarCamera(false), mListenerCount(0) {
    updateSetting();
}

void SoundAudience::setListenerCount(s32 count) {
    Pos3f mtx;
    mtx.identity();
    for (s32 i = 0; i < count; i++) {
        mListeners.at(i).resetMtx(mtx);
    }
    mListenerCount = count;
}

void SoundAudience::updateSetting() {
    mFrontDolbyRate = (mSetting.mMinDolby - mSetting.mCenterDolby) / mSetting.mFrontDolbyZ;
    mBehindDolbyRate = (mSetting.mMaxDolby - mSetting.mCenterDolby) / mSetting.mBehindDolbyZ;
    mVolumeRate = (mSetting.mMinVolume - 1.0f) / (mSetting.mFarDistance - mSetting.mNearDistance);
    mPriorityRate = mSetting.mPriorityDownRange / (mSetting.mFarDistance - mSetting.mNearDistance);
    mFxMixRate = (mSetting.mMaxFxMix - mSetting.mMinFxMix) / (mSetting.mFarDistance - mSetting.mNearDistance);
}

bool SoundAudience::startAudible(SoundAudible* pAudible, const Vec3f& rPos, SoundID soundID) const {
    if (mListenerCount == 0) {
        return false;
    }

    pAudible->start(rPos, SoundAudibleParam(StdSoundInfo::getInstance()->getAudibleSw(soundID), -1), mListenerCount);
    for (s32 i = 0; i < mListenerCount; i++) {
        mListeners.begin()[i].convertAbsToRel(pAudible->getPos(), &pAudible->getChannel(i).mRelPos);
    }
    return true;
}

u32 SoundAudience::calcPriority(SoundAudible* pAudible) const {
    const s32 count = pAudible->getChannelCount();
    if (pAudible->getAudibleParam().get_BIT11() == 0) {
        for (s32 i = 0; i < count; i++) {
            mListeners.begin()[i].convertAbsToRel(pAudible->getPos(), &pAudible->getChannel(i).mRelPos);
        }

        return 0;
    }

    u32 priority = -1;
    for (s32 i = 0; i < count; i++) {
        SoundDopplerChannel& rChannel = pAudible->getChannel(i);
        const u32 volDistBit = pAudible->getAudibleParam().getVolDistBit();
        mListeners.begin()[i].convertAbsToRel(pAudible->getPos(), &rChannel.mRelPos);
        const bool bit = pAudible->getAudibleParam().get_BIT8();
        const u32 deltaPriority = calcDeltaPriority_(rChannel.mRelPos.getDist(), bit, volDistBit);
        if (deltaPriority < priority) {
            priority = deltaPriority;
        }
    }

    return priority;
}

void SoundAudience::mixChannelOut(const SoundParams& rParams, SoundAudible* pAudible, s32 channel) const {
    SoundDopplerChannel* pChannel = &pAudible->getChannel(channel);
    const SoundAudibleParam& rParam = pAudible->getAudibleParam();
    SoundParams params;

    if (rParam.calcVolume()) {
        params.mVolume = calcVolume_(pChannel->mRelPos.getDist(), pAudible->getVolDistBit());
        const f32 vol = !pAudible->mIsResetting ? ease(params.mVolume, pAudible->mVolume) : params.mVolume;
        pAudible->mVolume = vol;
        params.mVolume = vol;
    } else {
        params.mVolume = 1.0f;
    }

    if (rParam.calcDolby()) {
        params.mDolby = calcDolby_(pChannel->mRelPos.getPos());
        const f32 dolby = !pAudible->mIsResetting ? ease(params.mDolby, pAudible->mDolby) : params.mDolby;
        pAudible->mDolby = dolby;
        params.mDolby = dolby;
    } else {
        params.mDolby = cNum_mixChannelOut_f1;
    }

    if (rParam.calcPan()) {
        if (mListenerCount > 2) {
            params.mPan = (channel & 1) != 0 ? 1.0f : 0.0f;
        } else {
            params.mPan = calcPan_(pChannel->mRelPos.getDist(), pChannel->mRelPos.getPos());
        }
        const f32 pan = !pAudible->mIsResetting ? ease(params.mPan, pAudible->mPan) : params.mPan;
        pAudible->mPan = pan;
        params.mPan = pan;
    } else {
        params.mPan = cNum_mixChannelOut_f2;
    }

    params.mPitch = calcPitch(pChannel, pAudible, mListeners.begin()[channel]);
    params.mFxMix = calcFxMix(pChannel, pAudible);

    pAudible->setOuterParams(rParams, params, channel);
    pAudible->mIsResetting = false;
}

u32 SoundAudience::calcDeltaPriority_(f32 dist, bool excludeFar, s32 distVolBit) const {
    f32 farDist = distVolBit == 8 ? mSetting.mFarDistance / cNum_calcDeltaPriority__f1 : mSetting.mFarDistance * distVolBit;

    if (mFarCamera) {
        farDist *= cNum_calcDeltaPriority__f2;
    }

    if (dist > farDist) {
        if (excludeFar) {
            return 0xFFFFFFFF;
        }

        return mSetting.mPriorityDownRange;
    }

    if (dist < mSetting.mNearDistance) {
        return 0;
    }

    f32 priorityRate = distVolBit == 1 ? mPriorityRate : mSetting.mPriorityDownRange / (farDist - mSetting.mNearDistance);
    return priorityRate * (dist - mSetting.mNearDistance);
}

f32 SoundAudience::calcVolume_(f32 dist, s32 distVolBit) const {
    f32 farDist = distVolBit == 8 ? mSetting.mFarDistance / cNum_calcVolume__f1 : mSetting.mFarDistance * distVolBit;

    if (mFarCamera) {
        farDist *= cNum_calcVolume__f2;
    }

    if (dist > farDist) {
        return mSetting.mMinVolume;
    }

    if (dist < mSetting.mNearDistance) {
        return 1.0f;
    }

    f32 volumeRate = distVolBit == 1 ? mVolumeRate : (mSetting.mMinVolume - 1.0f) / (farDist - mSetting.mNearDistance);
    return MR::clamp(volumeRate * (dist - mSetting.mNearDistance) + 1.0f, 0.0f, 1.0f);
}

f32 SoundAudience::calcDolby_(const Vec3f& rPos) const {
    f32 z = rPos.z;
    if (z > mSetting.mBehindDolbyZ) {
        return mSetting.mMaxDolby;
    }

    if (z < mSetting.mFrontDolbyZ) {
        return mSetting.mMinDolby;
    }

    if (z < 0.0f) {
        return mSetting.mCenterDolby + mFrontDolbyRate * z;
    } else {
        return mSetting.mCenterDolby + mBehindDolbyRate * z;
    }
}

f32 SoundAudience::calcPan_(f32 dist, const Vec3f& rPos) const {
    f32 panRate = dist <= MR::epsilon() ? 0.0f : rPos.x / dist;

    return panRate * mSetting.mPanAmplitude + cNum_calcPan__f1;
}

f32 SoundAudience::calcPitch(SoundDopplerChannel* pChannel, const SoundAudible* pAudible, const SoundListener& rListener) const {
    const SoundAudibleParam& rParam = pAudible->getAudibleParam();
    if (rParam.getDoppler() == 0) {
        return 1.0f;
    }

    f32 doppler = rParam.getDopplerPower();
    f32 vel1 = doppler * pChannel->getDir().dot(rListener.getVelocity());
    f32 vel2 = doppler * pChannel->getDir().dot(pAudible->getVelocity());
    f32 pitch = (mSetting.mSonicSpeed + vel1) / (mSetting.mSonicSpeed + vel2);

    if (pChannel->mPitch > 0.0f) {
        if (pChannel->mPitch > pitch * mSetting.mPitchDeltaRatioRange) {
            return pChannel->mPitch;
        }

        if (pitch > pChannel->mPitch * mSetting.mPitchDeltaRatioRange) {
            return pChannel->mPitch;
        }

        pChannel->mPitch = pitch;
        return pitch;
    }

    if (pitch > 0.0f) {
        pChannel->mPitch = pitch;
        return pitch;
    }

    return 1.0f;
}

f32 SoundAudience::calcFxMix(const SoundDopplerChannel* pChannel, const SoundAudible* pAudible) const {
    if (!pAudible->getAudibleParam().calcFxMix()) {
        return 0.0f;
    }

    const f32 dist = pChannel->mRelPos.getDist();
    if (dist > mSetting.mFarDistance) {
        return mSetting.mMaxFxMix;
    }

    if (dist < mSetting.mNearDistance) {
        return mSetting.mMinFxMix;
    }

    return mSetting.mMinFxMix + mFxMixRate * (dist - mSetting.mNearDistance);
}
