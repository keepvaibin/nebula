#include "Game/Sound/MeLink.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/SoundMemory.hpp"

SoundCommand& MeLink::next(u16 kind) {
    return SoundEngine::get()->ring().next(kind, mPlayer);
}

u64 MeLink::send() {
    return SoundEngine::get()->ring().send();
}

// Running from the start.
void MeLink::start(u32 tracks, const u8* pData, u32 start, const SoundParams& rSound) {
    mMemory = SoundMemory::alloc(MePlayer::bytes(tracks));
    mPlayer = MePlayer::create(mMemory, tracks);
    mStart = start;
    MeStartCommand command;
    command.mData = pData;
    command.mStart = start;
    command.mSound = rSound;
    next(SOUND_ME_START).payload< MeStartCommand >() = command;
    mStatus = MeTrack::STATUS_RUN;
    mStatusSerial = send();
}

void MeLink::frame(const SoundParams& rSound) {
    SoundCommand& rCommand = next(SOUND_ME_PARAMS);
    rCommand.mArg = 1;
    rCommand.payload< SoundParams >() = rSound;
    send();
}

void MeLink::stopAtNoteEnd() {
    next(SOUND_ME_STOP_AT_NOTE_END);
    send();
}

// Free at once.
void MeLink::stop() {
    next(SOUND_ME_STOP);
    mStatus = MeTrack::STATUS_FREE;
    mStatusSerial = send();
}

void MeLink::pause(bool pause) {
    SoundCommand& rCommand = next(SOUND_ME_PAUSE);
    rCommand.mArg = pause ? 1 : 0;
    send();
}

void MeLink::release() {
    if (mPlayer == nullptr) {
        return;
    }
    next(SOUND_ME_RELEASE);
    send();
    SoundMemory::retire(mMemory, mPlayer->mReleased);
    mPlayer = nullptr;
    mMemory = nullptr;
}

s32 MeLink::status() const {
    if (mPlayer == nullptr) {
        return MeTrack::STATUS_FREE;
    }
    return SoundEngine::get()->applied() >= mStatusSerial ? static_cast< s32 >(mPlayer->mStatus) : mStatus;
}
