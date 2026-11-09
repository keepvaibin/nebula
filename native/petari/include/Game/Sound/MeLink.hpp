#pragma once

#include "Game/Sound/MeTrack.hpp"
#include "Game/Sound/SoundCommand.hpp"

// The game's side of one Me phrase's player: its memory, its commands, and the status its own commands set until the audio
// thread has run them.
class MeLink {
public:
    bool exists() const {
        return mPlayer != nullptr;
    }
    // A player for `tracks` tracks, started at `start` of the program `pData` with these values.
    void start(u32 tracks, u32 stack, const u8* pData, u32 start, const SoundParams& rSound);
    // The values each frame, with the phrase's frame work after them.
    void frame(const SoundParams& rSound);
    void stopAtNoteEnd();
    void stop();
    void pause(bool pause);
    // The player goes to the audio thread to finish; SoundMemory frees it once the audio thread lets it go.
    void release();

    s32 status() const;
    // The program place it started at (the track's Me id).
    u32 startPlace() const {
        return mStart;
    }

private:
    SoundCommand& next(u16 kind);
    u64 send();

    MePlayer* mPlayer = nullptr;
    void* mMemory = nullptr;
    u32 mStart = 0;
    s32 mStatus = MeTrack::STATUS_FREE;
    u64 mStatusSerial = 0;
};
