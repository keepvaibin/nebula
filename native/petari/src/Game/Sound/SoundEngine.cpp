#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/MeTrack.hpp"
#include "Game/Sound/SoundMemory.hpp"
#include "Game/Sound/StreamPlayer.hpp"
#include "Game/System/LumaTime.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstring>
#include <new>

extern "C" const NebulaHostServices* nebula_host(void);

SoundEngine* SoundEngine::sEngine;

namespace {
    // The buses an effect line's two sends name (mBusA and mBusB).
    const u16 cEffectDestinations[12] = {
        NEBULA_AUDIO_BUS_LEFT,           NEBULA_AUDIO_BUS_RIGHT,         NEBULA_AUDIO_BUS_EFFECT0,
        NEBULA_AUDIO_BUS_EFFECT1,        NEBULA_AUDIO_BUS_EFFECT2,       NEBULA_AUDIO_BUS_EFFECT3,
        NEBULA_AUDIO_BUS_SURROUND_RIGHT, NEBULA_AUDIO_BUS_SURROUND_LEFT, NEBULA_AUDIO_BUS_RIGHT_INVERTED,
        NEBULA_AUDIO_BUS_DISCARD,        NEBULA_AUDIO_BUS_CENTER,        NEBULA_AUDIO_BUS_NONE,
    };

    void* zeroed(NebulaArena* pArena, u32 bytes, int alignment) {
        void* memory = NebulaArena::alloc(bytes, alignment, pArena);
        if (memory == nullptr) {
            NEBULA_PANIC("no memory for the sound engine (%u bytes)", bytes);
        }
        std::memset(memory, 0, bytes);
        return memory;
    }

    u32 align16(u32 bytes) {
        return (bytes + 15) & ~15u;
    }
}  // namespace

u32 SoundPlayer::bytes(u32 tracks, u32 mgrCount, const TrackRoom& rRoom, u32 named) {
    return align16(sizeof(SoundPlayer)) + align16(mgrCount * sizeof(SoundParams)) + align16(mgrCount * sizeof(bool)) +
           align16((rootWords(named) + named) * sizeof(NebulaAtomic< u32 >)) + align16(named * 16 * sizeof(SoundParams)) +
           tracks * TrackArena::recordBytes(mgrCount, rRoom);
}

SoundPlayer* SoundPlayer::create(void* pMemory, u32 tracks, u32 mgrCount, const TrackRoom& rRoom, u32 named, u32 ownerSound,
                                 u8 ownerRoute) {
    u8* cursor = static_cast< u8* >(pMemory);
    SoundPlayer* player = new (cursor) SoundPlayer();
    cursor += align16(sizeof(SoundPlayer));
    player->mMgrSound = reinterpret_cast< SoundParams* >(cursor);
    for (u32 i = 0; i < mgrCount; i++) {
        new (&player->mMgrSound[i]) SoundParams();
    }
    cursor += align16(mgrCount * sizeof(SoundParams));
    player->mHasSound = reinterpret_cast< bool* >(cursor);
    for (u32 i = 0; i < mgrCount; i++) {
        player->mHasSound[i] = false;
    }
    cursor += align16(mgrCount * sizeof(bool));
    player->mMgrCount = mgrCount;
    player->mRootWords = rootWords(named);
    player->mNamed = named;
    player->mPresent = reinterpret_cast< NebulaAtomic< u32 >* >(cursor);
    for (u32 i = 0; i < player->mRootWords + named; i++) {
        new (&player->mPresent[i]) NebulaAtomic< u32 >(0);
    }
    cursor += align16((player->mRootWords + named) * sizeof(NebulaAtomic< u32 >));
    player->mTrackSound = reinterpret_cast< SoundParams* >(cursor);
    for (u32 i = 0; i < named * 16; i++) {
        new (&player->mTrackSound[i]) SoundParams();
    }
    cursor += align16(named * 16 * sizeof(SoundParams));
    player->mArena.setup(cursor, tracks, mgrCount, rRoom);
    player->mArena.mHolder.mKind = OWNER_TRACK;
    player->mArena.mHolder.mPlayer = player;
    player->mRoot = player->mArena.alloc(SoundEngine::get());
    player->mRoot->mOwnerSound = ownerSound;
    player->mRoot->mOwnerRoute = ownerRoute;
    return player;
}

void SoundEngine::setupSpeaker(u32 pads) {
    SoundCommand& rCommand = mRing.next(SOUND_SPEAKER_SETUP, nullptr);
    SpeakerSetupCommand& rSetup = rCommand.payload< SpeakerSetupCommand >();
    rSetup.mMemory = SoundMemory::alloc(SpeakerMixer::bytes(pads));
    rSetup.mPads = pads;
    mRing.send();
}

SoundEngine* SoundEngine::create(NebulaArena* pArena) {
    SoundEngine* engine = new (zeroed(pArena, sizeof(SoundEngine), 64)) SoundEngine();
    sEngine = engine;
    engine->mRing.setup(pArena);
    engine->mHostVoices = static_cast< NebulaVoice* >(zeroed(pArena, sizeof(NebulaVoice) * cVoices, 32));
    engine->mEffects = static_cast< NebulaEffectLine* >(zeroed(pArena, sizeof(NebulaEffectLine) * 4, 32));
    for (u8 i = 0; i < 4; i++) {
        EffectLineCommand off = {};
        off.mLine = i;
        engine->setEffectLine(off);
    }
    Voices::Slot* slots = static_cast< Voices::Slot* >(zeroed(pArena, sizeof(Voices::Slot) * cVoices, 16));
    SoundNote* notes = static_cast< SoundNote* >(zeroed(pArena, sizeof(SoundNote) * cVoices, 16));
    for (u32 i = 0; i < cVoices; i++) {
        new (&slots[i]) Voices::Slot();
        new (&notes[i]) SoundNote();
    }
    engine->mVoices.setup(engine, engine->mHostVoices, cVoices, slots, notes);
    engine->mTracksJob.mKind = SoundJob::JOB_TRACKS;
    const NebulaHostServices* const host = nebula_host();
    host->voices_setup(host->user, engine->mHostVoices, cVoices, engine->mEffects);
    return engine;
}

void SoundEngine::setupBank(const BankFile* pFile, u32 size, NebulaArena* pArena) {
    OscData* oscs = static_cast< OscData* >(zeroed(pArena, sizeof(OscData) * (pFile->mOscCount + 1), 16));
    SoundBank bank;
    if (!bank.setup(pFile, size, oscs)) {
        NEBULA_PANIC("a sound bank not cooked by this version of the importer");
    }
    const u32 number = bank.number();
    if (number >= cBankNumbers) {
        NEBULA_PANIC("sound bank number %u out of range", number);
    }
    mBanks[number] = bank;
    mHasBank[number] = true;
}

void SoundEngine::setupWaveBanks(u32 count, NebulaArena* pArena) {
    mWaveBanks = static_cast< WaveBank* >(zeroed(pArena, sizeof(WaveBank) * (count + 1), 16));
    for (u32 i = 0; i < count; i++) {
        new (&mWaveBanks[i]) WaveBank();
    }
    mWaveBankCount = count;
}

void SoundEngine::setupWaveBank(u32 index, const WaveBankFile* pFile, u32 size, NebulaArena* pArena) {
    const u32 ids = pFile->mIdCount;
    const u32 groups = pFile->mGroupCount;
    s32* current = static_cast< s32* >(zeroed(pArena, sizeof(s32) * (ids + 1), 16));
    u32* live = static_cast< u32* >(zeroed(pArena, sizeof(u32) * (groups + 1), 16));
    const u8** data = static_cast< const u8** >(zeroed(pArena, sizeof(u8*) * (groups + 1), 16));
    u32* dataBytes = static_cast< u32* >(zeroed(pArena, sizeof(u32) * (groups + 1), 16));
    if (index >= mWaveBankCount || !mWaveBanks[index].setup(pFile, size, current, live, data, dataBytes)) {
        NEBULA_PANIC("a wave bank not cooked by this version of the importer");
    }
}

void SoundEngine::startClock() {
    const NebulaHostServices* const host = nebula_host();
    host->sound_clock(host->user, NEBULA_AUDIO_START, cTicksPerSecond);
}

// The blocks the clock asked for (at most cMaxCatchUp; the rest are dropped), each to the output.
void SoundEngine::frame() {
    __atomic_fetch_add(&mFramesAsked, 1u, __ATOMIC_RELAXED);
    u32 frames = __atomic_exchange_n(&mFramesAsked, 0u, __ATOMIC_RELAXED);
    if (frames > cMaxCatchUp) {
        frames = cMaxCatchUp;
    }
    for (u32 i = 0; i < frames; i++) {
        tick();
    }
}

void SoundEngine::tick() {
    // The game's writes since the last tick, in its order: between two ticks they are what it set.
    for (const SoundCommand* command = mRing.front(); command != nullptr; command = mRing.front()) {
        apply(*command);
        mRing.done();
    }
    if (mResetting) {
        resetStep();
    }
    // The world clock's tape (services/time.md 7): at a rate other than real time the logic (the voices' updates, the
    // tracks, the streams) runs rate/1000 times a tick, rounded down with the rest carried, and every voice plays
    // rate/1000 as fast; the output stays one tick, so music, sequences, effects and streams change speed and pitch
    // together. 0 is the world's sound held, which AudSystem does as the pause menu does: the engine runs at real time.
    const u32 rate = luma::time::soundRate();
    if (rate == 1000u || rate == 0u) {
        if (mTapeRate != 1000u) {
            mTapeRate = 1000u;
            mTapeAcc = 0;
            Voices::setRateScale(1.0f);
        }
        mVoices.updateAll();
        runJobs(true);
    } else {
        if (rate != mTapeRate) {
            mTapeRate = rate;
            Voices::setRateScale(static_cast< f32 >(rate) / 1000.0f);
        }
        mTapeAcc += rate;
        const u32 logicTicks = mTapeAcc / 1000u;
        mTapeAcc -= logicTicks * 1000u;
        for (u32 i = 0; i < logicTicks; i++) {
            mVoices.updateSlots();
            runJobs(false);
        }
        mVoices.mix();
        // The remote's speaker mixes one output tick of its own samples (6 kHz to the remote), at its own pace.
        if (mSpeakerJob.mListed) {
            mSpeakerJob.mCall(mSpeakerJob.mData);
        }
    }
    const NebulaHostServices* const host = nebula_host();
    host->voices_frame(host->user);
}

void SoundEngine::runJobs(bool speaker) {
    // The jobs by place, each found again after the last ran: one may end another (a stream's last note dropped).
    u32 next = 0;
    for (;;) {
        SoundJob* job = mFirstJob;
        while (job != nullptr && job->mIndex < next) {
            job = job->mNext;
        }
        if (job == nullptr) {
            break;
        }
        next = job->mIndex + 1;
        switch (job->mKind) {
        case SoundJob::JOB_TRACKS:
            runTracks();
            break;
        case SoundJob::JOB_STREAM:
            job->mCall(job->mData);
            break;
        case SoundJob::JOB_CALL:
            if (speaker) {
                job->mCall(job->mData);
            }
            break;
        }
    }
}

// The first free place.
void SoundEngine::addJob(SoundJob* pJob) {
    if (pJob->mListed) {
        return;
    }
    u32 index = 0;
    SoundJob** link = &mFirstJob;
    while (*link != nullptr && (*link)->mIndex == index) {
        index++;
        link = &(*link)->mNext;
    }
    pJob->mIndex = index;
    pJob->mNext = *link;
    *link = pJob;
    pJob->mListed = true;
}

void SoundEngine::removeJob(SoundJob* pJob) {
    for (SoundJob** link = &mFirstJob; *link != nullptr; link = &(*link)->mNext) {
        if (*link == pJob) {
            *link = pJob->mNext;
            pJob->mNext = nullptr;
            pJob->mListed = false;
            return;
        }
    }
}

// Each playing root in start order; one that ends leaves the list.
void SoundEngine::runTracks() {
    SoundPlayer* previous = nullptr;
    for (SoundPlayer* player = mFirstRoot; player != nullptr;) {
        SoundPlayer* const next = player->mNext;
        if (player->mRoot->seqMain() < 0) {
            if (previous != nullptr) {
                previous->mNext = next;
            } else {
                mFirstRoot = next;
            }
            if (mLastRoot == player) {
                mLastRoot = previous;
            }
            player->mNext = nullptr;
            player->mListed = false;
            publish(player);
            tryRelease(player);
        } else {
            publish(player);
            previous = player;
        }
        player = next;
    }
}

void SoundEngine::publish(SoundPlayer* pPlayer) {
    SeqTrack* const root = pPlayer->mRoot;
    pPlayer->mStatus = root->mStatus;
    // Children are kept in index order: each word is stored once its bits are in.
    const u32 rootWords = pPlayer->mRootWords;
    u32 word = 0;
    u32 bits = 0;
    for (SeqTrack* child = root->firstChild(); child != nullptr; child = child->nextSibling()) {
        const u32 at = child->index() / 32;
        if (at >= rootWords) {
            break;
        }
        for (; word < at; word++) {
            pPlayer->mPresent[word] = bits;
            bits = 0;
        }
        bits |= 1u << (child->index() & 31);
    }
    for (; word < rootWords; word++) {
        pPlayer->mPresent[word] = bits;
        bits = 0;
    }
    u32 named = 0;
    for (SeqTrack* child = root->firstChild(); child != nullptr && child->index() < pPlayer->mNamed; child = child->nextSibling()) {
        for (; named < child->index(); named++) {
            pPlayer->mPresent[rootWords + named] = 0;
        }
        bits = 0;
        for (SeqTrack* grandchild = child->firstChild(); grandchild != nullptr; grandchild = grandchild->nextSibling()) {
            if (grandchild->index() < 32) {
                bits |= 1u << grandchild->index();
            }
        }
        pPlayer->mPresent[rootWords + named] = bits;
        named++;
    }
    for (; named < pPlayer->mNamed; named++) {
        pPlayer->mPresent[rootWords + named] = 0;
    }
}

// The player's memory goes back once the game has let it go, it plays no more, and no note reads it.
void SoundEngine::tryRelease(SoundPlayer* pPlayer) {
    if (!pPlayer->mReleaseAsked || pPlayer->mListed || pPlayer->mArena.mHolder.mHeld != 0 || pPlayer->mReleased) {
        return;
    }
    mVoices.forgetOwners(pPlayer->mArena.begin(), pPlayer->mArena.end());
    pPlayer->mReleased = true;
}

void SoundEngine::releaseHolder(NoteHolder* pHolder) {
    pHolder->mHeld--;
    if (pHolder->mHeld != 0) {
        return;
    }
    switch (pHolder->mKind) {
    case OWNER_TRACK:
        tryRelease(static_cast< SoundPlayer* >(pHolder->mPlayer));
        break;
    case OWNER_ME:
        mRhythm.tryRelease(static_cast< MePlayer* >(pHolder->mPlayer));
        break;
    default:
        break;
    }
}

SeqTrack* SoundEngine::track(SoundPlayer* pPlayer, const TrackPath& rPath) {
    SeqTrack* track = pPlayer->mRoot;
    if (rPath.mDepth >= 1) {
        track = track->child(rPath.mChild);
    }
    if (rPath.mDepth >= 2 && track != nullptr) {
        track = track->child(rPath.mGrandchild);
    }
    return track;
}

void SoundEngine::apply(const SoundCommand& rCommand) {
    switch (rCommand.mKind) {
    case SOUND_OUTPUT_LEVEL:
        setOutputLevel(rCommand.payload< f32 >());
        break;
    case SOUND_OUTPUT_MODE:
        mOutputMode = rCommand.mValue;
        break;
    case SOUND_EFFECT_LINE:
        setEffectLine(rCommand.payload< EffectLineCommand >());
        break;
    case SOUND_RESET_INIT:
        mResetLevel = mOutputLevel;
        break;
    case SOUND_RESET_START: {
        const ResetCommand& rReset = rCommand.payload< ResetCommand >();
        mResetStopClock = rReset.mStopClock;
        mResetLevel = mOutputLevel;
        mResetTime = rReset.mTime;
        mResetting = true;
        mResetDone = false;
        break;
    }
    case SOUND_RESET_RESUME:
        setOutputLevel(mResetLevel);
        break;
    case SOUND_WAVE_GROUP: {
        const WaveGroupCommand& rGroup = rCommand.payload< WaveGroupCommand >();
        if (rGroup.mWaveBank < mWaveBankCount) {
            if (rGroup.mLive) {
                mWaveBanks[rGroup.mWaveBank].activate(rGroup.mGroup, rGroup.mData, rGroup.mBytes);
            } else {
                mWaveBanks[rGroup.mWaveBank].deactivate(rGroup.mGroup);
            }
        }
        break;
    }
    case SOUND_STREAM_INIT:
        static_cast< StreamPlayer* >(rCommand.mTarget)->init();
        break;
    case SOUND_STREAM_PREPARE:
        static_cast< StreamPlayer* >(rCommand.mTarget)->prepare(this, rCommand.payload< const u8* >(), rCommand.mValue);
        break;
    case SOUND_STREAM_START:
        static_cast< StreamPlayer* >(rCommand.mTarget)->post(StreamPlayer::MESSAGE_START);
        break;
    case SOUND_STREAM_STOP:
        static_cast< StreamPlayer* >(rCommand.mTarget)->post(static_cast< u32 >(rCommand.mArg) << 16 | StreamPlayer::MESSAGE_STOP);
        break;
    case SOUND_STREAM_PAUSE:
        static_cast< StreamPlayer* >(rCommand.mTarget)->post(rCommand.mArg != 0 ? StreamPlayer::MESSAGE_PAUSE : StreamPlayer::MESSAGE_RESUME);
        break;
    case SOUND_STREAM_MIX:
        static_cast< StreamPlayer* >(rCommand.mTarget)->mix(rCommand.payload< StreamMixCommand >());
        break;
    case SOUND_STREAM_CANCEL:
        static_cast< StreamPlayer* >(rCommand.mTarget)->cancel();
        break;
    case SOUND_STREAM_RELEASE:
        mRhythm.forgetStream(static_cast< StreamPlayer* >(rCommand.mTarget));
        static_cast< StreamPlayer* >(rCommand.mTarget)->release(this);
        break;
    case SOUND_STREAM_ROOM:
        static_cast< StreamPlayer* >(rCommand.mTarget)->grow(rCommand.payload< StreamPlayer::MessageRoom* >());
        break;
    case SOUND_ME_START:
    case SOUND_ME_PARAMS:
    case SOUND_ME_STOP_AT_NOTE_END:
    case SOUND_ME_STOP:
    case SOUND_ME_PAUSE:
    case SOUND_ME_RELEASE:
    case SOUND_RHYTHM_INIT:
    case SOUND_RHYTHM_ACTIVATE:
    case SOUND_RHYTHM_USE:
    case SOUND_TEMPO_INFO:
    case SOUND_TEMPO_REJECT:
    case SOUND_CHORD_LOAD:
    case SOUND_CHORD_INVALIDATE:
        mRhythm.apply(rCommand);
        break;
    case SOUND_SPEAKER_SETUP: {
        const SpeakerSetupCommand& rSetup = rCommand.payload< SpeakerSetupCommand >();
        mSpeaker.setup(rSetup.mMemory, rSetup.mPads);
        mSpeakerJob.mKind = SoundJob::JOB_CALL;
        mSpeakerJob.mCall = SpeakerMixer::job;
        mSpeakerJob.mData = &mSpeaker;
        addJob(&mSpeakerJob);
        break;
    }
    case SOUND_SPEAKER_PAD:
    case SOUND_SPEAKER_START:
    case SOUND_SPEAKER_STATE:
    case SOUND_SPEAKER_STOP:
    case SOUND_SPEAKER_RELEASE:
        mSpeaker.apply(rCommand);
        break;
    default:
        applyPlayer(static_cast< SoundPlayer* >(rCommand.mTarget), rCommand);
        publish(static_cast< SoundPlayer* >(rCommand.mTarget));
        break;
    }
}

void SoundEngine::applyPlayer(SoundPlayer* pPlayer, const SoundCommand& rCommand) {
    SeqTrack* const root = pPlayer->mRoot;
    switch (rCommand.mKind) {
    case SOUND_PLAYER_INIT:
        root->init();
        break;
    case SOUND_PLAYER_RESERVE: {
        // SequenceSound::reserveChildTracks_: the named children, and under them the sequence's tracks by number (16 to a child).
        const u32 tracks = rCommand.mValue;
        for (u32 i = 0; i < pPlayer->mNamed; i++) {
            SeqTrack* child = root->reserveChild(i);
            if (child == nullptr) {
                continue;
            }
            for (u32 j = 0; j < 16; j++) {
                if (i * 16 + j < tracks) {
                    child->reserveChild(j);
                }
            }
        }
        break;
    }
    case SOUND_PLAYER_SEQ: {
        const SeqCommand& rSeq = rCommand.payload< SeqCommand >();
        root->setSeqData(rSeq.mFile, rSeq.mEntryOp);
        break;
    }
    case SOUND_PLAYER_MGRS:
        root->setChannelMgrCount(rCommand.mValue);
        break;
    case SOUND_PLAYER_SOUND: {
        const u32 mgr = rCommand.mArg;
        if (mgr >= pPlayer->mMgrCount) {
            break;
        }
        const MgrSoundCommand& rSound = rCommand.payload< MgrSoundCommand >();
        pPlayer->mHasSound[mgr] = rSound.mHas;
        pPlayer->mMgrSound[mgr] = rSound.mSound;
        root->setMgrSound(mgr, rSound.mHas ? &pPlayer->mMgrSound[mgr] : nullptr);
        break;
    }
    case SOUND_PLAYER_START:
        // The sequencers' job takes its place with the first root.
        addJob(&mTracksJob);
        if (!pPlayer->mListed) {
            pPlayer->mNext = nullptr;
            if (mLastRoot != nullptr) {
                mLastRoot->mNext = pPlayer;
            } else {
                mFirstRoot = pPlayer;
            }
            mLastRoot = pPlayer;
            pPlayer->mListed = true;
        }
        root->mStatus = SeqTrack::STATUS_RUN;
        break;
    case SOUND_PLAYER_STOP:
        root->mIsStopped = true;
        break;
    case SOUND_PLAYER_TEMPO:
        root->setTempoRate(rCommand.payload< f32 >());
        break;
    case SOUND_PLAYER_MUTE: {
        const TrackCommand& rTrack = rCommand.payload< TrackCommand >();
        SeqTrack* target = track(pPlayer, rTrack.mPath);
        if (target != nullptr) {
            target->mute(rTrack.mValue != 0);
        }
        break;
    }
    case SOUND_PLAYER_PAUSE:
        root->pause(rCommand.mArg != 0);
        break;
    case SOUND_PLAYER_PORT: {
        const TrackCommand& rTrack = rCommand.payload< TrackCommand >();
        SeqTrack* target = track(pPlayer, rTrack.mPath);
        if (target != nullptr) {
            target->writePort(rTrack.mPort, static_cast< u16 >(rTrack.mValue));
        }
        break;
    }
    case SOUND_PLAYER_CHILD_IIR:
        if (rCommand.mValue < 129) {
            for (SeqTrack* child = root->firstChild(); child != nullptr; child = child->nextSibling()) {
                child->setIIR(cIIRCutoff[rCommand.mValue]);
            }
        }
        break;
    case SOUND_PLAYER_RHYTHM: {
        const TrackCommand& rTrack = rCommand.payload< TrackCommand >();
        SeqTrack* target = track(pPlayer, rTrack.mPath);
        if (target != nullptr) {
            target->setRhythmParser(rTrack.mValue >= 0 ? mRhythm.parser(static_cast< u32 >(rTrack.mValue)) : nullptr);
        }
        break;
    }
    case SOUND_PLAYER_TRACK_SOUND: {
        const TrackSoundCommand& rTrack = rCommand.payload< TrackSoundCommand >();
        if (rTrack.mPath.mDepth != 2 || rTrack.mPath.mChild >= pPlayer->mNamed || rTrack.mPath.mGrandchild >= 16) {
            break;
        }
        SeqTrack* target = track(pPlayer, rTrack.mPath);
        if (target != nullptr) {
            SoundParams& rSound = pPlayer->mTrackSound[rTrack.mPath.mChild * 16 + rTrack.mPath.mGrandchild];
            rSound = rTrack.mSound;
            target->setMgrSound(0, rTrack.mHas ? &rSound : nullptr);
        }
        break;
    }
    case SOUND_PLAYER_RELEASE:
        pPlayer->mReleaseAsked = true;
        if (pPlayer->mListed) {
            root->mIsStopped = true;
        }
        tryRelease(pPlayer);
        break;
    }
}

void SoundEngine::setOutputLevel(f32 level) {
    mOutputLevel = level;
    mMixerGain = 4.0f * level;
}

// Settings change, the line's delay kept; with settings and a restart the line starts over, silent. Without settings the line
// is off.
void SoundEngine::setEffectLine(const EffectLineCommand& rLine) {
    NebulaEffectLine& rEffect = mEffects[rLine.mLine];
    if (rLine.mHasConfig) {
        rEffect.volume_a = rLine.mVolumeA / 32768.0f;
        rEffect.bus_a = cEffectDestinations[rLine.mBusA];
        rEffect.volume_b = rLine.mVolumeB / 32768.0f;
        rEffect.bus_b = cEffectDestinations[rLine.mBusB];
        rEffect.subframes = static_cast< u16 >(rLine.mSubframes);
        for (int i = 0; i < 8; i++) {
            rEffect.fir[i] = rLine.mFir[i] / 32768.0f;
        }
    }
    if (rLine.mRestart && rLine.mHasConfig) {
        mEffectStarted[rLine.mLine] = true;
        rEffect.generation++;
    } else if (!rLine.mHasConfig) {
        mEffectStarted[rLine.mLine] = rLine.mRestart;
    }
    rEffect.filter = mEffectStarted[rLine.mLine] && rLine.mHasConfig ? rLine.mFilter : 0;
}

// The reset: the level down to nothing over the ticks asked, then every playing note dropped.
void SoundEngine::resetStep() {
    if (mResetTime == 0) {
        mVoices.dropActive();
        if (mResetStopClock) {
            const NebulaHostServices* const host = nebula_host();
            host->sound_clock(host->user, NEBULA_AUDIO_STOP, 0);
        }
        mResetting = false;
        mResetDone = true;
        return;
    }
    mResetTime--;
    setOutputLevel(mResetTime * mOutputLevel / (mResetTime + 1));
}

void SoundEngine::mixVoices(u32 used) {
    const NebulaHostServices* const host = nebula_host();
    host->voices_mix(host->user, mMixerGain, used);
}

bool SoundEngine::prepareNote(u32 bankNumber, s32 program, u8 key, u8 velocity, u16 priority, SoundNote* pNote) {
    const SoundBank* bank = bankNumber < cBankNumbers && mHasBank[bankNumber] ? &mBanks[bankNumber] : nullptr;
    const WaveBank* waves = bank != nullptr && bank->waveBank() < mWaveBankCount ? &mWaveBanks[bank->waveBank()] : nullptr;
    return SoundBanks::prepareNote(bank, waves, program, key, velocity, priority, &mInstRandom, pNote);
}

void SoundEngine::noteEvent(SoundNote* pNote, u32 event, NebulaVoice* pVoice) {
    switch (pNote->mOwner) {
    case OWNER_TRACK:
        SeqTrack::noteEvent(event, pNote, pVoice, static_cast< ChannelMgr* >(pNote->mOwnerData));
        break;
    case OWNER_ME:
        MeTrack::noteEvent(event, pNote, pVoice, static_cast< MeChannelMgr* >(pNote->mOwnerData));
        break;
    case OWNER_STREAM:
        StreamPlayer::noteEvent(event, pNote, pVoice, static_cast< StreamPlayer* >(pNote->mOwnerData));
        break;
    default:
        break;
    }
}

// The sound a note plays for, for the host's volume categories: a track's root names it (route 1 effect, 2 sequence), a stream
// is route 3; the Me tracks name none.
u8 SoundEngine::noteSound(const SoundNote& rNote, u32* pId) const {
    *pId = 0;
    switch (rNote.mOwner) {
    case OWNER_TRACK: {
        const ChannelMgr* mgr = static_cast< const ChannelMgr* >(rNote.mOwnerData);
        for (const SeqTrack* track = mgr != nullptr ? mgr->mTrack : nullptr; track != nullptr; track = track->parent()) {
            if (track->mOwnerRoute != 0) {
                *pId = track->mOwnerSound;
                return track->mOwnerRoute;
            }
        }
        return 0;
    }
    case OWNER_STREAM: {
        const StreamPlayer* stream = static_cast< const StreamPlayer* >(rNote.mOwnerData);
        *pId = stream->ownerSound();
        return 3;
    }
    default:
        return 0;
    }
}

void SoundEngine::reportTrackBound() {
    NEBULA_PANIC("a sequence opened more tracks than its cooked bound");
}

void SoundEngine::reportBadTarget(u32 offset) {
    NEBULA_PANIC("a sequence branched to byte %u, not an operation's start", offset);
}

// On the host's audio clock thread.
extern "C" NEBULA_GAME_API void nebula_game_sound_frame(void) {
    SoundEngine* const engine = SoundEngine::get();
    if (engine != nullptr) {
        engine->frame();
    }
}
