#pragma once

#include "Game/AudioLib/AudAudience.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundParams.hpp"
#include "Game/Sound/SoundAudible.hpp"
#include "Game/Sound/SoundAudibleParam.hpp"

class SoundDopplerChannel;
class SoundDopplerAbsPos;
class AudAudience_withSetting;

template < int SIZE, class T, class U >
class AudGenericAudible_ : public SoundAudible {
public:
    AudGenericAudible_(const Vec3f& rPos, const Vec3f* pPos, u32 mask) : SoundAudible(), mAbsPos() {
        mAudibleParam = -1;
        mAbsPos.init(&mPos, rPos, pPos);

        for (int i = 0; i < SIZE; i++) {
            if ((mask & 1 << i) == 0) {
                mChannelData[i].mParams.init();
                mChannelData[i].mPitch = 1.0f;
                mChannels[i] = &mChannelData[i];
            } else {
                mChannels[i] = nullptr;
            }
        }

        mVolume = 1.0f;
        mPan = 0.5f;
        mDolby = 0.0f;
        mIsResetting = true;
    }

    virtual SoundParams* getOuterParams(int index) {
        T* channel = getChannel(index);
        if (channel != nullptr) {
            return &channel->mParams;
        }
        return nullptr;
    }

    void setOuterParams(const SoundParams& rOutParams, const SoundParams& rInParams, int index) {
        getOuterParams(index)->combine(rOutParams, rInParams);
    }

    virtual void calc() {
        mAbsPos.calc(mPos);
    }

    const Vec3f& getPos() const {
        return mPos;
    }

    T* getChannel(int index) {
        return mChannels[index];
    }

    SoundAudibleParam& getAudibleParam() {
        return mAudibleParam;
    }

    u32 getVolDistBit() {
        return getAudibleParam().getVolDistBit();
    }

    const Vec3f& getVelocity() const {
        return mAbsPos.getVelocity();
    }

    SoundAudibleParam mAudibleParam;
    U mAbsPos;
    T* mChannels[SIZE];
    T mChannelData[SIZE];
    f32 mVolume;
    f32 mPan;
    f32 mDolby;
    bool mIsResetting;
};

template < int SIZE >
class AudDopplerAudible : public AudGenericAudible_< SIZE, SoundDopplerChannel, SoundDopplerAbsPos > {
public:
    typedef AudGenericAudible_< SIZE, SoundDopplerChannel, SoundDopplerAbsPos > Base;
    AudDopplerAudible(const Vec3f& rPos, const Vec3f* pPos, u32 mask) : Base(rPos, pPos, mask) {
    }
};
