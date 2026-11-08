#pragma once

#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/Sound/SoundGlobal.hpp"

class SoundHandle;
class NebulaArena;
class AudMeMgr;
class StreamPlayer;

// The game's side of the music logic: which beat reader each music uses, sent to the audio thread's RhythmSystem, and the Me
// phrases.
class AudRhythmMeSystem : public SoundGlobal< AudRhythmMeSystem > {
public:
    AudRhythmMeSystem(NebulaArena*, u32, bool);

    bool setSeq(SoundHandle&, s32);
    void rejectSeq(s32);
    void update();
    void setIdle(bool);
    void setUsingRhythmParser(u32);
    // The beat reader `parserNo` follows the stream's clock.
    void setTempoStream(s32 parserNo, StreamPlayer* pStream, f32 beatMul, u32 introBeats, u32 loopBeats, u32 loopStartSamples,
                        u32 loopEndSamples);
    void rejectTempoStream(s32 parserNo);

    s32 getBgmIdx() const {
        return mBgmIdx;
    }
    s32 mBgmIdx;
    // The music track whose child's notes the beat reader hears.
    u32 mBeatTrack;
    AudMeMgr* mMeMgr;
    AudChordInfo mChordInfo;
};
