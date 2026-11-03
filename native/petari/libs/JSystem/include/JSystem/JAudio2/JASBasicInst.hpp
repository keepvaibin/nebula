#pragma once

#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASGadget.hpp"
#include "JSystem/JAudio2/JASOscillator.hpp"
#include "JSystem/JAudio2/JASSoundParams.hpp"

class NebulaArena;

struct JASInstEffect;

const int OSC_MAX = 2;

struct JASInstParam : public JASSoundParams {
    JASInstParam();  // gets overwritten in OverwriteJAudio

    void initChannelType() {
        mChannelType = JASChannel::CH_WAVE;
    }

    void init_18() {
        _18 = 0;
    }

    void initOscillators() {
        mOscillatorData = nullptr;
        mNumOscillators = 0;
    }

    void init_24() {
        _24 = false;
    }

    void initRelease() {
        mDirectRelease = 0;
    }

    void init() {
        initChannelType();
        init_18();
        initOscillators();
        init_24();
        initRelease();
    }

    u32 mChannelType;
    u32 _18;
    JASOscillator::Data** mOscillatorData;
    u32 mNumOscillators;
    bool _24;
    u16 mDirectRelease;
};

struct JASInst {
    virtual ~JASInst() {};
    virtual bool getParam(int, int, JASInstParam*) const = 0;
    virtual u32 getType() const = 0;
};

struct JASBasicInst : public JASInst {
    struct TVelomap {
        s32 _0;
        s32 _4;
        f32 mVolume;
        f32 mPitch;
    };

    struct TKeymap {
        ~TKeymap();
        TKeymap() {
            mHighKey = -1;
            mVelomapCount = 0;
            mVelomap = nullptr;
        }
        s32 getHighKey() const {
            return mHighKey;
        }
        void setHighKey(int key) {
            mHighKey = key;
        }

        void setVeloRegionCount(u32, NebulaArena*);
        TVelomap* getVeloRegion(int);
        const TVelomap* getVeloRegion(int) const;

        u32 getVeloRegionCount() const {
            return mVelomapCount;
        }

        s32 mHighKey;
        u32 mVelomapCount;
        TVelomap* mVelomap;
    };

    JASBasicInst();
    void setKeyRegionCount(u32, NebulaArena*);
    void appendEffect(JASPtrListAdaptor< JASInstEffect >*);
    void setOsc(int, JASOscillator::Data const*);
    TKeymap* getKeyRegion(int);
    TKeymap* getKeyRegion(int) const;

    virtual ~JASBasicInst();
    virtual bool getParam(int, int, JASInstParam*) const;
    virtual u32 getType() const {
        return 'BSIC';
    };

    void setVolume(f32 volume) {
        mVolume = volume;
    }
    void setPitch(f32 pitch) {
        mPitch = pitch;
    }
    u32 getKeyRegionCount() const {
        return mKeymapCount;
    }

    f32 mVolume;
    f32 mPitch;
    JASOscillator::Data const* mOscillatorData[OSC_MAX];
    JASPtrList< JASInstEffect > mEffects;
    u32 mKeymapCount;
    TKeymap* mKeymap;
};
