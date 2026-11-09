#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/SoundMemory.hpp"
#include <cstring>
#include <new>

namespace {
    u32 align64(u32 bytes) {
        return (bytes + 63) & ~63u;
    }
}  // namespace

// The memory: the groups' last sent values (the game's), then the player (the audio thread's until it sets mReleased).
void PlayerLink::create(u32 tracks, u32 mgrCount, const TrackRoom& rRoom, u32 ownerSound, u8 ownerRoute) {
    const u32 head = align64(mgrCount * (sizeof(SoundParams) + sizeof(bool)));
    u8* memory = static_cast< u8* >(SoundMemory::alloc(head + SoundPlayer::bytes(tracks, mgrCount, rRoom)));
    mMemory = memory;
    mSent = reinterpret_cast< SoundParams* >(memory);
    mSentHas = reinterpret_cast< bool* >(mSent + mgrCount);
    for (u32 i = 0; i < mgrCount; i++) {
        new (&mSent[i]) SoundParams();
        mSentHas[i] = false;
    }
    mPlayer = SoundPlayer::create(memory + head, tracks, mgrCount, rRoom, ownerSound, ownerRoute);
    mMgrCount = 1;
    mStatus = SeqTrack::STATUS_FREE;
    mStatusSerial = 0;
    mPresent[0] = mPresent[1] = mPresent[2] = 0;
    mPresentSerial = 0;
}

void PlayerLink::release() {
    if (mPlayer == nullptr) {
        return;
    }
    next(SOUND_PLAYER_RELEASE);
    send();
    SoundMemory::retire(mMemory, mPlayer->mReleased);
    mPlayer = nullptr;
    mMemory = nullptr;
}

SoundCommand& PlayerLink::next(u16 kind) {
    return SoundEngine::get()->ring().next(kind, mPlayer);
}

u64 PlayerLink::send() {
    return SoundEngine::get()->ring().send();
}

s32 PlayerLink::status() const {
    if (mPlayer == nullptr) {
        return SeqTrack::STATUS_FREE;
    }
    return SoundEngine::get()->applied() >= mStatusSerial ? static_cast< s32 >(mPlayer->mStatus) : mStatus;
}

bool PlayerLink::present(const TrackPath& rPath) const {
    if (mPlayer == nullptr) {
        return false;
    }
    if (rPath.mDepth == 0) {
        return true;
    }
    const bool live = SoundEngine::get()->applied() >= mPresentSerial;
    u32 word;
    u32 bit;
    if (rPath.mDepth == 1) {
        word = 0;
        bit = rPath.mChild;
    } else {
        if (rPath.mChild >= 2) {
            return false;
        }
        word = 1 + rPath.mChild;
        bit = rPath.mGrandchild;
    }
    if (bit >= 32) {
        return false;
    }
    const u32 mask = live ? static_cast< u32 >(mPlayer->mPresent[word]) : mPresent[word];
    return (mask >> bit & 1) != 0;
}

void PlayerLink::init() {
    next(SOUND_PLAYER_INIT);
    mStatus = SeqTrack::STATUS_FREE;
    mStatusSerial = send();
    for (u32 i = 0; i < mMgrCount; i++) {
        mSentHas[i] = false;
    }
}

// The reserved tracks: two children of the root, and the sequence's numbered tracks under them, 16 to a child.
void PlayerLink::reserve(u32 tracks) {
    SoundCommand& rCommand = next(SOUND_PLAYER_RESERVE);
    rCommand.mValue = tracks;
    mPresent[0] = 3;
    for (u32 i = 0; i < 2; i++) {
        mPresent[1 + i] = 0;
        for (u32 j = 0; j < 16; j++) {
            if (i * 16 + j < tracks) {
                mPresent[1 + i] |= 1u << j;
            }
        }
    }
    mPresentSerial = send();
}

void PlayerLink::setSeq(const SeqFile* pFile, u32 entryOp) {
    SoundCommand& rCommand = next(SOUND_PLAYER_SEQ);
    SeqCommand& rSeq = rCommand.payload< SeqCommand >();
    rSeq.mFile = pFile;
    rSeq.mEntryOp = entryOp;
    send();
}

void PlayerLink::setMgrCount(u32 count) {
    SoundCommand& rCommand = next(SOUND_PLAYER_MGRS);
    rCommand.mValue = count;
    send();
    mMgrCount = count;
}

void PlayerLink::setMgrSound(u32 mgr, const SoundParams* pSound) {
    if (mgr >= mMgrCount) {
        return;
    }
    const bool has = pSound != nullptr;
    if (has == mSentHas[mgr] && (!has || std::memcmp(pSound, &mSent[mgr], sizeof(SoundParams)) == 0)) {
        return;
    }
    SoundCommand& rCommand = next(SOUND_PLAYER_SOUND);
    rCommand.mArg = static_cast< u16 >(mgr);
    MgrSoundCommand& rSound = rCommand.payload< MgrSoundCommand >();
    rSound.mHas = has;
    if (has) {
        rSound.mSound = *pSound;
        mSent[mgr] = *pSound;
    }
    mSentHas[mgr] = has;
    send();
}

void PlayerLink::start() {
    next(SOUND_PLAYER_START);
    mStatus = SeqTrack::STATUS_RUN;
    mStatusSerial = send();
}

void PlayerLink::stop() {
    next(SOUND_PLAYER_STOP);
    send();
}

void PlayerLink::setTempoRate(f32 rate) {
    SoundCommand& rCommand = next(SOUND_PLAYER_TEMPO);
    rCommand.payload< f32 >() = rate;
    send();
}

void PlayerLink::mute(const TrackPath& rPath, bool mute) {
    SoundCommand& rCommand = next(SOUND_PLAYER_MUTE);
    TrackCommand& rTrack = rCommand.payload< TrackCommand >();
    rTrack.mPath = rPath;
    rTrack.mPort = 0;
    rTrack.mValue = mute ? 1 : 0;
    send();
}

void PlayerLink::pause(bool pause) {
    SoundCommand& rCommand = next(SOUND_PLAYER_PAUSE);
    rCommand.mArg = pause ? 1 : 0;
    send();
}

void PlayerLink::writePort(const TrackPath& rPath, u32 port, u16 value) {
    SoundCommand& rCommand = next(SOUND_PLAYER_PORT);
    TrackCommand& rTrack = rCommand.payload< TrackCommand >();
    rTrack.mPath = rPath;
    rTrack.mPort = static_cast< u8 >(port);
    rTrack.mValue = value;
    send();
}

void PlayerLink::setChildIIR(u32 cutoff) {
    SoundCommand& rCommand = next(SOUND_PLAYER_CHILD_IIR);
    rCommand.mValue = cutoff;
    send();
}

void PlayerLink::setRhythm(const TrackPath& rPath, s32 parser) {
    SoundCommand& rCommand = next(SOUND_PLAYER_RHYTHM);
    TrackCommand& rTrack = rCommand.payload< TrackCommand >();
    rTrack.mPath = rPath;
    rTrack.mPort = 0;
    rTrack.mValue = parser;
    send();
}

void PlayerLink::setTrackSound(const TrackPath& rPath, const SoundParams* pSound) {
    SoundCommand& rCommand = next(SOUND_PLAYER_TRACK_SOUND);
    TrackSoundCommand& rTrack = rCommand.payload< TrackSoundCommand >();
    rTrack.mPath = rPath;
    rTrack.mHas = pSound != nullptr;
    if (pSound != nullptr) {
        rTrack.mSound = *pSound;
    }
    send();
}
