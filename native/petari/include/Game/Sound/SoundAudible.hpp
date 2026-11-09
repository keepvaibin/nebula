#pragma once

#include "Game/Sound/SoundAudibleParam.hpp"
#include "Game/Sound/SoundDoppler.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

// A positional sound's place and, per listener, where the sound is from that listener and the values mixed for it.
// Part of its Sound; the channel room grows only past one listener.
class SoundAudible {
public:
    void start(const Vec3f& rPos, SoundAudibleParam param, s32 channelCount) {
        mAudibleParam = param;
        mAbsPos.init(&mPos, rPos, nullptr);
        mChannelCount = channelCount;
        for (s32 i = 0; i < channelCount; i++) {
            SoundDopplerChannel& rChannel = mChannels.at(i);
            rChannel.mParams.init();
            rChannel.mPitch = 1.0f;
        }
        mVolume = 1.0f;
        mPan = 0.5f;
        mDolby = 0.0f;
        mIsResetting = true;
    }

    void calc() {
        mAbsPos.calc(mPos);
    }

    s32 getChannelCount() const {
        return mChannelCount;
    }

    SoundDopplerChannel& getChannel(s32 index) {
        return mChannels.begin()[index];
    }

    const SoundParams& getOuterParams(s32 index) const {
        return mChannels.begin()[index].mParams;
    }

    void setOuterParams(const SoundParams& rOutParams, const SoundParams& rInParams, s32 index) {
        getChannel(index).mParams.combine(rOutParams, rInParams);
    }

    const Vec3f& getPos() const {
        return mPos;
    }

    void setPos(const Vec3f& rPos) {
        mPos.set(rPos);
    }

    const SoundAudibleParam& getAudibleParam() const {
        return mAudibleParam;
    }

    u32 getVolDistBit() const {
        return mAudibleParam.getVolDistBit();
    }

    const Vec3f& getVelocity() const {
        return mAbsPos.getVelocity();
    }

    Vec3f mPos;
    SoundAudibleParam mAudibleParam;
    SoundDopplerAbsPos mAbsPos;
    MR::GrowTable< SoundDopplerChannel, 1 > mChannels;
    s32 mChannelCount;
    f32 mVolume;
    f32 mPan;
    f32 mDolby;
    bool mIsResetting;
};
