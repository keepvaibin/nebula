#pragma once

#include "Game/Sound/SoundAudience3D.hpp"
#include "Game/Sound/SoundAudible.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class SoundID;
struct SoundParams;

// A listener: the camera matrix it hears from, its place and how far it moved this frame.
class SoundListener {
public:
    void resetMtx(const Mtx pMtx);
    void setMtx(const MtxPtr pMtx);
    void convertAbsToRel(const Vec3f& rPos, SoundDopplerRelPos* pRel) const;

    const Vec3f& getPos() const {
        return mPos;
    }

    const Vec3f& getVelocity() const {
        return mVelocity;
    }

    Pos3f mMtx;
    Vec3f mVelocity;
    Vec3f mPos;
    Vec3f mPrevPos;
};

// Where positional sounds are heard from: the listeners (a channel each in every audible) and the distance settings
// that turn an audible's place into its volume, pan, dolby, pitch, effect mix and priority.
class SoundAudience {
public:
    SoundAudience();

    // Listeners start at the identity matrix.
    void setListenerCount(s32 count);

    s32 getListenerCount() const {
        return mListenerCount;
    }

    SoundListener& getListener(s32 index) {
        return mListeners.begin()[index];
    }

    void updateSetting();

    // False (the sound is not positional) when there is no listener.
    bool startAudible(SoundAudible* pAudible, const Vec3f& rPos, SoundID soundID) const;
    // -1 when every channel is past the far distance and the sound drops there.
    u32 calcPriority(SoundAudible* pAudible) const;
    void mixChannelOut(const SoundParams& rParams, SoundAudible* pAudible, s32 channel) const;

    SoundAudience3DSetting mSetting;
    f32 mFrontDolbyRate;
    f32 mBehindDolbyRate;
    f32 mVolumeRate;
    f32 mPriorityRate;
    f32 mFxMixRate;
    bool mFarCamera;

private:
    u32 calcDeltaPriority_(f32 dist, bool excludeFar, s32 distVolBit) const;
    f32 calcVolume_(f32 dist, s32 distVolBit) const;
    f32 calcDolby_(const Vec3f& rPos) const;
    f32 calcPan_(f32 dist, const Vec3f& rPos) const;
    f32 calcPitch(SoundDopplerChannel* pChannel, const SoundAudible* pAudible, const SoundListener& rListener) const;
    f32 calcFxMix(const SoundDopplerChannel* pChannel, const SoundAudible* pAudible) const;

    MR::GrowTable< SoundListener, 1 > mListeners;
    s32 mListenerCount;
};
