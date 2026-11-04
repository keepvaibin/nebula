#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "Game/System/GameAssets.hpp"
#include "Game/System/GameThreads.hpp"

class JASChannel;
class JAISound;

struct JASVoice;

// A streamed piece of music or sound: its whole file is read into memory, then each channel is a voice looping over its samples.
class JASStream {
public:
    static const int CHANNEL_MAX = 6;

    typedef void (*StreamCallback)(u32, JASStream*, void*);

    enum CallbackType {
        /* 0 */ CB_START,
        /* 1 */ CB_STOP,
    };

    // A cooked stream (crates/nebula-import/src/cook/audio_data.rs): this, then each channel's PCM16 samples in turn.
    struct Header {
        u32 tag;
        u32 channels;
        u32 rate;
        u32 samples;
        u32 loop;
        u32 loop_start;
        u32 loop_end;
        u32 volume;
    };

    JASStream();
    ~JASStream();
    void init(StreamCallback, void*);
    bool prepare(s32);
    bool start();
    bool stop(u16);
    bool pause(bool);
    bool cancel();
    void finish();
    void load();
    static s32 channelProcCallback(void*);
    static void channelCallback(u32, JASChannel*, JASVoice*, void*);
    void updateChannel(u32, JASChannel*, JASVoice*);
    s32 channelProc();
    void channelStart();
    void channelStop(u16);

    void setPitch(f32 pitch) {
        mPitch = pitch;
    }

    void setVolume(f32 volume) {
        for (int i = 0; i < CHANNEL_MAX; i++) {
            mChannelVolume[i] = volume;
        }
    }

    void setPan(f32 pan) {
        for (int i = 0; i < CHANNEL_MAX; i++) {
            mChannelPan[i] = pan;
        }
    }

    void setFxmix(f32 fxMix) {
        for (int i = 0; i < CHANNEL_MAX; i++) {
            mChannelFxMix[i] = fxMix;
        }
    }

    void setDolby(f32 dolby) {
        for (int i = 0; i < CHANNEL_MAX; i++) {
            mChannelDolby[i] = dolby;
        }
    }

    void setChannelVolume(int channel, f32 volume) {
        mChannelVolume[channel] = volume;
    }

    void setChannelPan(int channel, f32 pan) {
        mChannelPan[channel] = pan;
    }

    void setChannelFxmix(int channel, f32 fxMix) {
        mChannelFxMix[channel] = fxMix;
    }

    void setChannelDolby(int channel, f32 dolby) {
        mChannelDolby[channel] = dolby;
    }

    bool isPaused() const {
        return mPaused;
    }

    GameMessageQueue mCommands;
    void* mCommandSlots[16];
    JASChannel* mChannels[CHANNEL_MAX];
    JASChannel* mUpdateChannel;
    NebulaAtomic< bool > mLoaded;
    bool mPaused;
    // The sample playing and the loops played: what the music's beat follows.
    NebulaAtomic< u32 > mPosition;
    NebulaAtomic< u32 > mLoopCount;
    GameAssets::File mFile;
    NebulaAtomic< bool > mCanceled;
    const u8* mData;  // the stream in the mapped store
    Header mHeader;
    StreamCallback mCallback;
    void* mCallbackData;
    f32 mVolume;
    f32 mPitch;
    f32 mChannelVolume[CHANNEL_MAX];
    f32 mChannelPan[CHANNEL_MAX];
    f32 mChannelFxMix[CHANNEL_MAX];
    f32 mChannelDolby[CHANNEL_MAX];
    u16 mMixConfig[6];
    // The sound playing this stream, for the host's volume categories (JASVoiceSlot::nameSound).
    JAISound* mNebulaOwner;
};
