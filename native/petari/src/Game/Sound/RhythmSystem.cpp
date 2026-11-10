#include "Game/Sound/RhythmSystem.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/StreamPlayer.hpp"
#include "Game/System/NebulaArena.hpp"
#include <new>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f64, cNum_sequenceSeconds_f1, "sequenceSeconds.f1", 60.0);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 120.0f);
namespace {
    // The beat note keys of kinds 0-4.
    NEBULA_TUNABLE_LIST(u8, cRhythmKeys, 5, {0x18, 0x1e, 0x1b, 0x19, 0x1a});
    NEBULA_TUNABLE(s32, cRhythmBeat, 0);
    // Kind 5: a beat note played softer than 100 (its velocity less 10), told before the beat itself.
    NEBULA_TUNABLE(s32, cRhythmSoftBeat, 5);
    NEBULA_TUNABLE(u32, cLookAheadSteps, 1000);

    // The authored program's operations at 0xA0-0xFF as the sequence format lays them out: operand count and widths (2 bits each:
    // byte, 16-bit, 24-bit, register).
    void operandShape(u8 code, u32* pCount, u16* pWidths) {
        u32 count = 0;
        u16 widths = 0;
        switch (code) {
        case 0xB1: count = 3; break;
        case 0xB2: count = 1; break;
        case 0xB3: count = 4; widths = 0x40; break;
        case 0xB4: count = 1; break;
        case 0xB8: count = 2; break;
        case 0xB9: count = 2; widths = 0x4; break;
        case 0xBA: count = 3; widths = 0x10; break;
        case 0xBB: count = 3; widths = 0x14; break;
        case 0xC1: count = 2; widths = 0x8; break;
        case 0xC2: count = 1; break;
        case 0xC3: count = 1; widths = 0x2; break;
        case 0xC4: count = 2; widths = 0x8; break;
        case 0xC6: count = 1; break;
        case 0xC7: count = 1; widths = 0x2; break;
        case 0xC8: count = 2; widths = 0x8; break;
        case 0xC9: count = 2; widths = 0xb; break;
        case 0xCA: count = 2; widths = 0xb; break;
        case 0xCB: count = 1; widths = 0x1; break;
        case 0xD0: count = 2; break;
        case 0xD1: count = 2; widths = 0xc; break;
        case 0xD2: count = 1; break;
        case 0xD3: count = 1; break;
        case 0xD4: count = 2; widths = 0xc; break;
        case 0xD5: count = 2; widths = 0xc; break;
        case 0xD6: count = 2; break;
        case 0xD7: count = 2; break;
        case 0xD8: count = 2; widths = 0x4; break;
        case 0xD9: count = 3; widths = 0x30; break;
        case 0xDA: count = 3; widths = 0x10; break;
        case 0xDB: count = 2; break;
        case 0xDC: count = 4; widths = 0xe0; break;
        case 0xE0: count = 1; widths = 0x1; break;
        case 0xE1: count = 1; widths = 0x1; break;
        case 0xE2: count = 1; break;
        case 0xE3: count = 1; break;
        case 0xE7: count = 2; widths = 0x4; break;
        case 0xE8: count = 2; widths = 0x8; break;
        case 0xE9: count = 5; widths = 0x155; break;
        case 0xEA: count = 2; widths = 0x4; break;
        case 0xEB: count = 1; break;
        case 0xEC: count = 4; widths = 0x55; break;
        case 0xED: count = 1; widths = 0x2; break;
        case 0xF1: count = 1; break;
        case 0xF3: count = 1; widths = 0x2; break;
        case 0xF4: count = 1; widths = 0x1; break;
        case 0xF5: count = 1; widths = 0x1; break;
        case 0xF8: count = 2; widths = 0x4; break;
        case 0xF9: count = 1; widths = 0x1; break;
        default: break;
        }
        *pCount = count;
        *pWidths = widths;
    }
}  // namespace

void TempoAdjuster::setInfo(f32 beatMul, u32 introBeats, u32 loopBeats, u32 loopStartSamples, u32 loopEndSamples) {
    mBeatMul = beatMul;
    mIntroBeats = introBeats;
    mLoopBeats = loopBeats;
    mLoopStartSamples = loopStartSamples;
    mLoopEndSamples = loopEndSamples;
}

void TempoAdjuster::setStream(StreamPlayer* pStream) {
    mStream = pStream;
    mIsNewStream = pStream != nullptr;
}

void TempoAdjuster::reject() {
    mStream = nullptr;
    mIsNewStream = false;
}

bool TempoAdjuster::takeNewStream() {
    const bool isNew = mIsNewStream;
    mIsNewStream = false;
    return isNew;
}

f64 TempoAdjuster::sequenceSeconds(void* pSelf) {
    const TempoAdjuster* const self = static_cast< const TempoAdjuster* >(pSelf);
    if (self->mStream == nullptr) {
        return -1.0;
    }
    const u32 position = self->mStream->mPosition;
    const u32 loops = self->mStream->mLoopCount;
    const u32 loopSamples = self->mLoopEndSamples - self->mLoopStartSamples;
    if (self->mLoopBeats == 0 || loopSamples == 0) {
        const u32 rate = self->mStream->rate();
        return rate != 0 ? static_cast< f64 >(position) / rate : -1.0;
    }
    const f64 secondsPerBeat = cNum_sequenceSeconds_f1 / self->mTempo * self->mBeatMul;
    const f64 intro = self->mIntroBeats * secondsPerBeat;
    const f64 loop = self->mLoopBeats * secondsPerBeat;
    if (loops == 0 && position < self->mLoopStartSamples) {
        return position * intro / self->mLoopStartSamples;
    }
    return intro + loops * loop + (static_cast< f64 >(position) - self->mLoopStartSamples) * loop / loopSamples;
}

void RhythmSeqParser::init() {
    mIsActive = false;
    mTempo = cNum_init_f1;
}

s32 RhythmSeqParser::rhythmType(u32 key) {
    for (s32 i = 0; i < 5; i++) {
        if (cRhythmKeys[i] == key) {
            return i;
        }
    }
    return -1;
}

bool RhythmSeqParser::registChord() {
    if (!mIsUsed || !mSystem->chord().isAvailable()) {
        return false;
    }
    return mSystem->chord().setCurChord(mChordId);
}

bool RhythmSeqParser::registScale() {
    if (!mIsUsed || !mSystem->chord().isAvailable()) {
        return false;
    }
    return mSystem->chord().setCurScale(mScaleId);
}

void RhythmSeqParser::flushChordScale() {
    registChord();
    registScale();
}

// Written without the register prefix, before it runs: the tempo (0xE0), the scale (0xE2, the bank operation's byte) and the chord
// (0xE3, the program's), each the only operation the cook makes that code from.
void RhythmSeqParser::command(SeqTrack* pTrack, const SeqOp& rOp) {
    (void)pTrack;
    switch (rOp.mCode) {
    case SEQ_TEMPO:
        mTempo = static_cast< u16 >(rOp.mArgs[0]);
        break;
    case SEQ_BANK:
        mScaleId = static_cast< u8 >(rOp.mArgs[0]);
        registScale();
        mSystem->publishChord();
        break;
    case SEQ_PROGRAM:
        mChordId = static_cast< u8 >(rOp.mArgs[0]);
        registChord();
        mSystem->publishChord();
        break;
    default:
        break;
    }
}

bool RhythmSeqParser::noteOn(SeqTrack* pTrack, u32 slot, u32 key, u32 velocity, u32 next) {
    if (!mIsActive) {
        return false;
    }
    const s32 noteOff = nextNoteOff(pTrack, slot, next);
    (void)noteOff;
    const s32 type = rhythmType(key);
    if (type == cRhythmBeat) {
        mTempoAdjuster.setTempo(mTempo);
        if (mTempoAdjuster.takeNewStream()) {
            pTrack->rootTrack()->setSecondsClock(TempoAdjuster::sequenceSeconds, &mTempoAdjuster);
        }
    }
    if (mIsUsed) {
        if (type == cRhythmBeat && static_cast< s32 >(velocity - 10) < 100) {
            mSystem->meBeat(cRhythmSoftBeat);
        }
        mSystem->meBeat(type);
    }
    return pTrack->noteOn(slot, key, velocity);
}

void RhythmSeqParser::noteOff(SeqTrack* pTrack, u32 slot) {
    if (!mIsActive) {
        return;
    }
    pTrack->noteOff(slot, 0);
}

u8 RhythmSeqParser::Scan::read8() {
    if (mCursor >= mBytes) {
        mFailed = true;
        return 0;
    }
    return mData[mCursor++];
}

u32 RhythmSeqParser::Scan::read16() {
    const u32 high = read8();
    return high << 8 | read8();
}

u32 RhythmSeqParser::Scan::read24() {
    const u32 high = read16();
    return high << 8 | read8();
}

u32 RhythmSeqParser::Scan::readMidiValue() {
    const u8 first = read8();
    if (!(first & 0x80)) {
        return first;
    }
    u32 value = first & 0x7f;
    for (s32 i = 0;; i++) {
        if (i > 2) {
            return 0;
        }
        value <<= 7;
        const u8 byte = read8();
        value |= byte & 0x7f;
        if (!(byte & 0x80)) {
            break;
        }
    }
    return value;
}

// The ticks from a note on to the next note off of its slot (or the slot's next note), read ahead on the program's bytes. Not
// found within 1000 operations: -1, and the track goes on from where the reading
// stopped. A read past the program's end or a return with both stacks empty ends the reading (-1, the track stays).
s32 RhythmSeqParser::nextNoteOff(SeqTrack* pTrack, u32 slot, u32 next) {
    Scan scan = {};
    scan.mData = pTrack->seqData();
    scan.mBytes = pTrack->seqBytes();
    scan.mCursor = next;
    scan.mCallStack = pTrack->scanStack();
    scan.mCallRoom = pTrack->stackRoom();
    u32 noteOff = 0;
    for (u32 i = 0; i < cLookAheadSteps; i++) {
        const u8 data = scan.read8();
        if ((data & 0x80) == 0) {
            const u8 flags = scan.read8();
            scan.read8();
            if (scan.mFailed) {
                return -1;
            }
            if ((flags & 7) == slot) {
                return static_cast< s32 >(noteOff);
            }
            continue;
        }
        switch (data & 0xf0) {
        case 0x80:
            if ((data & 7) == slot) {
                return static_cast< s32 >(noteOff);
            }
            break;
        case 0x90:
            passRegCommand(pTrack, scan, (data & 7) + 1);
            break;
        default:
            noteOff += passCommand(pTrack, scan, data, 0);
            break;
        }
        if (scan.mFailed) {
            return -1;
        }
    }
    pTrack->jumpToOffset(scan.mCursor);
    return -1;
}

u32 RhythmSeqParser::passRegCommand(SeqTrack* pTrack, Scan& rScan, int numArgs) {
    u8 data = rScan.read8();
    u16 widths = 0;
    u16 bits = 3;
    for (int i = 0; i < numArgs; i++) {
        if ((data & 0x80) != 0) {
            widths |= bits;
        }
        data <<= 1;
        bits <<= 2;
    }
    return passCommand(pTrack, rScan, rScan.read8(), widths);
}

// One operation passed over: its operands read, a call, return or jump followed, and its wait (0xF0, 0xF1) returned.
u32 RhythmSeqParser::passCommand(SeqTrack* pTrack, Scan& rScan, u8 code, u16 widths) {
    u32 count = 0;
    u16 shape = 0;
    if (code == 0xB0) {
        rScan.read8();
    } else {
        operandShape(code, &count, &shape);
    }
    u32 readType = shape | widths;
    u32 args[8] = {};
    for (u32 i = 0; i < count; i++) {
        u32 arg = 0;
        switch (readType & 3) {
        case 0:
            arg = rScan.read8();
            break;
        case 1:
            arg = rScan.read16();
            break;
        case 2:
            arg = rScan.read24();
            break;
        case 3:
            arg = pTrack->readReg(rScan.read8());
            break;
        }
        args[i] = arg;
        readType = static_cast< u16 >(readType) >> 2;
    }
    if (rScan.mFailed || code == 0xB0) {
        return 0;
    }
    switch (code) {
    case 0xC5:  // return: to the look ahead's own call, else the track's
        if (rScan.mCallDepth != 0) {
            rScan.mCursor = rScan.mCallStack[--rScan.mCallDepth];
        } else {
            const s32 top = pTrack->stackTopOffset();
            if (top < 0) {
                rScan.mFailed = true;
            } else {
                rScan.mCursor = static_cast< u32 >(top);
            }
        }
        return 0;
    case 0xC3:  // call: the cook's stack bound covers the look ahead's calls too
        if (rScan.mCallDepth < rScan.mCallRoom) {
            rScan.mCallStack[rScan.mCallDepth++] = rScan.mCursor;
            rScan.mCursor = args[0];
        }
        return 0;
    case 0xC7:  // jump
        rScan.mCursor = args[0];
        return 0;
    case 0xF0:  // wait
        return rScan.readMidiValue();
    case 0xF1:  // wait, a byte
        return args[0];
    case 0xFD:
        passPrintf(pTrack, rScan);
        return 0;
    default:
        return 0;
    }
}

// The bytes a print operation reads: its text to the 0, then a byte for each value.
void RhythmSeqParser::passPrintf(SeqTrack* pTrack, Scan& rScan) {
    (void)pTrack;
    u32 values = 0;
    for (u32 i = 0; i < 0x80; i++) {
        const u8 c = rScan.read8();
        if (c == 0 || rScan.mFailed) {
            break;
        }
        if (c == '\\') {
            if (rScan.read8() == 0) {
                break;
            }
        } else if (c == '%') {
            i++;
            if (rScan.read8() == 0) {
                break;
            }
            values++;
        }
    }
    for (u32 i = 0; i < values; i++) {
        rScan.read8();
    }
}

void RhythmSystem::setup(SoundEngine* pEngine, u32 parsers, NebulaArena* pArena) {
    mEngine = pEngine;
    mParserCount = parsers;
    mParsers = static_cast< RhythmSeqParser* >(NebulaArena::alloc(sizeof(RhythmSeqParser) * (parsers + 1), 16, pArena));
    if (mParsers == nullptr) {
        NEBULA_PANIC("no memory for the rhythm parsers");
    }
    for (u32 i = 0; i < parsers; i++) {
        new (&mParsers[i]) RhythmSeqParser();
        mParsers[i].mSystem = this;
        mParsers[i].mIsUsed = i == 0;
    }
}

void RhythmSystem::publishChord() {
    mChordAvailable = mChord.isAvailable();
    mChordIndex = mChord.mCurChordIndex;
    mScaleIndex = mChord.mCurScaleIndex;
}

void RhythmSystem::meBeat(s32 type) {
    MePlayer** link = &mFirstMe;
    while (*link != nullptr) {
        MePlayer* const player = *link;
        if (player->mRoot->rhythmProc(type) < 0) {
            *link = player->mNext;
            player->mNext = nullptr;
            player->mListed = false;
        } else {
            link = &player->mNext;
        }
    }
}

MeTrack* RhythmSystem::sameMe(const MeTrack* pTrack, u32 meId) {
    for (MePlayer* player = mFirstMe; player != nullptr; player = player->mNext) {
        if (player->mRoot != pTrack && player->mRoot->mMeId == meId) {
            return player->mRoot;
        }
    }
    return nullptr;
}

void RhythmSystem::unlist(MePlayer* pPlayer) {
    for (MePlayer** link = &mFirstMe; *link != nullptr; link = &(*link)->mNext) {
        if (*link == pPlayer) {
            *link = pPlayer->mNext;
            pPlayer->mNext = nullptr;
            pPlayer->mListed = false;
            return;
        }
    }
}

// The player's memory goes back once the game has let it go, it plays no more, and no note reads it.
void RhythmSystem::tryRelease(MePlayer* pPlayer) {
    if (!pPlayer->mReleaseAsked || pPlayer->mListed || pPlayer->mArena.mHolder.mHeld != 0 || pPlayer->mReleased) {
        return;
    }
    mEngine->voices().forgetOwners(pPlayer->mArena.begin(), pPlayer->mArena.end());
    pPlayer->mReleased = true;
}

void RhythmSystem::apply(const SoundCommand& rCommand) {
    MePlayer* const player = static_cast< MePlayer* >(rCommand.mTarget);
    RhythmSeqParser* const parser = rCommand.mValue < mParserCount ? &mParsers[rCommand.mValue] : nullptr;
    switch (rCommand.mKind) {
    case SOUND_ME_START: {
        const MeStartCommand& rStart = rCommand.payload< MeStartCommand >();
        MeTrack* const root = player->mRoot;
        root->setSeqData(rStart.mData, rStart.mStart);
        root->setSound(rStart.mSound);
        // The registers start at 60 with the first phrase ever played.
        if (!mMeStarted) {
            for (u32 i = 0; i < 256; i++) {
                mRegs[i] = 60;
            }
            mMeStarted = true;
        }
        if (!player->mListed) {
            player->mNext = mFirstMe;
            mFirstMe = player;
            player->mListed = true;
        }
        root->setStatus(MeTrack::STATUS_RUN);
        break;
    }
    case SOUND_ME_PARAMS:
        player->mRoot->setSound(rCommand.payload< SoundParams >());
        player->mRoot->gframeProc();
        break;
    case SOUND_ME_STOP_AT_NOTE_END:
        player->mRoot->stopAtCurrentNoteEnd();
        break;
    case SOUND_ME_STOP:
        unlist(player);
        player->mRoot->stopSeq();
        break;
    case SOUND_ME_PAUSE:
        player->mRoot->setPauseFlag(rCommand.mArg != 0);
        break;
    case SOUND_ME_RELEASE:
        player->mReleaseAsked = true;
        unlist(player);
        player->mRoot->close();
        tryRelease(player);
        break;
    case SOUND_RHYTHM_INIT:
        if (parser != nullptr) {
            parser->init();
        }
        break;
    case SOUND_RHYTHM_ACTIVATE:
        if (parser != nullptr) {
            parser->mIsActive = true;
        }
        break;
    case SOUND_RHYTHM_USE:
        for (u32 i = 0; i < mParserCount; i++) {
            mParsers[i].mIsUsed = i == rCommand.mValue;
            if (mParsers[i].mIsUsed) {
                mParsers[i].flushChordScale();
            }
        }
        publishChord();
        break;
    case SOUND_TEMPO_INFO:
        if (parser != nullptr) {
            const TempoInfoCommand& rInfo = rCommand.payload< TempoInfoCommand >();
            parser->mTempoAdjuster.setInfo(rInfo.mBeatMul, rInfo.mIntroBeats, rInfo.mLoopBeats, rInfo.mLoopStartSamples, rInfo.mLoopEndSamples);
            parser->mTempoAdjuster.setStream(rInfo.mStream);
        }
        break;
    case SOUND_TEMPO_REJECT:
        if (parser != nullptr) {
            parser->mTempoAdjuster.reject();
        }
        break;
    case SOUND_CHORD_LOAD:
        mChord.load(rCommand.payload< const void* >(), rCommand.mArg != 0);
        publishChord();
        break;
    case SOUND_CHORD_INVALIDATE:
        mChord.invalidate();
        publishChord();
        break;
    default:
        break;
    }
}
