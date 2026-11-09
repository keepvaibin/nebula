#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/StreamPlayer.hpp"
#include "Game/System/NebulaList.hpp"

class StreamManager;

// A streamed sound (music, voices, cutscenes): a StreamPlayer the audio thread runs, made with the sound and given back when it
// goes. Children name its channels.
class StreamSound final : public Sound, public NebulaListLink< StreamSound > {
public:
    explicit StreamSound(StreamManager* streamMgr);
    virtual ~StreamSound();
    void startID_(SoundID id, s32 streamFileEntry, const Vec3f* posPtr, SoundAudience* audience, int category);
    bool prepare_prepareStream_();
    void prepare_();
    void prepare_startStream_();
    void managerMixOut_(const SoundParams& inParams, SoundActivity activity);
    void dieStream_();
    bool tryDie_();
    void managerCalc_();
    s32 getNumChild() const;
    SoundChild* getChild(int index);
    void releaseChild(int index);
    PlayerLink* getPlayer();
    PlayerLink* getChildTrack(int index, TrackPath* pPath);
    StreamSound* asStream();
    SoundTempo* getTempoMgr();

    void stopStream(bool stop) {
        mLoaded = stop;
    }

    // The pause the stream's messages have set (init clears it at once).
    bool isPaused() const;
    // The audio thread's player, for commands that name it.
    StreamPlayer* player() const {
        return mStream;
    }

    static const int NUM_CHILDREN = StreamPlayer::cChannels;

    int mPrepareState;
    s32 mStreamFileEntry;
    int mCategory;
    SoundChild* children[NUM_CHILDREN];
    StreamManager* streamMgr;
    bool mIsPaused;

private:
    SoundCommand& next(u16 kind);
    // A start, stop or pause: a bigger room goes first when the messages not yet taken could fill the stream's.
    SoundCommand& nextMessage(u16 kind);
    u64 send();
    void sendChannel(u32 channel, const SoundParams& rValues, f32 pitch);
    void initStream();
    bool prepareStream();
    bool finished() const;

    StreamPlayer* mStream;
    // The room the stream's messages have (the last one sent; null: inline) and the messages sent.
    StreamPlayer::MessageRoom* mRoom;
    u32 mMessageRoom;
    u32 mPosted;
    // The stream's file is mapped: it is loaded the moment it is prepared.
    bool mLoaded;
    // Finished: its last channel stopped (a count the audio thread raises) or it was cancelled.
    u32 mFinishBase;
    bool mCanceled;
    u64 mInitSerial;
    // Each channel's values as last sent (none since init: unknown).
    bool mSentKnown[NUM_CHILDREN];
    StreamMixCommand mSent[NUM_CHILDREN];
};
