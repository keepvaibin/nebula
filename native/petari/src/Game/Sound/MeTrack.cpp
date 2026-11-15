#include "Game/Sound/MeTrack.hpp"
#include "Game/Sound/ChordInfo.hpp"
#include "Game/Sound/RhythmSystem.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include <new>

namespace {
    const SeqPoint cAdsTable[4] = {{0, 0, 0x7fff}, {0, 0, 0x7fff}, {0, 0, 0}, {0xe, 0, 0}};
    const OscData cEnvOsc = {Oscillator::TARGET_VOLUME, 1.0f, nullptr, nullptr, 1.0f, 0.0f};
    const OscData cPitchEnvOsc = {Oscillator::TARGET_PITCH, 1.0f, nullptr, nullptr, 1.0f, 0.0f};

    enum ModifyType : s32 { ModifyType_Chord, ModifyType_Scale, ModifyType_Steps, ModifyType_Octave };
    enum ModifyDir : s32 { ModifyDir_Up, ModifyDir_Down, ModifyDir_None, ModifyDir_Rand };
    const u8 cNullNote = 0x7f;

    u32 align16(u32 bytes) {
        return (bytes + 15) & ~15u;
    }
}  // namespace

void MeChannelMgr::releaseNote(u16 directRelease) {
    if (mNote == nullptr) {
        return;
    }
    mTrack->mSystem->engine()->voices().releaseNote(mNote, directRelease);
    mNote->free();
    mNote = nullptr;
}

void MeChannelMgr::setPauseFlag(bool pause) {
    if (mNote != nullptr) {
        mNote->mPause = pause;
    }
}

// A record: the track, then its call and loop entries (u32 addresses, then u16 counts).
u32 MeArena::recordBytes(u32 stack) {
    const u32 bytes = sizeof(MeTrack) + stack * (sizeof(u32) + sizeof(u16));
    return (bytes + 15) & ~15u;
}

void MeArena::setup(void* pMemory, u32 count, u32 stack) {
    mBase = static_cast< u8* >(pMemory);
    mCount = count;
    mRecordBytes = recordBytes(stack);
    mStack = stack;
    mFree = nullptr;
    mHolder.mHeld = 0;
    for (u32 i = count; i-- > 0;) {
        MeTrack* const track = reinterpret_cast< MeTrack* >(mBase + i * mRecordBytes);
        track->mNextFree = mFree;
        mFree = track;
    }
}

MeTrack* MeArena::alloc(RhythmSystem* pSystem, MePlayer* pPlayer) {
    MeTrack* free = mFree;
    if (free == nullptr) {
        return nullptr;
    }
    mFree = free->mNextFree;
    u8* const record = reinterpret_cast< u8* >(free);
    MeTrack* track = new (record) MeTrack();
    u32* const stack = reinterpret_cast< u32* >(record + sizeof(MeTrack));
    track->construct(pSystem, pPlayer, this, stack, reinterpret_cast< u16* >(stack + mStack), mStack);
    return track;
}

void MeArena::free(MeTrack* pTrack) {
    pTrack->mNextFree = mFree;
    mFree = pTrack;
}

const void* MeArena::begin() const {
    return mBase;
}

const void* MeArena::end() const {
    return mBase + mCount * mRecordBytes;
}

u32 MePlayer::bytes(u32 tracks, u32 stack) {
    return align16(sizeof(MePlayer)) + tracks * MeArena::recordBytes(stack);
}

MePlayer* MePlayer::create(void* pMemory, u32 tracks, u32 stack) {
    u8* cursor = static_cast< u8* >(pMemory);
    MePlayer* player = new (cursor) MePlayer();
    cursor += align16(sizeof(MePlayer));
    player->mArena.setup(cursor, tracks, stack);
    player->mArena.mHolder.mKind = OWNER_ME;
    player->mArena.mHolder.mPlayer = player;
    player->mRoot = player->mArena.alloc(&SoundEngine::get()->rhythm(), player);
    return player;
}

void MeTrack::construct(RhythmSystem* pSystem, MePlayer* pPlayer, MeArena* pArena, u32* pStack, u16* pLoopCounts, u32 stackRoom) {
    mSystem = pSystem;
    mPlayer = pPlayer;
    mArena = pArena;
    mStack = pStack;
    mLoopCounts = pLoopCounts;
    mStackRoom = stackRoom;
    mMgr.mTrack = this;
    init();
}

void MeTrack::init() {
    mParent = nullptr;
    mFirstChild = nullptr;
    mNextSibling = nullptr;
    mBrotherNo = -1;
    mTrackInfo = TrackInfo();
    mVolume = 1.0f;
    mPitchBend = 0.0f;
    mPan = 0.5f;
    mFxMix = 0.0f;
    mDolby = 0.0f;
    mBaseNote = 60;
    mBaseNotePrev = 60;
    mOscParam[0] = cEnvOsc;
    mOscParam[1] = cPitchEnvOsc;
    for (u32 i = 0; i < 4; i++) {
        mOscPoint[i] = cAdsTable[i];
    }
    mJumpFlag = false;
    mIsProcStopped = false;
    mStatus = STATUS_FREE;
}

void MeTrack::setStatus(s32 status) {
    mStatus = status;
    if (mParent == nullptr && mPlayer != nullptr && mPlayer->mRoot == this) {
        mPlayer->mStatus = status;
    }
}

// The parent's settings, envelopes and wait (the envelope settings keep pointing at the parent's points, as copied).
void MeTrack::inherit(const MeTrack& rParent) {
    mTrackInfo = rParent.mTrackInfo;
    for (u32 i = 0; i < 2; i++) {
        mOscParam[i] = rParent.mOscParam[i];
    }
    for (u32 i = 0; i < 4; i++) {
        mOscPoint[i] = rParent.mOscPoint[i];
    }
    mProgress = rParent.mProgress;
    autoWait();
    mStopAfterNote = rParent.mStopAfterNote;
    if (mStopAfterNote) {
        mCtrlStopAfterNote = true;
    }
    mJumpFlag = rParent.mJumpFlag;
}

MeTrack* MeTrack::child(u32 index) const {
    for (MeTrack* track = mFirstChild; track != nullptr; track = track->mNextSibling) {
        if (static_cast< u32 >(track->mBrotherNo) == index) {
            return track;
        }
        if (static_cast< u32 >(track->mBrotherNo) > index) {
            break;
        }
    }
    return nullptr;
}

MeTrack* MeTrack::newChild(u32 index) {
    if (child(index) != nullptr) {
        return nullptr;
    }
    MeTrack* track = mArena->alloc(mSystem, mPlayer);
    if (track == nullptr) {
        return nullptr;
    }
    track->inherit(*this);
    track->mParent = this;
    track->mBrotherNo = static_cast< s32 >(index);
    MeTrack** link = &mFirstChild;
    while (*link != nullptr && static_cast< u32 >((*link)->mBrotherNo) < index) {
        link = &(*link)->mNextSibling;
    }
    track->mNextSibling = *link;
    *link = track;
    return track;
}

// The track's own notes go off first, then the child's.
bool MeTrack::deleteChild(u32 index) {
    MeTrack** link = &mFirstChild;
    while (*link != nullptr && static_cast< u32 >((*link)->mBrotherNo) != index) {
        link = &(*link)->mNextSibling;
    }
    MeTrack* track = *link;
    if (track == nullptr) {
        return false;
    }
    noteOffAll(0);
    *link = track->mNextSibling;
    track->close();
    mArena->free(track);
    return true;
}

void MeTrack::close() {
    noteOffAll(0);
    while (mFirstChild != nullptr) {
        MeTrack* track = mFirstChild;
        mFirstChild = track->mNextSibling;
        track->close();
        mArena->free(track);
    }
}

void MeTrack::setSeqData(const u8* pData, u32 start) {
    mIsLocked = false;
    mData = pData;
    mStackDepth = 0;
    for (u32 i = 0; i < mStackRoom; i++) {
        mStack[i] = 0;
        mLoopCounts[i] = 0;
    }
    mCursor = start;
    mMeId = start;
    setStatus(STATUS_READY);
}

void MeTrack::stopSeq() {
    noteOffAll(0);
    for (MeTrack* track = mFirstChild; track != nullptr; track = track->mNextSibling) {
        track->noteOffAll(0);
    }
    setStatus(STATUS_FREE);
}

void MeTrack::suspendSeq() {
    stopSeq();
}

void MeTrack::stopAtCurrentNoteEnd() {
    mStopAfterNote = true;
    mCtrlStopAfterNote = true;
    if (mParent != nullptr) {
        return;
    }
    for (MeTrack* track = mFirstChild; track != nullptr; track = track->mNextSibling) {
        track->stopAtCurrentNoteEnd();
    }
}

s32 MeTrack::rhythmProc(s32 type) {
    if (mParent == nullptr && mStatus == STATUS_FREE) {
        return -1;
    }
    updateChannelParams();
    if (mParent != nullptr) {
        return 0;
    }
    if (!mIsProcStopped) {
        // seqProc: -1 when the program ends, 1 at a note boundary, else 0.
        const s32 procResult = seqProc(type);
        if ((procResult == 1 && mStopAfterNote) || procResult < 0) {
            mIsProcStopped = true;
        }
    }
    // A child's program opens and closes only its own children, so the next sibling stays.
    for (MeTrack* track = mFirstChild; track != nullptr;) {
        MeTrack* const next = track->mNextSibling;
        const u32 index = static_cast< u32 >(track->mBrotherNo);
        const s32 procResult = track->seqProc(type);
        if ((procResult == 1 && mStopAfterNote) || procResult < 0) {
            deleteChild(index);
        }
        track = next;
    }
    if (!hasChildren() && mIsProcStopped) {
        setStatus(STATUS_FREE);
        return -1;
    }
    return 0;
}

void MeTrack::setPauseFlag(bool pause) {
    mMgr.setPauseFlag(pause);
    mIsPaused = pause;
    if (mParent != nullptr) {
        return;
    }
    for (MeTrack* track = mFirstChild; track != nullptr; track = track->mNextSibling) {
        track->setPauseFlag(pause);
    }
}

void MeTrack::noteOffAll(u16 directRelease) {
    mMgr.releaseNote(directRelease);
}

bool MeTrack::noteOn(u32 key, u32 velocity) {
    mMgr.releaseNote(0);
    SoundEngine* const engine = mSystem->engine();
    SoundNote prepared;
    prepared.mOwner = OWNER_ME;
    prepared.mOwnerData = &mMgr;
    SoundNote* note = nullptr;
    if (engine->prepareNote(mTrackInfo.mBankNumber, mTrackInfo.mProgNumber, static_cast< u8 >(key), static_cast< u8 >(velocity),
                            static_cast< u16 >(mTrackInfo.mReleasePriority << 8 | mTrackInfo.mPlayPriority), &prepared)) {
        note = engine->voices().play(prepared, false);
    }
    if (note != nullptr) {
        note->mPause = mIsPaused;
        note->mUpdateTimer = 0;
        note->mParams = mMgr.mParams;
        overwriteOsc(note);
        if (mTrackInfo.mRelease != 0) {
            note->mOsc[0].mDirectRelease = mTrackInfo.mRelease;
        }
    }
    mMgr.mNote = note;
    return note != nullptr;
}

void MeTrack::overwriteOsc(SoundNote* pNote) {
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

void MeTrack::startNote() {
    noteOffAll(0);
    if (mBaseNote != cNullNote) {
        adjustInStartNoteRange(mBaseNote);
        adjustInNoteRange(mBaseNote);
        noteOn(mBaseNote, mTrackInfo.mVelocity);
        mSystem->mRegs[mTrackInfo.mNoteReg & 0xff] = mBaseNote;
    }
    mBaseNotePrev = mBaseNote;
    if (mIsInStart) {
        MeTrack* parent = mParent;
        if (parent != nullptr) {
            MeTrack* same = mSystem->sameMe(parent, parent->mMeId);
            if (same != nullptr) {
                same->suspendSeq();
            }
            parent->setStatus(STATUS_START);
        }
        setStatus(STATUS_START);
    }
    mIsInStart = false;
}

// The note's mix values as it starts and each update; its timer is never set (MeChannelMgr).
void MeTrack::noteEvent(u32 event, SoundNote* pNote, NebulaVoice* pVoice, MeChannelMgr* pMgr) {
    (void)pVoice;
    switch (event) {
    case NOTE_PLAY:
    case NOTE_START:
        pNote->mParams = pMgr->mParams;
        break;
    case NOTE_STOP:
        if (pNote == pMgr->mNote) {
            pMgr->mNote = nullptr;
        }
        break;
    default:
        break;
    }
}

void MeTrack::setOscAdsr(s16 attack, s16 decay, s16 sustainTime, s16 sustainLevel, u16 release) {
    mOscParam[0] = cEnvOsc;
    mOscParam[0].mTable = &mOscPoint[0];
    mOscPoint[0].mTime = attack;
    mOscPoint[1].mTime = decay;
    mOscPoint[2].mTime = sustainTime;
    mOscPoint[2].mValue = sustainLevel;
    mTrackInfo.mRelease = release;
}

void MeTrack::updateChannelParams() {
    f32 vol = mVolume;
    vol *= vol;
    f32 pitch = 1.0f;
    const f32 bend = mPitchBend * mTrackInfo.mBendSense * (1.0f / 3.0f);
    f32 pan = mPan - 0.5f;
    f32 dolby = mDolby;
    f32 fxmix = mFxMix;
    const SoundParams& rSound = mMgr.mSound;
    vol *= rSound.mVolume;
    pitch *= rSound.mPitch;
    pan += rSound.mPan - 0.5f;
    fxmix += rSound.mFxMix;
    dolby += rSound.mDolby;
    NoteParams& rParams = mMgr.mParams;
    if (mParent == nullptr) {
        rParams.mVolume = vol;
        rParams.mPitch = pitch;
        rParams.mPan = pan;
        rParams.mFxMix = fxmix;
        rParams.mDolby = dolby;
        rParams.mBend = bend;
    } else {
        const NoteParams& rParent = mParent->mMgr.mParams;
        rParams.mVolume = rParent.mVolume * vol;
        rParams.mPitch = rParent.mPitch * pitch;
        rParams.mPan = (rParent.mPan - 0.5f) + pan;
        rParams.mFxMix = rParent.mFxMix + fxmix;
        rParams.mDolby = rParent.mDolby + dolby;
        rParams.mBend = rParent.mBend + bend;
    }
    rParams.mPan += 0.5f;
}

void MeTrack::gframeProc() {
    updateChannelParams();
    if (mParent != nullptr) {
        return;
    }
    for (MeTrack* track = mFirstChild; track != nullptr; track = track->mNextSibling) {
        track->updateChannelParams();
    }
}

void MeTrack::setBaseNoteFromChord(const u8* pChordData, u8 numNotes, bool random) {
    ChordInfo& rChord = mSystem->chord();
    if (!rChord.isAvailable()) {
        mBaseNote = cNullNote;
        return;
    }
    s32 ctrlList[cNoteListRoom] = {};
    s32 chordNotes[ChordInfo::NUM_CHORD_NOTES] = {};
    makeRandomSearchList(ctrlList, numNotes, !random);
    makeRandomSearchList(chordNotes, ChordInfo::NUM_CHORD_NOTES, false);
    for (s32 i = 0; i < numNotes; i++) {
        const u8 ctrl = pChordData[ctrlList[i]];
        for (s32 j = 0; j < ChordInfo::NUM_CHORD_NOTES; j++) {
            const u8 note = rChord.mChordNoteList[chordNotes[j]];
            if (note == cNullNote) {
                continue;
            }
            if (((ctrl & 0x01) != 0 && rChord.isBass(note)) || ((ctrl & 0x02) != 0 && rChord.isRoot(note)) ||
                ((ctrl & 0x04) != 0 && rChord.isThird(note)) || ((ctrl & 0x08) != 0 && rChord.isFifth(note)) ||
                ((ctrl & 0x10) != 0 && rChord.isSixth(note)) || ((ctrl & 0x20) != 0 && rChord.isSeventh(note)) ||
                ((ctrl & 0x40) != 0 && rChord.isNinth(note)) || ((ctrl & 0x80) != 0 && rChord.isEleventh(note))) {
                mBaseNote = note;
                return;
            }
        }
    }
    mBaseNote = cNullNote;
}

void MeTrack::setBaseNoteFromScale(const u8* pScaleData, u8 numNotes, bool random) {
    ChordInfo& rChord = mSystem->chord();
    if (!rChord.isAvailable()) {
        mBaseNote = cNullNote;
        return;
    }
    s32 ctrlList[cNoteListRoom] = {};
    makeRandomSearchList(ctrlList, numNotes, !random);
    for (s32 i = 0; i < numNotes; i++) {
        const u8 ctrl = pScaleData[ctrlList[i]];
        const u8 note = (ctrl & 0x80) != 0 ? rChord.mCurScaleUp[ctrl & 0x7f] : rChord.mCurScaleDown[ctrl];
        if (note != cNullNote) {
            mBaseNote = note;
            return;
        }
    }
    mBaseNote = cNullNote;
}

void MeTrack::setBaseNoteFromChromatic(const u8* pChromData, u8 numNotes, bool random) {
    if (!mSystem->chord().isAvailable()) {
        mBaseNote = cNullNote;
        return;
    }
    s32 ctrlList[cNoteListRoom] = {};
    makeRandomSearchList(ctrlList, numNotes, !random);
    mBaseNote = pChromData[ctrlList[0]];
}

void MeTrack::adjustNote(s32 direction, s32 type) {
    if (mBaseNote == cNullNote) {
        return;
    }
    ChordInfo& rChord = mSystem->chord();
    if (!rChord.isAvailable()) {
        return;
    }
    u8 note = mBaseNote;
    switch (type) {
    case ModifyType_Chord:
        switch (direction) {
        case ModifyDir_Up:
            note = rChord.getNearestChordNoteDir(note, true);
            break;
        case ModifyDir_Down:
            note = rChord.getNearestChordNoteDir(note, false);
            break;
        case ModifyDir_None:
            note = rChord.getNearestChordNote(note);
            break;
        }
        break;
    case ModifyType_Scale:
        switch (direction) {
        case ModifyDir_Up:
            note = rChord.getNearestScaleNoteDir(note, true);
            break;
        case ModifyDir_Down:
            note = rChord.getNearestScaleNoteDir(note, false);
            break;
        case ModifyDir_None:
            note = rChord.getNearestScaleNote(note);
            break;
        }
        break;
    }
    mBaseNote = note;
}

void MeTrack::modifyNote(s32 direction, s32 type, s32 steps) {
    if (mBaseNote == cNullNote) {
        return;
    }
    if (direction == ModifyDir_Rand) {
        direction = getRandom(2) != 0;
    }
    ChordInfo& rChord = mSystem->chord();
    if (!rChord.isAvailable()) {
        return;
    }
    switch (type) {
    case ModifyType_Chord:
        if (direction == ModifyDir_Up) {
            mBaseNote = rChord.getUpperNoteOnChord(mBaseNote, steps);
        } else if (direction == ModifyDir_Down) {
            mBaseNote = rChord.getLowerNoteOnChord(mBaseNote, steps);
        }
        break;
    case ModifyType_Scale:
        if (direction == ModifyDir_Up) {
            mBaseNote = rChord.getUpperNoteOnScale(mBaseNote, steps);
        } else if (direction == ModifyDir_Down) {
            mBaseNote = rChord.getLowerNoteOnScale(mBaseNote, steps);
        }
        break;
    case ModifyType_Steps:
        if (direction == ModifyDir_Up) {
            mBaseNote += steps;
        } else {
            mBaseNote -= steps;
        }
        break;
    case ModifyType_Octave:
        if (direction == ModifyDir_Up) {
            mBaseNote += steps * 12;
        } else {
            mBaseNote -= steps * 12;
        }
        break;
    }
    if (mBaseNote != cNullNote) {
        adjustInNoteRange(mBaseNote);
    }
}

u32 MeTrack::getRandom(u32 range) {
    return mSystem->getRandom(range);
}

void MeTrack::makeRandomSearchList(s32* pList, s32 numItems, bool ordered) {
    for (s32 i = 0; i < numItems; i++) {
        pList[i] = i;
    }
    if (!ordered) {
        for (s32 i = 0; i < numItems; i++) {
            const s32 a = static_cast< s32 >(getRandom(static_cast< u32 >(numItems)));
            const s32 b = static_cast< s32 >(getRandom(static_cast< u32 >(numItems)));
            const s32 swap = pList[a];
            pList[a] = pList[b];
            pList[b] = swap;
        }
    }
}

void MeTrack::adjustInNoteRange(u8& rNote) const {
    while (rNote > mTrackInfo.mNoteRangeUpper) {
        rNote -= 12;
    }
    while (rNote < mTrackInfo.mNoteRangeLower) {
        rNote += 12;
    }
}

void MeTrack::adjustInStartNoteRange(u8& rNote) const {
    if (!mIsInStart || mNoStartRange) {
        return;
    }
    while (rNote > mTrackInfo.mNoteRangeStart + 12) {
        rNote -= 12;
    }
    while (rNote < mTrackInfo.mNoteRangeStart) {
        rNote += 12;
    }
}

MeTrack* MeTrack::brother(s32 index) const {
    if (mParent == nullptr || index < 0) {
        return nullptr;
    }
    return mParent->child(static_cast< u32 >(index));
}

void MeTrack::autoWait() {
    if (mProgress < 0) {
        return;
    }
    mWaitType = mProgress;
    mWaitTime = 1;
}

u32 MeTrack::read16() {
    const u32 value = static_cast< u32 >(mData[mCursor]) << 8 | mData[mCursor + 1];
    mCursor += 2;
    return value;
}

u32 MeTrack::read24() {
    const u32 value = static_cast< u32 >(mData[mCursor]) << 16 | static_cast< u32 >(mData[mCursor + 1]) << 8 | mData[mCursor + 2];
    mCursor += 3;
    return value;
}

// The cook's mStackBound covers every path: full is a phrase that was not cooked from its program.
bool MeTrack::call(u32 address) {
    if (mStackDepth >= mStackRoom) {
        return false;
    }
    mStack[mStackDepth++] = mCursor;
    mCursor = address;
    return true;
}

bool MeTrack::ret() {
    if (mStackDepth == 0) {
        return false;
    }
    mCursor = mStack[--mStackDepth];
    return true;
}

bool MeTrack::loopStart(u16 count) {
    if (mStackDepth >= mStackRoom) {
        return false;
    }
    mStack[mStackDepth] = mCursor;
    mLoopCounts[mStackDepth++] = count;
    return true;
}

bool MeTrack::loopEnd() {
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
    mCursor = mStack[mStackDepth - 1];
    return true;
}

// The wait counts the beats of its kind; then the program runs until it waits or locks.
s32 MeTrack::seqProc(s32 type) {
    if (mData == nullptr) {
        return 0;
    }
    if (mWaitType == type) {
        if (mWaitTime > 0 && --mWaitTime == 0) {
            mWaitType = -1;
            mWaitTime = 0;
        }
    }
    if (mWaitTime > 0) {
        return 0;
    }
    while (mWaitTime == 0 && !mIsLocked) {
        const s32 opResult = parse();
        if (opResult < 0) {
            return -1;
        }
        if (opResult == 1 && mCtrlStopAfterNote) {
            return 1;
        }
    }
    return 0;
}

// One operation: <0 the program ends, 1 a note boundary. Bytes without an operation (the cook refuses any at or
// past 0xA0) do nothing.
s32 MeTrack::parse() {
    const u32 code = read8();
    switch (code) {
    case 0x00:  // bank
        mTrackInfo.mBankNumber = static_cast< u16 >(read8());
        return 0;
    case 0x01:  // program
        mTrackInfo.mProgNumber = static_cast< u16 >(read8());
        return 0;
    case 0x02: {  // bank and program
        const u32 value = read16();
        mTrackInfo.mBankNumber = static_cast< u8 >(value >> 8);
        mTrackInfo.mProgNumber = static_cast< u8 >(value);
        return 0;
    }
    case 0x03:  // release
        mTrackInfo.mRelease = static_cast< u16 >(read16());
        return 0;
    case 0x04: {  // simple ADSR
        const u32 attack = read16();
        const u32 decay = read16();
        const u32 sustainTime = read16();
        const u32 sustainLevel = read16();
        const u32 release = read16();
        setOscAdsr(static_cast< s16 >(attack), static_cast< s16 >(decay), static_cast< s16 >(sustainTime), static_cast< s16 >(sustainLevel),
                   static_cast< u16 >(release));
        return 0;
    }
    case 0x10: {  // open a track
        const u32 index = read8();
        const u32 address = read24();
        MeTrack* track = newChild(index);
        if (track != nullptr) {
            track->setSeqData(mData, address);
        }
        return 0;
    }
    case 0x11:  // close a track
        deleteChild(read8());
        return 0;
    case 0x20: {  // start on a beat
        const s32 type = static_cast< s32 >(read8());
        if (type != 6) {
            wait(type, 1);
        }
        return 0;
    }
    case 0x24:  // end
        return -1;
    case 0x28: {  // wait
        const s32 type = static_cast< s32 >(read8());
        const s32 time = static_cast< s32 >(read16());
        wait(type, time);
        return 0;
    }
    case 0x2A:  // progress on
        mProgress = static_cast< s32 >(read8());
        return 0;
    case 0x2B:  // progress off
        mProgress = -1;
        return 0;
    case 0x30:  // jump
        mCursor = read24();
        return 0;
    case 0x31:  // call
        call(read24());
        return 0;
    case 0x32:  // return
        ret();
        return 0;
    case 0x33:  // loop start
        loopStart(static_cast< u16 >(read16()));
        return 0;
    case 0x34:  // loop end
        loopEnd();
        return 0;
    case 0x35: {  // jump if the flag is clear
        const u32 address = read24();
        if (!mJumpFlag) {
            mCursor = address;
        }
        mJumpFlag = false;
        return 0;
    }
    case 0x36: {  // jump if the flag is set
        const u32 address = read24();
        if (mJumpFlag) {
            mCursor = address;
        }
        mJumpFlag = false;
        return 0;
    }
    case 0x38:  // lock
        mIsLocked = true;
        return 0;
    case 0x39:  // no wait
        mWaitType = -1;
        mWaitTime = 0;
        return 0;
    case 0x3A: {  // call if the flag is clear
        const u32 address = read24();
        if (!mJumpFlag) {
            call(address);
        }
        mJumpFlag = false;
        return 0;
    }
    case 0x3B: {  // call if the flag is set
        const u32 address = read24();
        if (mJumpFlag) {
            call(address);
        }
        mJumpFlag = false;
        return 0;
    }
    case 0x40: {  // note: its definition
        const u32 address = read24();
        mTrackInfo.mVelocity = static_cast< u8 >(read8());
        call(address);
        return 1;
    }
    case 0x41:  // long note, its operands unused
        mCursor += 5;
        return 0;
    case 0x42:  // note with pitch, its operands unused
        mCursor += 5;
        return 0;
    case 0x43:  // long note with pitch, its operands unused
        mCursor += 6;
        return 0;
    case 0x48:  // note off
        noteOffAll(0);
        return 1;
    case 0x49:  // tie
        autoWait();
        return 0;
    case 0x4A:  // rest
        noteOffAll(0);
        autoWait();
        return 1;
    case 0x50:
        mTrackInfo.mNoteRangeUpper = static_cast< u8 >(read8());
        return 0;
    case 0x51:
        mTrackInfo.mNoteRangeLower = static_cast< u8 >(read8());
        return 0;
    case 0x52:
        mTrackInfo.mNoteRangeStart = static_cast< u8 >(read8());
        return 0;
    case 0x53:
        read8();
        mTrackInfo.mNoteRangeStart = 0x7f;
        return 0;
    case 0x54:
        read8();
        mTrackInfo.mNoteRangeStart = 0x7e;
        return 0;
    case 0x55:
        mNoStartRange = true;
        return 0;
    case 0x58:
        mPan = static_cast< u8 >(read8()) / 127.0f;
        return 0;
    case 0x59:
        mDolby = static_cast< u8 >(read8()) / 127.0f;
        return 0;
    case 0x5A:
        mVolume = static_cast< u16 >(read16()) / 32767.0f;
        return 0;
    case 0x5B:
        mPitchBend = static_cast< s16 >(read16()) / 32767.0f;
        return 0;
    case 0x5C:
        mTrackInfo.mBendSense = static_cast< u8 >(read8());
        return 0;
    case 0x5D:
        mFxMix = static_cast< u16 >(read16()) / 32767.0f;
        return 0;
    case 0x60:  // the register the note goes to
        mTrackInfo.mNoteReg = read8();
        return 0;
    case 0x68: {  // compare a register with its own number
        const u8 reg = static_cast< u8 >(read8());
        read8();
        mJumpFlag = reg == mSystem->mRegs[reg];
        return 0;
    }
    case 0x69: {  // compare with the per-child table, which nothing writes (all zero); the cook refuses numbers past its 8
        const u8 reg = static_cast< u8 >(read8());
        read8();
        mJumpFlag = reg == 0;
        return 0;
    }
    case 0x70:  // note definition start
        noteOffAll(0);
        return 1;
    case 0x71:  // note definition end
        startNote();
        autoWait();
        ret();
        return 0;
    case 0x78:
    case 0x79:  // range exceptions, unused
        mCursor += 3;
        return 0;
    case 0x80:
        mBaseNote = mBaseNotePrev;
        return 0;
    case 0x81: {
        const u8* data = mData + read24();
        setBaseNoteFromChord(data + 2, data[0], data[1] != 0);
        return 0;
    }
    case 0x82: {
        const u8* data = mData + read24();
        setBaseNoteFromScale(data + 2, data[0], data[1] != 0);
        return 0;
    }
    case 0x83: {
        const u8* data = mData + read24();
        setBaseNoteFromChromatic(data + 2, data[0], data[1] != 0);
        return 0;
    }
    case 0x84: {  // another part's note (none: unchanged)
        const MeTrack* track = brother(static_cast< s32 >(read8()));
        if (track != nullptr) {
            mBaseNote = track->mBaseNote;
        }
        return 0;
    }
    case 0x85: {  // the part above's note
        const MeTrack* track = brother(mParent != nullptr ? mBrotherNo - 1 : -2);
        if (track != nullptr) {
            mBaseNote = track->mBaseNote;
        }
        return 0;
    }
    case 0x86:
        mBaseNote = static_cast< u8 >(read8());
        return 0;
    case 0x87:
        mBaseNote = mSystem->mRegs[read8()];
        return 0;
    case 0x8A: {
        const s32 direction = static_cast< s32 >(read8());
        const s32 type = static_cast< s32 >(read8());
        adjustNote(direction, type);
        return 0;
    }
    case 0x90: {
        const s32 direction = static_cast< s32 >(read8());
        const s32 type = static_cast< s32 >(read8());
        const s32 steps = static_cast< s32 >(read8());
        modifyNote(direction, type, steps);
        return 0;
    }
    case 0x91: {
        const s32 direction = static_cast< s32 >(read8());
        const s32 type = static_cast< s32 >(read8());
        const u32 range = read8();
        modifyNote(direction, type, static_cast< s32 >(getRandom(range)));
        return 0;
    }
    default:
        return 0;
    }
}
