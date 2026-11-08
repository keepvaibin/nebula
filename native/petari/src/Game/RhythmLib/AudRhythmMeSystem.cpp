#include "Game/RhythmLib/AudRhythmMeSystem.hpp"
#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    SoundCommand& next(u16 kind, s32 parserNo) {
        SoundCommand& rCommand = SoundEngine::get()->ring().next(kind, nullptr);
        rCommand.mValue = static_cast< u32 >(parserNo);
        return rCommand;
    }

    void send() {
        SoundEngine::get()->ring().send();
    }
}  // namespace

AudRhythmMeSystem::AudRhythmMeSystem(NebulaArena* pArena, u32 numSeqParsers, bool hasMgr)
    : SoundGlobal< AudRhythmMeSystem >(true), mBgmIdx(), mBeatTrack(), mMeMgr() {
    if (numSeqParsers != 0) {
        setUsingRhythmParser(0);
    }

    if (hasMgr) {
        mMeMgr = new (pArena, 0) AudMeMgr(pArena);
    }
}

// The music's root track and its beat track read the music's notes; the beat track is not heard.
bool AudRhythmMeSystem::setSeq(SoundHandle& rHandle, s32 parserNo) {
    if (!rHandle.isSoundAttached()) {
        return false;
    }

    PlayerLink* pPlayer = rHandle->getPlayer();
    if (pPlayer == nullptr || !pPlayer->exists()) {
        return false;
    }

    pPlayer->setRhythm(PlayerLink::root(), parserNo);
    if (!pPlayer->present(PlayerLink::child(0))) {
        return false;
    }

    const TrackPath beat = PlayerLink::grandchild(0, mBeatTrack);
    if (!pPlayer->present(beat)) {
        return false;
    }

    pPlayer->setRhythm(beat, parserNo);
    pPlayer->mute(beat, true);

    next(SOUND_RHYTHM_ACTIVATE, parserNo);
    send();
    return true;
}

void AudRhythmMeSystem::rejectSeq(s32 parserNo) {
    next(SOUND_RHYTHM_INIT, parserNo);
    send();
}

void AudRhythmMeSystem::update() {
    if (mMeMgr != nullptr) {
        mMeMgr->update();
    }
}

void AudRhythmMeSystem::setIdle(bool idle) {
    if (mMeMgr != nullptr) {
        mMeMgr->setPauseFlagAll(idle);
    }
}

void AudRhythmMeSystem::setUsingRhythmParser(u32 parserNo) {
    next(SOUND_RHYTHM_USE, parserNo);
    send();
    mBgmIdx = parserNo;
}

void AudRhythmMeSystem::setTempoStream(s32 parserNo, StreamPlayer* pStream, f32 beatMul, u32 introBeats, u32 loopBeats,
                                       u32 loopStartSamples, u32 loopEndSamples) {
    SoundCommand& rCommand = next(SOUND_TEMPO_INFO, parserNo);
    TempoInfoCommand& rInfo = rCommand.payload< TempoInfoCommand >();
    rInfo.mStream = pStream;
    rInfo.mBeatMul = beatMul;
    rInfo.mIntroBeats = introBeats;
    rInfo.mLoopBeats = loopBeats;
    rInfo.mLoopStartSamples = loopStartSamples;
    rInfo.mLoopEndSamples = loopEndSamples;
    send();
}

void AudRhythmMeSystem::rejectTempoStream(s32 parserNo) {
    next(SOUND_TEMPO_REJECT, parserNo);
    send();
}
