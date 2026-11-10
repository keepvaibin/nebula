#pragma once

#include "Game/Sound/SoundEngine.hpp"

// A streamed piece of music or sound on the audio thread: each channel of the mapped NSTR file is a note looping over its samples
// The game thread owns the record and talks to it only by command; it reads the atomics.
class StreamPlayer {
public:
    // The NSTR format's channels (the importer refuses more; StreamSound's children name them).
    static const u32 cChannels = 6;

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
    static const u32 cTag = 0x4e535452;  // the character constant 'NSTR' (audio_data.rs ast: u32::from_be_bytes(*b"NSTR"))

    // Read by the game thread: the sample playing and the loops played (what the music's beat follows), and how many times
    // its last channel has stopped (StreamSound's "started" callback).
    NebulaAtomic< u32 > mPosition = 0;
    NebulaAtomic< u32 > mLoopCount = 0;
    NebulaAtomic< u32 > mFinishes = 0;
    // The pause its messages have set so far, and that the memory is free.
    NebulaAtomic< bool > mShownPaused = false;
    NebulaAtomic< bool > mReleased = false;
    // Messages taken from the queue (run, or cleared by init): the game's sent count less this bounds the queue.
    NebulaAtomic< u32 > mConsumed = 0;

    // A bigger queue the game makes (SoundMemory) and sends before a message could fill the one in use; the record is followed
    // by mCapacity messages.
    struct MessageRoom {
        NebulaAtomic< bool > mReleased = false;
        u32 mCapacity = 0;
        u32* messages() {
            return reinterpret_cast< u32* >(this + 1);
        }
        static u32 bytes(u32 capacity) {
            return sizeof(MessageRoom) + capacity * sizeof(u32);
        }
    };
    static const u32 cInlineMessages = 16;

    // Audio thread.
    void init();
    void prepare(SoundEngine* pEngine, const u8* pData, u32 ownerSound);
    // Start, stop and pause run in the stream's job, in the order sent.
    void post(u32 message);
    // The queue moves into `pRoom`; the room it leaves is let go.
    void grow(MessageRoom* pRoom);
    void cancel();
    // The game has let it go: nothing reads it after this.
    void release(SoundEngine* pEngine);
    void mix(const StreamMixCommand& rMix);
    enum Message : u32 { MESSAGE_START, MESSAGE_STOP, MESSAGE_PAUSE, MESSAGE_RESUME };
    static void runJob(void* pSelf);
    static void noteEvent(u32 event, SoundNote* pNote, NebulaVoice* pVoice, StreamPlayer* pStream);
    u32 ownerSound() const {
        return mOwnerSound;
    }
    u32 rate() const {
        return mHeader.rate;
    }

private:
    void process();
    void channelStart();
    void channelStop(u16 directRelease);
    void updateChannel(u32 event, SoundNote* pNote, NebulaVoice* pVoice);
    void finish();

    SoundEngine* mEngine = nullptr;
    SoundJob mJob;
    const u8* mData = nullptr;
    Header mHeader = {};
    SoundNote* mChannels[cChannels] = {};
    SoundNote* mUpdateChannel = nullptr;
    bool mPaused = false;
    bool mLoaded = false;
    bool mArmed = false;
    // Posted on the audio thread and read by the next job run: inline, then the last room the game sent.
    u32 mInline[cInlineMessages] = {};
    u32* mMessages = mInline;
    u32 mMessageRoom = cInlineMessages;
    MessageRoom* mRoom = nullptr;
    u32 mMessageHead = 0;
    u32 mMessageCount = 0;
    u32 mOwnerSound = 0;
    f32 mVolume = 1.0f;
    f32 mPitch = 1.0f;
    f32 mChannelVolume[cChannels] = {};
    f32 mChannelPan[cChannels] = {};
    f32 mChannelFxMix[cChannels] = {};
    f32 mChannelDolby[cChannels] = {};
    u16 mMixConfig[6] = {};
};
