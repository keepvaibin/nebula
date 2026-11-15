#include "Game/Sound/SeqTrack.hpp"
#include "Game/Sound/SoundBank.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include <new>
#include <nebula_geometry.hpp>

namespace {
    const SeqPoint cDefaultAdsr[4] = {{0, 0, 0x7fff}, {0, 0, 0x7fff}, {0, 0, 0}, {0xe, 0, 0}};
    const OscData cEnvOsc = {Oscillator::TARGET_VOLUME, 1.0f, nullptr, nullptr, 1.0f, 0.0f};
    const OscData cPitchEnvOsc = {Oscillator::TARGET_PITCH, 1.0f, nullptr, nullptr, 1.0f, 0.0f};

    enum Condition : u32 { COND_ALWAYS, COND_NULL, COND_NONNULL, COND_EQUAL_ONE, COND_NEGATIVE, COND_POSITIVE };
    // Interrupt kinds: 0 unpause, 1 pause, 2 and 3 a write to port 0 or 1, 5 the interrupt timer, 6 every tick.
    const u32 cIntrTimer = 5;
    const u32 cIntrTick = 6;

    u8 hiByte(u32 v) {
        return static_cast< u8 >(v >> 8);
    }
    u8 loByte(u32 v) {
        return static_cast< u8 >(v);
    }
    u16 hiHalf(u32 v) {
        return static_cast< u16 >(v >> 16);
    }
    u16 loHalf(u32 v) {
        return static_cast< u16 >(v);
    }
    u32 be16(const u8* p) {
        return static_cast< u16 >((p[0] << 8) | p[1]);
    }
    u32 be24(const u8* p) {
        return (p[0] << 16) | (p[1] << 8) | p[2];
    }
    u32 be32(const u8* p) {
        return (static_cast< u32 >(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    }
}  // namespace

void ChannelMgr::init() {
    mSound = nullptr;
    mParams = NoteParams();
    for (u32 i = 0; i < mSlots; i++) {
        mNotes[i] = nullptr;
    }
}

void ChannelMgr::releaseAll() {
    Voices& rVoices = mTrack->engine()->voices();
    for (u32 i = 0; i < mSlots; i++) {
        SoundNote* note = mNotes[i];
        if (note != nullptr) {
            rVoices.releaseNote(note, 0);
            note->free();
            mNotes[i] = nullptr;
        }
    }
}

bool ChannelMgr::noteOff(u32 slot, u16 directRelease) {
    if (slot >= mSlots) {
        return false;
    }
    SoundNote* note = mNotes[slot];
    if (note == nullptr) {
        return false;
    }
    mTrack->engine()->voices().releaseNote(note, directRelease);
    note->free();
    mNotes[slot] = nullptr;
    return true;
}

void ChannelMgr::setPauseFlag(bool pause) {
    for (u32 i = 0; i < mSlots; i++) {
        if (mNotes[i] != nullptr) {
            mNotes[i]->mPause = pause;
        }
    }
}

// A record: the track, its channel managers, their note slots, its call and loop entries (s32 operations, u32 look-ahead
// offsets), its two envelopes, then the u16 loop counts.
u32 TrackArena::recordBytes(u32 mgrCount, const TrackRoom& rRoom) {
    const u32 bytes = sizeof(SeqTrack) + mgrCount * sizeof(ChannelMgr) + mgrCount * rRoom.mSlots * sizeof(SoundNote*) +
                      rRoom.mStack * (sizeof(s32) + sizeof(u32)) + 2 * rRoom.mEnvelope * sizeof(SeqPoint) + rRoom.mStack * sizeof(u16);
    return (bytes + 15) & ~15u;
}

void TrackArena::setup(void* pMemory, u32 count, u32 mgrCount, const TrackRoom& rRoom) {
    mBase = static_cast< u8* >(pMemory);
    mCount = count;
    mRecordBytes = recordBytes(mgrCount, rRoom);
    mMgrCount = mgrCount;
    mRoom = rRoom;
    mFree = nullptr;
    mHolder.mHeld = 0;
    for (u32 i = count; i-- > 0;) {
        SeqTrack* track = reinterpret_cast< SeqTrack* >(mBase + i * mRecordBytes);
        track->mNextRoot = mFree;
        mFree = track;
    }
}

SeqTrack* TrackArena::alloc(SoundEngine* pEngine) {
    SeqTrack* free = mFree;
    if (free == nullptr) {
        return nullptr;
    }
    mFree = free->mNextRoot;
    u8* const record = reinterpret_cast< u8* >(free);
    SeqTrack* const track = new (record) SeqTrack();
    track->construct(pEngine, this, record + sizeof(SeqTrack), mMgrCount, mRoom);
    return track;
}

void TrackArena::free(SeqTrack* pTrack) {
    pTrack->mNextRoot = mFree;
    mFree = pTrack;
}

void SeqTrack::construct(SoundEngine* pEngine, TrackArena* pArena, u8* pTail, u32 mgrCapacity, const TrackRoom& rRoom) {
    ChannelMgr* const mgrs = reinterpret_cast< ChannelMgr* >(pTail);
    SoundNote** const slots = reinterpret_cast< SoundNote** >(mgrs + mgrCapacity);
    s32* const stack = reinterpret_cast< s32* >(slots + mgrCapacity * rRoom.mSlots);
    u32* const scanStack = reinterpret_cast< u32* >(stack + rRoom.mStack);
    SeqPoint* const envelopes = reinterpret_cast< SeqPoint* >(scanStack + rRoom.mStack);
    u16* const loopCounts = reinterpret_cast< u16* >(envelopes + 2 * rRoom.mEnvelope);

    mEngine = pEngine;
    mArena = pArena;
    mStack = stack;
    mScanStack = scanStack;
    mLoopCounts = loopCounts;
    mStackRoom = rRoom.mStack;
    mOscTable[0] = envelopes;
    mOscTable[1] = envelopes + rRoom.mEnvelope;
    mEnvelopeRoom = rRoom.mEnvelope;
    mMgrs = mgrs;
    mMgrCapacity = mgrCapacity;
    for (u32 i = 0; i < mgrCapacity; i++) {
        new (&mgrs[i]) ChannelMgr();
        mgrs[i].mTrack = this;
        mgrs[i].mNotes = slots + i * rRoom.mSlots;
        mgrs[i].mSlots = rRoom.mSlots;
        mgrs[i].init();
    }
    init();
}

void SeqTrack::init() {
    mData = nullptr;
    mFile = nullptr;
    mOps = nullptr;
    mOffsets = nullptr;
    mPc = -1;
    mStackDepth = 0;
    for (u32 i = 0; i < mStackRoom; i++) {
        mStack[i] = -1;
        mLoopCounts[i] = 0;
    }
    mRhythm = nullptr;
    mTimer = 0;
    mSwap = -1;
    mIntrTable = 0;
    mIntrPending = 0;
    mIntrMask = 0;
    mIntTimer = 0;
    mIntrCount = 0;
    mIntrPeriod = 0;
    mWaitNote = false;
    for (u32 i = 0; i < 16; i++) {
        mPorts[i] = 0;
    }
    mImport = 0;
    mExport = 0;
    mMoveParam[0].mValue = 1.0f;
    mMoveParam[1].mValue = 0.0f;
    mMoveParam[3].mValue = 0.5f;
    mMoveParam[2].mValue = 0.0f;
    mMoveParam[4].mValue = 0.0f;
    mMoveParam[5].mValue = 0.0f;
    for (u32 i = 0; i < cTimedParams; i++) {
        mMoveParam[i].mCount = 0;
        mMoveParam[i].mTarget = mMoveParam[i].mValue;
    }
    for (u32 i = 0; i < 14; i++) {
        mRegs[i] = 0;
    }
    mOscParam[0] = cEnvOsc;
    mOscParam[1] = cPitchEnvOsc;
    for (int i = 0; i < 4; i++) {
        mOscPoint[i] = cDefaultAdsr[i];
    }
    mParent = nullptr;
    mFirstChild = nullptr;
    mMgrs[0].init();
    mMgrCount = 1;
    mPlaytime = 1.0f;
    mSampleInterval = 1.0f;
    mVibDepth = 0.0f;
    mVibPitch = 1.0f;
    mTremDepth = 0.0f;
    mTremPitch = 1.0f;
    mVibDelay = 0;
    mTremDelay = 0;
    mPanPower = 1.0f;
    mDirectRelease = 0;
    mSkipSample = 0;
    mTempo = 120;
    mTimebase = 48;
    mTempoRate = 1.0f;
    mSecondsClock = nullptr;
    mSecondsClockData = nullptr;
    mClockTicks = 0.0;
    updateTempo();
    mTranspose = 0;
    mPitch = 60;
    mBankNumber = 0;
    mProgNumber = 0xf0;
    mBendSense = 0xc;
    mNoteOnPrio = 0x40;
    mReleasePrio = 0;
    mGateRate = 100;
    mMixConfig[0] = 0x150;
    mMixConfig[1] = 0x210;
    mMixConfig[2] = 0x352;
    mMixConfig[3] = 0x412;
    mMixConfig[4] = 0;
    mMixConfig[5] = 0;
    for (u32 i = 0; i < 8; i++) {
        mFIR[i] = 0;
        mIIR[i] = 0;
    }
    mFIR[0] = 0x7fff;
    mIIR[0] = 0x7fff;
    mFilterMode = 0;
    mPauseFlag = false;
    mIsMute = false;
    mIsDirectlyPlayed = true;
    mInvalidateSeq = true;
    mIsOwnedByParent = false;
    mReadyToPlay = false;
    mIsStopped = false;
    mStatus = STATUS_FREE;
}

// A group beyond the count starts as new when the count grows again.
void SeqTrack::setChannelMgrCount(u32 count) {
    if (count > mMgrCapacity) {
        count = mMgrCapacity;
    }
    for (u32 i = mMgrCount; i < count; i++) {
        mMgrs[i].init();
    }
    mMgrCount = count < 1 ? 1 : count;
}

void SeqTrack::inherit(const SeqTrack& rParent) {
    mIsDirectlyPlayed = rParent.mIsDirectlyPlayed;
    mBankNumber = rParent.mBankNumber;
    mProgNumber = rParent.mProgNumber;
    mNoteOnPrio = rParent.mNoteOnPrio;
    mReleasePrio = rParent.mReleasePrio;
    mSkipSample = rParent.mSkipSample;
    for (u32 i = 0; i < 8; i++) {
        mFIR[i] = rParent.mFIR[i];
        mIIR[i] = rParent.mIIR[i];
    }
    mFilterMode = rParent.mFilterMode;
    for (u32 i = 0; i < 6; i++) {
        mMixConfig[i] = rParent.mMixConfig[i];
    }
}

void SeqTrack::setSeqData(const SeqFile* pFile, u32 entryOp) {
    mFile = pFile;
    const u32* groupFirst = reinterpret_cast< const u32* >(pFile + 1);
    const u32* entries = groupFirst + pFile->mGroupCount + 1;
    mOffsets = entries + pFile->mEntryCount;
    mOps = reinterpret_cast< const SeqOp* >(mOffsets + pFile->mOpCount);
    mData = reinterpret_cast< const u8* >(mOps + pFile->mOpCount);
    mStackDepth = 0;
    for (u32 i = 0; i < mStackRoom; i++) {
        mStack[i] = -1;
        mLoopCounts[i] = 0;
    }
    mPc = static_cast< s32 >(entryOp);
}

void SeqTrack::freeTrack() {
    mArena->free(this);
}

void SeqTrack::close() {
    SeqTrack** link = &mFirstChild;
    while (*link != nullptr) {
        SeqTrack* child = *link;
        child->close();
        if (child->mIsOwnedByParent) {
            *link = child->mNextSibling;
            child->freeTrack();
        } else {
            link = &child->mNextSibling;
        }
    }
    for (u32 i = 0; i < mMgrCount; i++) {
        mMgrs[i].releaseAll();
    }
    mParent = nullptr;
    mStatus = STATUS_STOPPED;
}

SeqTrack* SeqTrack::child(u32 index) const {
    for (SeqTrack* child = mFirstChild; child != nullptr && child->mIndex <= index; child = child->mNextSibling) {
        if (child->mIndex == index) {
            return child;
        }
    }
    return nullptr;
}

bool SeqTrack::connectChild(u32 index, SeqTrack* pChild) {
    SeqTrack** link = &mFirstChild;
    while (*link != nullptr && (*link)->mIndex < index) {
        link = &(*link)->mNextSibling;
    }
    if (*link != nullptr && (*link)->mIndex == index) {
        return false;
    }
    pChild->mParent = this;
    pChild->mIndex = index;
    pChild->mNextSibling = *link;
    *link = pChild;
    return true;
}

SeqTrack* SeqTrack::reserveChild(u32 index) {
    SeqTrack* track = mArena->alloc(mEngine);
    if (track == nullptr) {
        mEngine->reportTrackBound();
        return nullptr;
    }
    track->mIsOwnedByParent = true;
    if (!connectChild(index, track)) {
        track->freeTrack();
        return nullptr;
    }
    return track;
}

void SeqTrack::closeChild(u32 index) {
    SeqTrack** link = &mFirstChild;
    while (*link != nullptr && (*link)->mIndex < index) {
        link = &(*link)->mNextSibling;
    }
    SeqTrack* child = *link;
    if (child == nullptr || child->mIndex != index) {
        return;
    }
    rootTrack()->updateSeq(false, 1.0f);
    child->close();
    if (child->mIsOwnedByParent) {
        *link = child->mNextSibling;
        child->freeTrack();
    }
}

SeqTrack* SeqTrack::openChild(u32 index) {
    SeqTrack** link = &mFirstChild;
    while (*link != nullptr && (*link)->mIndex < index) {
        link = &(*link)->mNextSibling;
    }
    SeqTrack* child = *link != nullptr && (*link)->mIndex == index ? *link : nullptr;
    if (child != nullptr) {
        switch (child->mStatus) {
        case STATUS_FREE:
            break;
        case STATUS_RUN:
            rootTrack()->updateSeq(false, 1.0f);
            child->close();
            [[fallthrough]];
        case STATUS_STOPPED: {
            const bool owned = child->mIsOwnedByParent;
            *link = child->mNextSibling;
            child->init();
            child->mIsOwnedByParent = owned;
            connectChild(index, child);
            break;
        }
        }
    } else {
        child = mArena->alloc(mEngine);
        if (child == nullptr) {
            mEngine->reportTrackBound();
            return nullptr;
        }
        child->mIsOwnedByParent = true;
        connectChild(index, child);
    }
    child->setChannelMgrCount(mMgrCount);
    child->inherit(*this);
    return child;
}

SeqTrack* SeqTrack::rootTrack() {
    SeqTrack* track = this;
    while (track->mParent != nullptr) {
        track = track->mParent;
    }
    return track;
}

s32 SeqTrack::transposeTotal() const {
    if (mParent != nullptr) {
        return mParent->transposeTotal() + mTranspose;
    }
    return mTranspose;
}

bool SeqTrack::isMute() const {
    if (mParent != nullptr) {
        return mIsMute || mParent->isMute();
    }
    return mIsMute;
}

SoundNote* SeqTrack::channelStart(ChannelMgr* pMgr, u32 key, u32 velocity, u32 updateTimer) {
    SoundNote prepared;
    prepared.mOwner = OWNER_TRACK;
    prepared.mOwnerData = pMgr;
    if (!mEngine->prepareNote(mBankNumber, mProgNumber, loByte(key), loByte(velocity), static_cast< u16 >(mNoteOnPrio | mReleasePrio << 8), &prepared)) {
        return nullptr;
    }
    SoundNote* note = mEngine->voices().play(prepared, false);
    if (note == nullptr) {
        return nullptr;
    }
    note->mUpdateTimer = updateTimer;
    note->mSkipSamples = mSkipSample;
    note->mVibrato.mDelay = mVibDelay;
    note->mTremolo.mDelay = mTremDelay;
    note->mParams = pMgr->mParams;
    for (u32 i = 0; i < 6; i++) {
        note->mMixConfig[i] = mMixConfig[i];
    }
    overwriteOsc(note);
    if (mDirectRelease != 0) {
        note->mOsc[0].mDirectRelease = mDirectRelease;
    }
    return note;
}

void SeqTrack::overwriteOsc(SoundNote* pNote) {
    for (u32 i = 0; i < 2; i++) {
        if (mOscParam[i].mTable != nullptr) {
            pNote->mOsc[i].initStart(&mOscParam[i]);
            if (pNote->mHolder == nullptr) {
                pNote->mHolder = &mArena->mHolder;
                mArena->mHolder.mHeld++;
            }
        }
    }
}

bool SeqTrack::noteOn(u32 slot, u32 key, u32 velocity) {
    if (isMute()) {
        return false;
    }
    bool isAllStarted = true;
    key += transposeTotal();
    for (u32 i = 0; i < mMgrCount; i++) {
        ChannelMgr& rMgr = mMgrs[i];
        rMgr.noteOff(slot, 0);
        SoundNote* note = channelStart(&rMgr, key, velocity, 0);
        if (note == nullptr) {
            isAllStarted = false;
        }
        if (slot < rMgr.mSlots) {
            rMgr.mNotes[slot] = note;
        }
    }
    return isAllStarted;
}

bool SeqTrack::gateOn(u32 key, u32 velocity, f32 seqTime, u32 flags) {
    key += transposeTotal();
    if (mGateRate != 100) {
        seqTime *= mGateRate / 100.0f;
    }
    const u32 dspTime = seqTimeToDspTime(seqTime);
    const u32 isSweep = flags & 1;
    const u32 updateTimer = (flags & 6) != 0 ? 0 : dspTime;
    const u32 pitch = isSweep ? mPitch : key;
    const s32 pitchDelta = key - pitch;
    for (u32 i = 0; i < mMgrCount; i++) {
        ChannelMgr& rMgr = mMgrs[i];
        if (!mReadyToPlay) {
            rMgr.noteOff(0, 0);
            if (!isMute()) {
                rMgr.mNotes[0] = channelStart(&rMgr, pitch, velocity, updateTimer);
            }
        } else {
            SoundNote* note = rMgr.mNotes[0];
            if (note != nullptr) {
                note->mKey = static_cast< s16 >(pitch - note->mBaseKey);
                note->mVelocity = static_cast< u16 >(velocity);
                note->mUpdateTimer = updateTimer;
            }
        }
        SoundNote* note;
        if (isSweep && (note = rMgr.mNotes[0]) != nullptr) {
            if (dspTime == 0) {
                note->mKeySweep = static_cast< f32 >(pitchDelta);
            } else {
                note->mKeySweep = 0.0f;
                note->mKeySweepTarget = static_cast< f32 >(pitchDelta);
            }
            note->mKeySweepCount = dspTime;
        }
    }
    mPitch = static_cast< u8 >(key);
    mReadyToPlay = (flags & 2) != 0;
    return true;
}

bool SeqTrack::noteOff(u32 slot, u16 directRelease) {
    bool isAllReleased = true;
    for (u32 i = 0; i < mMgrCount; i++) {
        if (!mMgrs[i].noteOff(slot, directRelease)) {
            isAllReleased = false;
        }
    }
    return isAllReleased;
}

void SeqTrack::noteOffAll(u16 directRelease) {
    for (u32 i = 0; i < mMgrs[0].mSlots; i++) {
        noteOff(i, directRelease);
    }
    for (SeqTrack* child = mFirstChild; child != nullptr; child = child->mNextSibling) {
        if (child->mStatus == STATUS_RUN) {
            child->noteOffAll(directRelease);
        }
    }
}

bool SeqTrack::checkNoteStop(u32 slot) const {
    for (u32 i = 0; i < mMgrCount; i++) {
        if (slot < mMgrs[i].mSlots && mMgrs[i].mNotes[slot] != nullptr) {
            return false;
        }
    }
    return true;
}

void SeqTrack::mute(bool mute) {
    mIsMute = mute;
    if (!mute) {
        return;
    }
    noteOffAll(10);
}

void SeqTrack::setChannelPauseFlag(bool pause) {
    for (u32 i = 0; i < mMgrCount; i++) {
        mMgrs[i].setPauseFlag(pause);
    }
    for (SeqTrack* child = mFirstChild; child != nullptr; child = child->mNextSibling) {
        child->setChannelPauseFlag(pause);
    }
}

void SeqTrack::pause(bool pause) {
    if (mPauseFlag != pause) {
        mPauseFlag = pause;
        setChannelPauseFlag(pause);
        interrupt(!pause);
    }
}

void SeqTrack::updateTimedParam() {
    for (u32 i = 0; i < cTimedParams; i++) {
        MoveParam& rParam = mMoveParam[i];
        if (rParam.mCount != 0) {
            rParam.mValue += (rParam.mTarget - rParam.mValue) / rParam.mCount;
            rParam.mCount--;
        }
    }
}

void SeqTrack::setParam(u32 param, f32 value, u32 time) {
    if (param >= cTimedParams) {
        return;
    }
    MoveParam& rParam = mMoveParam[param];
    rParam.mTarget = value;
    if (time == 0) {
        rParam.mValue = rParam.mTarget;
    }
    rParam.mCount = time;
}

void SeqTrack::updateTrack(f32 pan) {
    updateTempo();
    for (u32 i = 0; i < mMgrCount; i++) {
        ChannelMgr& rMgr = mMgrs[i];
        f32 volume = mMoveParam[0].mValue;
        volume *= volume;
        f32 pitch = 1.0f;
        const f32 bend = mMoveParam[1].mValue * mBendSense * 0.333333343267f;
        f32 panValue = (mMoveParam[3].mValue - 0.5f) * mPanPower;
        f32 fxmix = mMoveParam[2].mValue;
        f32 dolby = mMoveParam[4].mValue;
        const SoundParams* params = rMgr.mSound;
        if (params != nullptr) {
            volume *= params->mVolume;
            pitch *= params->mPitch;
            panValue += params->mPan - 0.5f;
            fxmix += params->mFxMix;
            dolby += params->mDolby;
        }
        panValue *= pan;
        if (mParent == nullptr) {
            rMgr.mParams.mVolume = volume;
            rMgr.mParams.mPitch = pitch;
            rMgr.mParams.mPan = panValue;
            rMgr.mParams.mFxMix = fxmix;
            rMgr.mParams.mDolby = dolby;
            rMgr.mParams.mBend = bend;
        } else {
            const ChannelMgr* parent = i < mParent->mMgrCount ? &mParent->mMgrs[i] : &mParent->mMgrs[0];
            rMgr.mParams.mVolume = parent->mParams.mVolume * volume;
            rMgr.mParams.mPitch = parent->mParams.mPitch * pitch;
            rMgr.mParams.mPan = parent->mParams.mPan - 0.5f + panValue;
            rMgr.mParams.mFxMix = parent->mParams.mFxMix + fxmix;
            rMgr.mParams.mDolby = parent->mParams.mDolby + dolby;
            rMgr.mParams.mBend = parent->mParams.mBend + bend;
        }
        rMgr.mParams.mPan += 0.5f;
    }
}

void SeqTrack::updateTempo() {
    if (mParent == nullptr) {
        // Sequence ticks a driver tick: beats a minute by ticks a beat.
        mSampleInterval = mTempoRate * ((mTimebase * mTempo) / (60.0f * SoundEngine::cTicksPerSecond));
    } else {
        mTempo = mParent->mTempo;
        mTimebase = mParent->mTimebase;
        mSampleInterval = mParent->mSampleInterval;
    }
}

void SeqTrack::updateSeq(bool update, f32 pan) {
    if (!update) {
        update = mInvalidateSeq;
    }
    mInvalidateSeq = false;
    if (update) {
        updateTrack(pan);
    }
    pan *= mPanPower;
    for (SeqTrack* child = mFirstChild; child != nullptr; child = child->mNextSibling) {
        if (child->mStatus == STATUS_RUN) {
            child->updateSeq(update, pan);
        }
    }
}

u32 SeqTrack::seqTimeToDspTime(f32 seqTime) {
    if (mIsDirectlyPlayed) {
        seqTime /= mSampleInterval;
    } else {
        seqTime *= 120.0f / mTimebase;
        seqTime *= SoundEngine::cTicksPerStep / 10.0f;
    }
    return static_cast< u32 >(seqTime);
}

void SeqTrack::setTempo(u16 tempo) {
    mTempo = tempo;
    mInvalidateSeq = true;
    updateTempo();
}

void SeqTrack::setTempoRate(f32 rate) {
    mTempoRate = rate;
    mInvalidateSeq = true;
    updateTempo();
}

void SeqTrack::setTimebase(u16 timebase) {
    mTimebase = timebase;
    mInvalidateSeq = true;
    updateTempo();
}

void SeqTrack::setSecondsClock(SecondsClock clock, void* pData) {
    mSecondsClock = clock;
    mSecondsClockData = pData;
    mClockTicks = 0.0;
    mPlaytime = 0.0f;
}

void SeqTrack::setOscTable(u32 osc, const u8* pBigEndian) {
    SeqPoint* const table = mOscTable[osc];
    for (u32 i = 0;; i++) {
        // The cook's mEnvelopePoints covers every envelope operation.
        if (i == mEnvelopeRoom) {
            NEBULA_PANIC("an envelope past the file's %u points (not cooked from its program)", mEnvelopeRoom);
        }
        table[i].mMode = static_cast< s16 >(be16(pBigEndian));
        table[i].mTime = static_cast< s16 >(be16(pBigEndian + 2));
        table[i].mValue = static_cast< s16 >(be16(pBigEndian + 4));
        pBigEndian += 6;
        // Its modes 0xD, 0xE and 0xF end it (Oscillator::update).
        if (table[i].mMode >= 0xd) {
            break;
        }
    }
    mOscParam[osc].mTable = table;
    if (osc == 0) {
        return;
    }
    mOscParam[osc].mRelease = nullptr;
}

void SeqTrack::setOscAdsr(s16 attack, s16 decay, s16 sustain, s16 release, u16 directRelease) {
    mOscParam[0] = cEnvOsc;
    mOscPoint[0].mTime = attack;
    mOscParam[0].mTable = mOscPoint;
    mOscPoint[1].mTime = decay;
    mOscPoint[2].mTime = sustain;
    mOscPoint[2].mValue = release;
    mDirectRelease = directRelease;
}

void SeqTrack::setFIR(const u8* pBigEndian) {
    for (u32 i = 0; i < 8; i++) {
        mFIR[i] = static_cast< s16 >(be16(pBigEndian + i * 2));
    }
    mFilterMode &= 0x20;
    mFilterMode |= 8;
}

void SeqTrack::setIIR(const s16* pCoefficients) {
    for (u32 i = 0; i < 8; i++) {
        mIIR[i] = pCoefficients[i];
    }
    mFilterMode |= 0x20;
}

void SeqTrack::writePort(u32 port, u16 value) {
    mImport |= 1 << port;
    mPorts[port] = value;
    if (port <= 1) {
        interrupt(port == 0 ? 2 : 3);
    }
}

u16 SeqTrack::readPort(u32 port) {
    mExport &= ~(1 << port);
    return mPorts[port];
}

void SeqTrack::updateChannel(SoundNote* pNote, NebulaVoice* pVoice) {
    pNote->mVibrato.mDepth = mVibDepth;
    pNote->mVibrato.mPitch = mVibPitch;
    pNote->mTremolo.mDepth = mTremDepth;
    pNote->mTremolo.mPitch = mTremPitch;
    if (mFilterMode & 0x20) {
        VoiceFilter::setIIR(pVoice, mIIR);
    }
    if (mFilterMode & 0x1f) {
        VoiceFilter::setFIR8(pVoice, mFIR);
    }
    VoiceFilter::setMode(pVoice, mFilterMode);
}

void SeqTrack::noteEvent(u32 event, SoundNote* pNote, NebulaVoice* pVoice, ChannelMgr* pMgr) {
    SeqTrack* track = pMgr->mTrack;
    switch (event) {
    case NOTE_PLAY:
    case NOTE_START:
        pNote->mParams = pMgr->mParams;
        track->updateChannel(pNote, pVoice);
        break;
    case NOTE_TIMER:
        track->mEngine->voices().releaseNote(pNote, 0);
        pNote->free();
        pMgr->mNotes[0] = nullptr;
        break;
    case NOTE_STOP:
        for (u32 i = 0; i < pMgr->mSlots; i++) {
            if (pNote == pMgr->mNotes[i]) {
                pMgr->mNotes[i] = nullptr;
                return;
            }
        }
        break;
    }
}

void SeqTrack::interrupt(u32 type) {
    const u32 mask = 1 << type;
    if (!(mIntrMask & mask)) {
        return;
    }
    mIntrPending |= mask;
}

s32 SeqTrack::findInterrupt() {
    u32 intr = mIntrMask & mIntrPending;
    for (u32 i = 0; intr != 0; i++) {
        if (intr & 1) {
            mIntrPending &= ~(1 << i);
            return static_cast< s32 >(i);
        }
        intr >>= 1;
    }
    return -1;
}

void SeqTrack::checkInterrupt() {
    s32 intr;
    if (mSwap < 0 && (intr = findInterrupt()) >= 0) {
        const u32 offset = be24(mData + mIntrTable + intr * 3);
        mSwap = mPc;
        mPc = opAt(offset);
    }
}

void SeqTrack::timerProcess() {
    if (mIntrCount != 0) {
        if (--mIntrCount == 0) {
            interrupt(cIntrTimer);
            if (mIntTimer != 0) {
                if (--mIntTimer != 0) {
                    mIntrCount = mIntrPeriod;
                }
            } else {
                mIntrCount = mIntrPeriod;
            }
        }
    }
}

s32 SeqTrack::seqTick() {
    if (mData == nullptr) {
        return 0;
    }
    interrupt(cIntrTick);
    timerProcess();
    if (mWaitNote) {
        if (!checkNoteStop(0)) {
            return 0;
        }
        mWaitNote = false;
    }
    if (mTimer > 0) {
        mTimer--;
    }
    checkInterrupt();
    while ((mTimer == 0 || mSwap >= 0) && !mWaitNote) {
        if (step() < 0) {
            return -1;
        }
    }
    return 0;
}

s32 SeqTrack::tickProc() {
    if (mPauseFlag) {
        return 0;
    }
    const s32 result = seqTick();
    updateTimedParam();
    mInvalidateSeq = true;
    if (result < 0) {
        return -1;
    }
    SeqTrack** link = &mFirstChild;
    while (*link != nullptr) {
        SeqTrack* child = *link;
        if (child->mStatus == STATUS_RUN && child->tickProc() < 0) {
            rootTrack()->updateSeq(false, 1.0f);
            child->close();
            if (child->mIsOwnedByParent) {
                *link = child->mNextSibling;
                child->freeTrack();
                continue;
            }
        }
        link = &child->mNextSibling;
    }
    return 0;
}

s32 SeqTrack::seqMain() {
    if (mIsStopped) {
        updateSeq(true, 1.0f);
        close();
        return -1;
    }
    const f32 step = 1.0f;
    const f64 clockSeconds = mSecondsClock != nullptr ? mSecondsClock(mSecondsClockData) : -1.0;
    if (clockSeconds >= 0.0) {
        // Its place is the clock's: as many ticks as have come due.
        const f64 due = clockSeconds * mTimebase * mTempo / 60.0;
        // Paused, no tick comes due: it takes up what the clock moved when it plays again.
        while (!mPauseFlag && mClockTicks + 1.0 <= due) {
            mClockTicks += 1.0;
            if (tickProc() < 0) {
                updateSeq(false, 1.0f);
                close();
                return -1;
            }
        }
        updateSeq(false, step);
        return 0;
    }
    while (mPlaytime >= step) {
        mPlaytime -= step;
        mClockTicks += 1.0;
        if (tickProc() < 0) {
            updateSeq(false, 1.0f);
            close();
            return -1;
        }
    }
    mPlaytime += mSampleInterval;
    updateSeq(false, step);
    return 0;
}

u32 SeqTrack::runningChildren() const {
    u32 bits = 0;
    for (SeqTrack* child = mFirstChild; child != nullptr && child->mIndex < 16; child = child->mNextSibling) {
        if (child->mStatus == STATUS_RUN) {
            bits |= 1u << child->mIndex;
        }
    }
    return bits;
}

u32 SeqTrack::nextOffset() const {
    if (mPc < 0) {
        return 0;
    }
    return static_cast< u32 >(mPc) < mFile->mOpCount ? mOffsets[mPc] : mFile->mDataBytes;
}

s32 SeqTrack::stackTopOffset() const {
    if (mStackDepth == 0) {
        return -1;
    }
    return static_cast< s32 >(mOffsets[mStack[mStackDepth - 1]]);
}

s32 SeqTrack::opAt(u32 offset) const {
    u32 low = 0;
    u32 high = mFile->mOpCount;
    while (low < high) {
        const u32 mid = (low + high) / 2;
        if (mOffsets[mid] < offset) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low < mFile->mOpCount && mOffsets[low] == offset) {
        return static_cast< s32 >(low);
    }
    mEngine->reportBadTarget(offset);
    return -1;
}

void SeqTrack::jumpToOffset(u32 offset) {
    mPc = opAt(offset);
}

// The registers as the programs address them: 0 to 2 bytes, 3 to 13 halves, 32 to 41 pairs of them.
void SeqTrack::writeRegister(u32 reg, u32 value) {
    if (reg < 14) {
        mRegs[reg] = reg <= 2 ? static_cast< u8 >(value) : static_cast< u16 >(value);
        return;
    }
    switch (reg) {
    case 32:
        mRegs[0] = hiByte(value);
        mRegs[1] = loByte(value);
        break;
    case 33:
        mRegs[4] = hiHalf(value);
        mRegs[5] = loHalf(value);
        break;
    case 34:
        mRegs[4] = static_cast< u8 >(mRegs[4]);
        mRegs[4] |= static_cast< u16 >((value & 0xff) << 8);
        break;
    case 35:
        mRegs[4] &= 0xff00;
        mRegs[4] |= static_cast< u8 >(value);
        break;
    case 36:
        mRegs[5] = static_cast< u8 >(mRegs[5]);
        mRegs[5] |= static_cast< u16 >((value & 0xff) << 8);
        break;
    case 37:
        mRegs[5] &= 0xff00;
        mRegs[5] |= static_cast< u8 >(value);
        break;
    case 38:
        mRegs[6] = hiHalf(value);
        mRegs[7] = loHalf(value);
        break;
    case 39:
        mRegs[8] = hiHalf(value);
        mRegs[9] = loHalf(value);
        break;
    case 40:
        mRegs[10] = hiHalf(value);
        mRegs[11] = loHalf(value);
        break;
    case 41:
        mRegs[12] = hiHalf(value);
        mRegs[13] = loHalf(value);
        break;
    }
}

u32 SeqTrack::readReg(u32 reg) const {
    if (reg < 0x40) {
        if (reg < 14) {
            return mRegs[reg];
        }
        switch (reg) {
        case 32:
            return ((mRegs[0] & 0xff) << 8) | (mRegs[1] & 0xff);
        case 33:
            return (static_cast< u32 >(mRegs[4]) << 16) | mRegs[5];
        case 34:
            return hiByte(mRegs[4]);
        case 35:
            return loByte(mRegs[4]);
        case 36:
            return hiByte(mRegs[5]);
        case 37:
            return loByte(mRegs[5]);
        case 38:
            return (static_cast< u32 >(mRegs[6]) << 16) | mRegs[7];
        case 39:
            return (static_cast< u32 >(mRegs[8]) << 16) | mRegs[9];
        case 40:
        case 41:
            // 41 reads 40's pair.
            return (static_cast< u32 >(mRegs[10]) << 16) | mRegs[11];
        default:
            return 0;
        }
    }
    switch (reg - 0x40) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
    case 0x8:
    case 0x9:
    case 0xa:
    case 0xb:
    case 0xc:
    case 0xd:
    case 0xe:
    case 0xf:
        return mPorts[reg - 0x40];
    case 0x20:
        return runningChildren();
    case 0x22:
        return mTimebase;
    case 0x23:
        return static_cast< u32 >(static_cast< s32 >(mTranspose));
    case 0x24:
        return mBendSense;
    case 0x25:
        return mGateRate;
    case 0x26:
        return mSkipSample;
    case 0x27:
        return mBankNumber;
    case 0x28:
        return mProgNumber;
    case 0x29:
        return static_cast< u32 >(mPanPower * 32767.0f);
    case 0x2a:
        return mReleasePrio | mNoteOnPrio;
    case 0x2b:
        return mNoteOnPrio;
    case 0x2c:
        return mReleasePrio;
    case 0x2d:
        return mDirectRelease;
    case 0x2e:
        return static_cast< u32 >(mVibDepth * 1524.0f);
    case 0x2f:
        return static_cast< u32 >(mVibDepth * 12192.0f);
    case 0x30:
        return static_cast< u32 >(mTremDepth * 256.0f);
    case 0x31:
        return static_cast< u32 >(mVibPitch * 64.0f);
    case 0x32:
        return static_cast< u32 >(mTremPitch * 64.0f);
    case 0x33:
        return mVibDelay;
    case 0x34:
        return mTremDelay;
    default:
        return 0;
    }
}

void SeqTrack::writeReg(u32 reg, u32 value) {
    writeRegister(SEQ_REG_FLAG, value);
    if (reg < 0x40) {
        writeRegister(reg, value);
        return;
    }
    switch (reg - 0x40) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
    case 0x8:
    case 0x9:
    case 0xa:
    case 0xb:
    case 0xc:
    case 0xd:
    case 0xe:
    case 0xf:
        mPorts[reg - 0x40] = static_cast< u16 >(value);
        break;
    case 0x22:
        setTimebase(static_cast< u16 >(value));
        break;
    case 0x23:
        mTranspose = static_cast< s8 >(value);
        break;
    case 0x24:
        mBendSense = static_cast< u8 >(value);
        break;
    case 0x25:
        mGateRate = static_cast< u8 >(value);
        break;
    case 0x26:
        mSkipSample = value;
        break;
    case 0x27:
        mBankNumber = static_cast< u8 >(value);
        break;
    case 0x28:
        mProgNumber = static_cast< u8 >(value);
        break;
    case 0x29:
        mPanPower = value / 32767.0f;
        break;
    case 0x2a:
        mNoteOnPrio = loByte(value);
        mReleasePrio = hiByte(value);
        break;
    case 0x2b:
        mNoteOnPrio = static_cast< u8 >(value);
        break;
    case 0x2c:
        mReleasePrio = static_cast< u8 >(value);
        break;
    case 0x2d:
        mDirectRelease = static_cast< u16 >(value);
        break;
    case 0x2e:
        mVibDepth = value / 1524.0f;
        break;
    case 0x2f:
        mVibDepth = value / 12192.0f;
        break;
    case 0x30:
        mTremDepth = value / 256.0f;
        break;
    case 0x31:
        mVibPitch = value / 64.0f;
        break;
    case 0x32:
        mTremPitch = value / 64.0f;
        break;
    case 0x33:
        mVibDelay = static_cast< u16 >(value);
        break;
    case 0x34:
        mTremDelay = static_cast< u16 >(value);
        break;
    }
}

bool SeqTrack::conditionCheck(u32 condition) const {
    const u16 flag = mRegs[SEQ_REG_FLAG];
    switch (condition) {
    case COND_ALWAYS:
        return true;
    case COND_NULL:
        return flag == 0;
    case COND_NONNULL:
        return flag != 0;
    case COND_EQUAL_ONE:
        return flag == 1;
    case COND_NEGATIVE:
        return flag >= 0x8000;
    case COND_POSITIVE:
        return flag < 0x8000;
    }
    return false;
}

// The cook's mStackBound covers every path: full is a file that was not cooked from its program.
bool SeqTrack::call(u32 op) {
    if (mStackDepth >= mStackRoom) {
        return false;
    }
    mStack[mStackDepth++] = mPc;
    mPc = static_cast< s32 >(op);
    return true;
}

bool SeqTrack::ret() {
    if (mStackDepth == 0) {
        return false;
    }
    mPc = mStack[--mStackDepth];
    return true;
}

bool SeqTrack::loopStart(u32 count) {
    if (mStackDepth >= mStackRoom) {
        return false;
    }
    mStack[mStackDepth] = mPc;
    mLoopCounts[mStackDepth++] = static_cast< u16 >(count);
    return true;
}

bool SeqTrack::loopEnd() {
    if (mStackDepth == 0) {
        return false;
    }
    u16 count = mLoopCounts[mStackDepth - 1];
    if (count > 0) {
        count--;
    }
    if (count == 0) {
        mStackDepth--;
        return true;
    }
    mLoopCounts[mStackDepth - 1] = count;
    mPc = mStack[mStackDepth - 1];
    return true;
}

u32 SeqTrack::arg(const SeqOp& rOp, u32 i) const {
    return (rOp.mRegisters >> i) & 1 ? readReg(rOp.mArgs[i]) : rOp.mArgs[i];
}

// A branch target: an operation index as cooked, or a byte offset read from a register.
u32 SeqTrack::target(const SeqOp& rOp, u32 i, u32 value) const {
    if ((rOp.mRegisters >> i) & 1) {
        return static_cast< u32 >(opAt(value));
    }
    return value;
}

void SeqTrack::execNoteOnGate(u32 key, u32 velocity, u32 time, u32 flags) {
    u32 gate = 0;
    if (flags >> 6 & 1) {
        gate |= 2;
    }
    if (flags >> 7 & 1) {
        gate |= 1;
    }
    if (time == 0) {
        gate |= 4;
    }
    gateOn(key, velocity, static_cast< f32 >(time), gate);
    if (time != 0) {
        mTimer = static_cast< s32 >(time);
    } else {
        mWaitNote = true;
    }
}

bool SeqTrack::execNoteOnMidi(u32 slot, u32 key, u32 velocity) {
    if (mRhythm != nullptr) {
        return mRhythm->noteOn(this, slot, key, velocity, nextOffset());
    }
    return noteOn(slot, key, velocity);
}

void SeqTrack::execNoteOff(u32 slot) {
    if (mRhythm != nullptr) {
        mRhythm->noteOff(this, slot);
        return;
    }
    noteOff(slot, 0);
}

// One operation. <0: the program finished.
s32 SeqTrack::step() {
    if (mPc < 0) {
        return -1;
    }
    const SeqOp& rOp = mOps[mPc++];
    if (mRhythm != nullptr && !(rOp.mRegisters & SeqOp::cRegisterForm)) {
        mRhythm->command(this, rOp);
    }
    u32 a[5];
    for (u32 i = 0; i < 5; i++) {
        a[i] = arg(rOp, i);
    }
    switch (rOp.mCode) {
    case SEQ_NOTE_ON:
        execNoteOnMidi(a[0], a[1], a[2]);
        break;
    case SEQ_NOTE_GATE:
        execNoteOnGate(a[1], a[2], a[3], a[0]);
        break;
    case SEQ_NOTE_OFF:
        execNoteOff(a[0]);
        break;
    case SEQ_LAST_NOTE:
        if (a[0] < 0x100) {
            mPitch = static_cast< u8 >(a[0]);
            mPitch += transposeTotal();
        }
        break;
    case SEQ_PARAM_RAW:
        setParam(a[0], static_cast< s16 >(a[1]) / 32767.0f, a[2]);
        break;
    case SEQ_PARAM_BYTE: {
        s16 value = static_cast< s16 >(a[1] << 8);
        if ((a[1] & 0x80) == 0) {
            value |= static_cast< s16 >(a[1] << 1);
        }
        setParam(a[0], value / 32767.0f, a[2]);
        break;
    }
    case SEQ_OPEN_TRACK: {
        const u32 entry = target(rOp, 1, a[1]);
        SeqTrack* child = openChild(a[0]);
        if (child != nullptr) {
            child->setSeqData(mFile, entry);
            child->start();
        }
        break;
    }
    case SEQ_CLOSE_TRACK:
        closeChild(a[0]);
        break;
    case SEQ_CALL:
        call(target(rOp, 0, a[0]));
        break;
    case SEQ_CALL_IF:
        if (conditionCheck(a[0])) {
            call(target(rOp, 1, a[1]));
        }
        break;
    case SEQ_RETURN:
        // With no call to return to, the program goes on with its next bytes (a[0]); the cook does not follow a return.
        if (!ret()) {
            mPc = opAt(a[0]);
        }
        break;
    case SEQ_RETURN_IF:
        if (conditionCheck(a[0])) {
            ret();
        }
        break;
    case SEQ_JUMP:
        mPc = static_cast< s32 >(target(rOp, 0, a[0]));
        break;
    case SEQ_JUMP_IF:
        if (conditionCheck(a[0])) {
            mPc = static_cast< s32 >(target(rOp, 1, a[1]));
        }
        break;
    case SEQ_JUMP_TABLE:
        mPc = opAt(be24(mData + a[1] + a[0] * 3));
        break;
    case SEQ_CALL_TABLE:
        call(static_cast< u32 >(opAt(be24(mData + a[1] + a[0] * 3))));
        break;
    case SEQ_LOOP_START:
        loopStart(a[0]);
        break;
    case SEQ_LOOP_END:
        loopEnd();
        break;
    case SEQ_READ_PORT: {
        mImport &= ~(1 << a[0]);
        writeReg(a[1], mPorts[a[0]]);
        break;
    }
    case SEQ_WRITE_PORT:
        mExport |= 1 << a[0];
        mPorts[a[0]] = static_cast< u16 >(a[1]);
        break;
    case SEQ_CHECK_IMPORT:
        writeRegister(SEQ_REG_FLAG, (mImport & (1 << a[0])) != 0);
        break;
    case SEQ_CHECK_EXPORT:
        writeRegister(SEQ_REG_FLAG, (mExport & (1 << a[0])) != 0);
        break;
    case SEQ_PARENT_WRITE_PORT:
        if (mParent != nullptr) {
            mParent->writePort(a[0], static_cast< u16 >(a[1]));
        }
        break;
    case SEQ_CHILD_WRITE_PORT: {
        SeqTrack* track = child((a[0] >> 4) & 0xf);
        if (track != nullptr) {
            track->writePort(a[0] & 0xf, static_cast< u16 >(a[1]));
        }
        break;
    }
    case SEQ_PARENT_READ_PORT:
        if (mParent != nullptr) {
            writeReg(a[1], mParent->readPort(a[0]));
        }
        break;
    case SEQ_CHILD_READ_PORT: {
        SeqTrack* track = child((a[0] >> 4) & 0xf);
        if (track != nullptr) {
            writeReg(a[1], track->readPort(a[0] & 0xf));
        }
        break;
    }
    case SEQ_REG_LOAD:
        writeReg(a[0], a[1]);
        break;
    case SEQ_REG: {
        u32 reg = a[1];
        u32 value = a[2];
        switch (a[0]) {
        case 0:
            break;
        case 1:
            value += readReg(reg);
            break;
        case 2:
            value = readReg(reg) - value;
            break;
        case 3:
            value = readReg(reg) - value;
            reg = 3;
            break;
        case 4:
            value *= readReg(reg);
            reg = 0x21;
            break;
        case 5:
            value &= readReg(reg);
            break;
        case 6:
            value |= readReg(reg);
            break;
        case 7:
            value ^= readReg(reg);
            break;
        case 8: {
            FastRandom& rRandom = mEngine->seqRandom();
            value = value != 0 ? (rRandom.rand() >> 9) % value : rRandom.rand() >> 9;
            break;
        }
        case 9:
            value = readReg(reg) << value;
            break;
        case 10:
            value = readReg(reg) >> value;
            break;
        default:
            return 0;
        }
        writeReg(reg, value);
        break;
    }
    case SEQ_REG_UNARY:
        if (a[0] == 0xb) {
            writeReg(a[1], 0u - readReg(a[1]));
        }
        break;
    case SEQ_REG_TABLE: {
        u32 value = a[2];
        u32 index = a[3];
        switch (a[0]) {
        case 0xc:
            value = mData[value + index];
            break;
        case 0xd:
            value = be16(mData + value + index * 2);
            break;
        case 0xe:
            index += index * 2;
            value = be24(mData + value + index);
            break;
        case 0xf:
            value = be32(mData + value + index * 4);
            break;
        case 0x10:
            value = be32(mData + value + index);
            break;
        default:
            return 0;
        }
        writeReg(a[1], value);
        break;
    }
    case SEQ_TEMPO:
        setTempo(static_cast< u16 >(a[0]));
        break;
    case SEQ_BANK_PROGRAM: {
        const u16 value = loHalf(a[0]);
        mBankNumber = hiByte(value);
        mProgNumber = loByte(value);
        break;
    }
    case SEQ_BANK:
        mBankNumber = loByte(a[0]);
        break;
    case SEQ_PROGRAM:
        mProgNumber = loByte(a[0]);
        break;
    case SEQ_ENV_SCALE:
        mOscParam[a[0] & 1].mScale = a[1] / 16383.0f;
        break;
    case SEQ_ENV_SET:
        setOscTable(a[0] & 1, mData + a[1]);
        break;
    case SEQ_SIMPLE_ADSR:
        setOscAdsr(static_cast< s16 >(a[0]), static_cast< s16 >(a[1]), static_cast< s16 >(a[2]), static_cast< s16 >(a[3]), static_cast< u16 >(a[4]));
        break;
    case SEQ_BUS_CONNECT:
        if (a[0] < 6) {
            mMixConfig[a[0]] = static_cast< u16 >(a[1]);
        }
        break;
    case SEQ_IIR_CUTOFF:
        if (a[0] < 0x80) {
            setIIR(cIIRCutoff[a[0]]);
        }
        break;
    case SEQ_IIR_SET: {
        s16 iir[8] = {};
        for (u32 i = 0; i < 4; i++) {
            iir[i] = static_cast< s16 >(a[i]);
        }
        setIIR(iir);
        break;
    }
    case SEQ_FIR_SET:
        setFIR(mData + a[0]);
        break;
    case SEQ_WAIT:
        mTimer = static_cast< s32 >(a[0]);
        break;
    case SEQ_INTERRUPT_TABLE:
        mIntrTable = a[0];
        break;
    case SEQ_INTERRUPT_ON:
        mIntrMask |= a[0];
        break;
    case SEQ_INTERRUPT_OFF:
        mIntrMask &= ~a[0];
        break;
    case SEQ_INTERRUPT_RETURN:
        if (mSwap >= 0) {
            mPc = mSwap;
            mSwap = -1;
        }
        checkInterrupt();
        break;
    case SEQ_INTERRUPT_CLEAR:
        mSwap = -1;
        checkInterrupt();
        break;
    case SEQ_INTERRUPT_TIMER:
        mIntTimer = static_cast< u8 >(a[0]);
        mIntrCount = a[1];
        mIntrPeriod = a[1];
        break;
    case SEQ_SYNC:
        writeRegister(SEQ_REG_FLAG, 0xffff);
        break;
    case SEQ_NOP:
        break;
    case SEQ_FINISH:
        return -1;
    }
    return 0;
}
