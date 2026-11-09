#pragma once

#include "Game/Sound/SoundEngine.hpp"

// The game's side of one sound's sequence player: the memory it made for it, the commands it sends, and the state its own commands
// set until the audio thread has run them (then the player's published values).
class PlayerLink {
public:
    bool exists() const {
        return mPlayer != nullptr;
    }
    // A player for `tracks` tracks alive at once and `mgrCount` channel groups, each track with `rRoom`.
    void create(u32 tracks, u32 mgrCount, const TrackRoom& rRoom, u32 ownerSound, u8 ownerRoute);
    // The player goes to the audio thread to finish; SoundMemory frees it once the audio thread lets it go.
    void release();

    s32 status() const;
    bool present(const TrackPath& rPath) const;
    u32 mgrCount() const {
        return mMgrCount;
    }

    void init();
    void reserve(u32 tracks);
    void setSeq(const SeqFile* pFile, u32 entryOp);
    void setMgrCount(u32 count);
    // The group's values, sent when they differ from what the audio thread has; null: the group has none.
    void setMgrSound(u32 mgr, const SoundParams* pSound);
    void start();
    void stop();
    void setTempoRate(f32 rate);
    void mute(const TrackPath& rPath, bool mute);
    void pause(bool pause);
    void writePort(const TrackPath& rPath, u32 port, u16 value);
    void setChildIIR(u32 cutoff);
    void setRhythm(const TrackPath& rPath, s32 parser);
    // A named child's values (a sequence's sound child), sent each mix-out; null: none.
    void setTrackSound(const TrackPath& rPath, const SoundParams* pSound);

    static TrackPath root() {
        TrackPath path = {0, 0, 0};
        return path;
    }
    static TrackPath child(u32 child) {
        TrackPath path = {1, static_cast< u8 >(child), 0};
        return path;
    }
    static TrackPath grandchild(u32 child, u32 grandchild) {
        TrackPath path = {2, static_cast< u8 >(child), static_cast< u8 >(grandchild)};
        return path;
    }

private:
    SoundCommand& next(u16 kind);
    u64 send();

    SoundPlayer* mPlayer = nullptr;
    void* mMemory = nullptr;
    u32 mMgrCount = 0;
    s32 mStatus = SeqTrack::STATUS_FREE;
    u64 mStatusSerial = 0;
    u32 mPresent[3] = {0, 0, 0};
    u64 mPresentSerial = 0;
    // What the audio thread was last sent for each group.
    SoundParams* mSent = nullptr;
    bool* mSentHas = nullptr;
};
