#include "Game/Speaker/SpkSound.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/SoundMemory.hpp"
#include "Game/Speaker/SpkSpeakerCtrl.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaInput.hpp"
#include <new>

namespace {
    SoundCommand& next(u16 kind, SpeakerVoice* pVoice) {
        return SoundEngine::get()->ring().next(kind, pVoice);
    }

    u64 send() {
        return SoundEngine::get()->ring().send();
    }
}  // namespace

void SpkSoundHandle::releaseSound() {
    if (!mSound) {
        return;
    }

    mSound->mSoundHandle = nullptr;
    mSound = nullptr;
}

SpkSound::SpkSound()
    : mSoundHandle(nullptr), mWaveID(-1), mPadChannel(PLAYER_1), mReleaseTime(0), mState(State_NONE), mStateSerial(0),
      mVoice(nullptr), mMemory(nullptr) {
}

SpkSound::~SpkSound() {
    releaseHandle();
    if (mVoice != nullptr) {
        next(SOUND_SPEAKER_RELEASE, mVoice);
        send();
        SoundMemory::retire(mMemory, mVoice->mReleased);
        mVoice = nullptr;
        mMemory = nullptr;
    }
}

s32 SpkSound::getState() const {
    if (mVoice == nullptr) {
        return mState;
    }
    return SoundEngine::get()->applied() >= mStateSerial ? static_cast< s32 >(mVoice->mShownState) : mState;
}

void SpkSound::setState(s32 state) {
    SoundCommand& rCommand = next(SOUND_SPEAKER_STATE, mVoice);
    rCommand.mValue = static_cast< u32 >(state);
    mState = state;
    mStateSerial = send();
}

// The stop time is in milliseconds (6 samples each); the envelope counts 40-sample blocks.
void SpkSound::stop(s32 stopTime) {
    const s32 fade = (6 * stopTime) / 40;
    SoundCommand& rCommand = next(SOUND_SPEAKER_STOP, mVoice);
    if (fade != 0) {
        rCommand.mArg = 0;
        rCommand.mValue = static_cast< u32 >(fade);
    } else {
        rCommand.mArg = 1;
        rCommand.mValue = mReleaseTime;
    }
    mState = State_STOP;
    mStateSerial = send();
}

void SpkSound::attachHandle(SpkSoundHandle* pHandle) {
    if (mSoundHandle) {
        releaseHandle();
    }

    mSoundHandle = pHandle;
    pHandle->mSound = this;
}

void SpkSound::releaseHandle() {
    if (!mSoundHandle) {
        return;
    }

    mSoundHandle->mSound = nullptr;
    mSoundHandle = nullptr;
}

// The wave in the speaker archive, read in place, and its table values.
bool SpkSound::start(s32 padChannel, s32 waveID) {
    mPadChannel = padChannel;

    SpkData* data = SoundGlobal< SpkSystem >::getInstance()->mData;
    if (!data->isValid()) {
        return false;
    }

    mWaveID = waveID;

    const SpkParameters* params = data->getSpkTable()->getParameters(waveID);
    const u16 id = params->mWaveID;
    mReleaseTime = (params->mReleaseTime * 6) / 40;

    const WaveData* wave = data->getWave()->getWaveData(id);
    SpeakerStartCommand command;
    command.mWave = wave->mWave;
    command.mSize = static_cast< s32 >(wave->mSize) / 2;
    command.mLoopStart = wave->mLoopStartPos;
    command.mLoopEnd = wave->mLoopEndPos;
    command.mBaseVolume = params->mVolume / 127.0f;

    mMemory = SoundMemory::alloc(sizeof(SpeakerVoice));
    mVoice = new (mMemory) SpeakerVoice();
    SoundCommand& rCommand = next(SOUND_SPEAKER_START, mVoice);
    rCommand.mArg = static_cast< u16 >(padChannel);
    rCommand.payload< SpeakerStartCommand >() = command;
    mState = State_LOCK;
    mStateSerial = send();
    return true;
}

void SpkSound::unlock() {
    if (getState() != State_LOCK) {
        return;
    }

    setState(State_UNLOCK);
}

void SpkSound::kill() {
    setState(State_DEAD);
}

SpkSoundHolder::SpkSoundHolder(NebulaArena* pArena) : SoundGlobal(true), mArena(pArena) {
    mSoundList = new (pArena, 0) NebulaList< SpkSound >[NebulaInputPlayers::count()];
    mPadHandles = new (pArena, 0) SpkSoundHandle[NebulaInputPlayers::count()];
}

bool SpkSoundHolder::startSound(s32 padChannel, s32 waveID, SpkSoundHandle* pHandle) {
    if (!SpkSpeakerCtrl::isEnable(padChannel)) {
        return false;
    }

    if (waveID < 0) {
        return false;
    }

    if (pHandle != nullptr) {
        SpkSound* sound = pHandle->getSound();
        if (sound != nullptr) {
            if (sound->getState() == SpkSound::State_LOCK) {
                sound->kill();
            } else {
                sound->stop(0);
            }
        }
    }

    SpkSound* sound = new (mArena, 0) SpkSound();

    if (pHandle != nullptr) {
        sound->attachHandle(pHandle);
    }

    if (!sound->start(padChannel, waveID)) {
        delete sound;
        return false;
    }

    mSoundList[padChannel].append(sound);
    return true;
}

void SpkSoundHolder::freeDeadSound(s32 padChannel) {
    for (NebulaListLink< SpkSound >* sound = mSoundList[padChannel].getFirst(); sound != mSoundList[padChannel].getEnd();) {
        NebulaListLink< SpkSound >* next = sound->getNext();
        if (sound->getObject()->getState() == SpkSound::State_DEAD) {
            mSoundList[padChannel].remove(sound);
            delete sound->getObject();
        }
        sound = next;
    }
}

// Each frame: last frame's sounds are heard from now, and the ended ones go.
void SpkSoundHolder::framework() {
    for (s32 i = 0; i < static_cast< s32 >(NebulaInputPlayers::count()); i++) {
        for (NebulaListLink< SpkSound >* sound = mSoundList[i].getFirst(); sound != mSoundList[i].getEnd(); sound = sound->getNext()) {
            if (sound->getObject()->getState() == SpkSound::State_LOCK) {
                sound->getObject()->unlock();
            }
        }
        freeDeadSound(i);
    }
}
