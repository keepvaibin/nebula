#pragma once

#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASLfo.hpp"
#include "JSystem/JAudio2/JASOscillator.hpp"
#include "JSystem/JAudio2/JASSoundParams.hpp"
#include "JSystem/JAudio2/JASWaveInfo.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

struct JASVoiceSlot;

struct JASVoice;

class JASChannelParams {
public:
    JASChannelParams() {
        mVolume = 1.0f;
        mPitch = 1.0f;
        _8 = 0.0f;
        mPan = 0.5f;
        mFxMix = 0.0f;
        mDolby = 0.0f;
    }

    void init() {
        mVolume = 1.0f;
        mPitch = 1.0f;
        _8 = 0.0f;
        mPan = 0.5f;
        mFxMix = 0.0f;
        mDolby = 0.0f;
    }

    f32 mVolume;
    f32 mPitch;
    f32 _8;
    f32 mPan;
    f32 mFxMix;
    f32 mDolby;
};

class JASChannel : public JASPoolAllocObject_MultiThreaded< JASChannel > {
public:
    typedef void (*Callback)(u32, JASChannel*, JASVoice*, void*);
    static const int BUSOUT_CPUCH = 6;
    static const int OSC_NUM = 2;

    enum CallbackType {
        /* 0 */ CB_PLAY,
        /* 1 */ CB_START,
        /* 2 */ CB_STOP,
        /* 3 */ CB_TIMER,
    };

    enum Status {
        /* 0 */ STATUS_STOP,
        /* 1 */ STATUS_PLAY,
        /* 2 */ STATUS_RELEASE,
    };

    enum ChannelType {
        /* 0 */ CH_WAVE = 0,
        /* 2 */ CH_OSC = 2,
    };

    struct PanVector {
        f32 mSound;
        f32 mEffect;
        f32 mChannel;
    };

    union MixConfig {
        u16 whole;
        struct {
            // whole is set as one u16 (0x150: bus 1, mode 5): fields from the least significant bit up.
            u8 lower1 : 4;
            u8 lower0 : 4;
            u8 upper : 8;
        };
    };

    JASChannel(Callback, void*);
    ~JASChannel();
    int play();
    int playForce();
    void release(u16);
    void setOscInit(u32, JASOscillator::Data const*);
    void setMixConfig(u32, u16);
    static f32 calcEffect(JASChannel::PanVector const*);
    static f32 calcPan(JASChannel::PanVector const*);
    void effectOsc(u32, JASOscillator::EffectParams*);
    void setKeySweepTarget(s32, u32);
    void updateEffectorParam(JASVoice*, f32*, JASOscillator::EffectParams const&);
    static s32 voiceUpdateCallback(u32, JASVoice*, void*);
    s32 initialUpdateVoice(JASVoice*);
    s32 updateVoice(JASVoice*);
    void updateAutoMixer(JASVoice*, f32, f32, f32, f32);
    void updateMixer(f32, f32, f32, f32, f32*);
    void free();
    static void initBankDisposeMsgQueue();
    static void receiveBankDisposeMsg();
    bool checkBankDispose() const;

    void setPauseFlag(bool flag) {
        mPauseFlag = flag;
    }

    void setUpdateTimer(u32 timer) {
        mUpdateTimer = timer;
    }

    void setBankDisposeID(const void* id) {
        mBankDisposeID = id;
    }

    void setDirectRelease(u16 release) {
        mOscillators[0].setDirectRelease(release);
    }

    void setVibrate(f32 depth, f32 pitch) {
        mVibrate.setDepth(depth);
        mVibrate.setPitch(pitch);
    }

    void setVibrateDelay(u16 delay) {
        mVibrate.setDelay(delay);
    }

    void setTremolo(f32 depth, f32 pitch) {
        mTremolo.setDepth(depth);
        mTremolo.setPitch(pitch);
    }

    void setTremoloDelay(u16 delay) {
        mTremolo.setDelay(delay);
    }

    void setPriority(u16 prio) {
        mPriority = prio;
    }

    void setParams(const JASChannelParams& params) {
        mParams = params;
    }

    void setInitVolume(f32 volume) {
        mSoundParams.mVolume = volume;
    }

    void setInitFxmix(f32 fxMix) {
        mSoundParams.mFxMix = fxMix;
    }

    void setInitPitch(f32 pitch) {
        mSoundParams.mPitch = pitch;
    }

    void setInitPan(f32 pan) {
        mSoundParams.mPan = pan;
    }

    void setInitDolby(f32 dolby) {
        mSoundParams.mDolby = dolby;
    }

    void setKey(s32 key) {
        mKey = key;
    }

    void setVelocity(u32 velocity) {
        mVelocity = velocity;
    }

    void setSkipSamples(u32 skipSamples) {
        mSkipSamples = skipSamples;
    }

    bool isDolbyMode() const {
        return mMixConfig[0].whole == 0xffff;
    }

    int mStatus;
    bool mPauseFlag;
    JASVoiceSlot* mSlot;
    Callback mCallback;
    void* mCallbackData;
    u32 mUpdateTimer;
    const void* mBankDisposeID;
    JASOscillator mOscillators[2];
    JASLfo mVibrate;
    JASLfo mTremolo;
    MixConfig mMixConfig[BUSOUT_CPUCH];
    u16 mPriority;
    JASChannelParams mParams;
    JASSoundParams mSoundParams;
    s16 mKey;
    u16 mVelocity;
    f32 mKeySweep;
    f32 mKeySweepTarget;
    u32 mKeySweepCount;
    u32 mSkipSamples;
    u32 mChannelType;
    JASWaveInfo mWaveInfo;
    union {
        const u8* mWavePtr;
        int mProgNo;
    };
    // The end of a wave outside the sound memory (a stream's).
    const u8* mWaveLimit;

    static GameMessageQueue sBankDisposeMsgQ;
    static void* sBankDisposeMsg[16];
    static void* sBankDisposeList[16];
    static int sBankDisposeListSize;
};
