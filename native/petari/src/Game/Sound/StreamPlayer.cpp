#include "Game/Sound/StreamPlayer.hpp"
#include <cstring>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_prepare_f1, "prepare.f1", 127.0f);

namespace {
    const SeqPoint cReleaseTable[2] = {
        {0x0000, 0x0002, 0x0000},
        {0x000F, 0x0000, 0x0000},
    };
    const OscData cStreamEnv = {Oscillator::TARGET_VOLUME, 1.0f, nullptr, cReleaseTable, 1.0f, 0.0f};
    // A stream's samples stay mapped while it plays.
    NEBULA_TUNABLE(u32, cAlwaysLive, 1);
}  // namespace

void StreamPlayer::init() {
    mPaused = false;
    mShownPaused = false;
    mLoaded = false;
    mArmed = false;
    mVolume = 1.0f;
    mPitch = 1.0f;
    for (u32 i = 0; i < cChannels; i++) {
        mChannelVolume[i] = 1.0f;
        mChannelPan[i] = cNum_init_f1;
        mChannelFxMix[i] = 0.0f;
        mChannelDolby[i] = 0.0f;
    }
    mMixConfig[0] = 0xffff;
    mConsumed = static_cast< u32 >(mConsumed) + mMessageCount;
    mMessageHead = 0;
    mMessageCount = 0;
}

void StreamPlayer::prepare(SoundEngine* pEngine, const u8* pData, u32 ownerSound) {
    mEngine = pEngine;
    mOwnerSound = ownerSound;
    std::memcpy(&mHeader, pData, sizeof(mHeader));
    mData = pData;
    mVolume = mHeader.volume / cNum_prepare_f1;
    mLoaded = true;
    mArmed = true;
    mJob.mKind = SoundJob::JOB_STREAM;
    mJob.mData = this;
    mJob.mCall = runJob;
    mEngine->addJob(&mJob);
}

// StreamSound::nextMessage sends a room first whenever its sent count could reach this one.
void StreamPlayer::post(u32 message) {
    if (mMessageCount == mMessageRoom) {
        NEBULA_PANIC("a stream message past the %u its game side made room for", mMessageRoom);
    }
    mMessages[(mMessageHead + mMessageCount) % mMessageRoom] = message;
    mMessageCount++;
}

void StreamPlayer::grow(MessageRoom* pRoom) {
    u32* messages = pRoom->messages();
    for (u32 i = 0; i < mMessageCount; i++) {
        messages[i] = mMessages[(mMessageHead + i) % mMessageRoom];
    }
    if (mRoom != nullptr) {
        mRoom->mReleased = true;
    }
    mRoom = pRoom;
    mMessages = messages;
    mMessageRoom = pRoom->mCapacity;
    mMessageHead = 0;
}

void StreamPlayer::cancel() {
    mArmed = false;
    if (mJob.mListed) {
        mEngine->removeJob(&mJob);
    }
}

void StreamPlayer::release(SoundEngine* pEngine) {
    cancel();
    pEngine->voices().forgetOwners(this, this + 1);
    if (mRoom != nullptr) {
        mRoom->mReleased = true;
    }
    mReleased = true;
}

void StreamPlayer::finish() {
    if (mJob.mListed) {
        mEngine->removeJob(&mJob);
    }
    if (mArmed) {
        mArmed = false;
        mFinishes = mFinishes + 1;
    }
}

void StreamPlayer::mix(const StreamMixCommand& rMix) {
    mPitch = rMix.mPitch;
    for (u32 i = 0; i < cChannels; i++) {
        if (rMix.mChannel == 0xff || rMix.mChannel == i) {
            mChannelVolume[i] = rMix.mVolume;
            mChannelPan[i] = rMix.mPan;
            mChannelFxMix[i] = rMix.mFxMix;
            mChannelDolby[i] = rMix.mDolby;
        }
    }
}

void StreamPlayer::runJob(void* pSelf) {
    static_cast< StreamPlayer* >(pSelf)->process();
}

void StreamPlayer::noteEvent(u32 event, SoundNote* pNote, NebulaVoice* pVoice, StreamPlayer* pStream) {
    pStream->updateChannel(event, pNote, pVoice);
}

void StreamPlayer::updateChannel(u32 event, SoundNote* pNote, NebulaVoice* pVoice) {
    switch (event) {
    case NOTE_START:
        if (mUpdateChannel == nullptr) {
            mUpdateChannel = pNote;
            mPosition = 0;
            mLoopCount = 0;
        }
        break;
    case NOTE_PLAY:
        if (pVoice->starting == 0 && pNote == mUpdateChannel) {
            const u32 position = pVoice->address / sizeof(s16);
            if (position < mPosition && mHeader.loop != 0) {
                mLoopCount = mLoopCount + 1;
            }
            mPosition = position;
        }
        break;
    case NOTE_STOP: {
        bool open = false;
        for (u32 i = 0; i < cChannels; i++) {
            if (pNote == mChannels[i]) {
                mChannels[i] = nullptr;
            } else if (mChannels[i] != nullptr) {
                open = true;
            }
        }
        if (!open) {
            finish();
        }
        break;
    }
    }
    pNote->mPause = mPaused;
}

void StreamPlayer::process() {
    if (!mLoaded) {
        return;
    }
    mConsumed = static_cast< u32 >(mConsumed) + mMessageCount;
    while (mMessageCount != 0) {
        const u32 message = mMessages[mMessageHead];
        mMessageHead = (mMessageHead + 1) % mMessageRoom;
        mMessageCount--;
        switch (message & 0xff) {
        case MESSAGE_START:
            channelStart();
            break;
        case MESSAGE_STOP:
            channelStop(static_cast< u16 >(message >> 16));
            break;
        case MESSAGE_PAUSE:
            mPaused = true;
            break;
        case MESSAGE_RESUME:
            mPaused = false;
            break;
        }
        mShownPaused = mPaused;
    }
    for (u32 i = 0; i < mHeader.channels; i++) {
        SoundNote* note = mChannels[i];
        if (note != nullptr) {
            NoteParams params;
            params.mVolume = mVolume * mChannelVolume[i];
            params.mPitch = mPitch;
            params.mPan = mChannelPan[i];
            params.mFxMix = mChannelFxMix[i];
            params.mDolby = mChannelDolby[i];
            note->mParams = params;
        }
    }
}

void StreamPlayer::channelStart() {
    for (u32 i = 0; i < mHeader.channels; i++) {
        SoundNote prepared;
        prepared.mOwner = OWNER_STREAM;
        prepared.mOwnerData = this;
        prepared.mPriority = 0x7f7f;
        for (u32 j = 0; j < sizeof(prepared.mMixConfig) / sizeof(prepared.mMixConfig[0]); j++) {
            prepared.mMixConfig[j] = mMixConfig[j];
        }
        prepared.mSourceRate = static_cast< f32 >(mHeader.rate);
        prepared.mOsc[0].initStart(&cStreamEnv);
        prepared.mFormat = SoundNote::FORMAT_PCM16;
        prepared.mLoop = mHeader.loop != 0 ? 0xffffffffu : 0u;
        prepared.mLoopStart = mHeader.loop_start;
        prepared.mLoopEnd = mHeader.loop_end;
        prepared.mSampleCount = mHeader.loop_end;
        prepared.mWaveLive = &cAlwaysLive;
        prepared.mWave = mData + sizeof(Header) + i * static_cast< size_t >(mHeader.samples) * sizeof(s16);
        prepared.mWaveEnd = prepared.mWave + static_cast< size_t >(mHeader.samples) * sizeof(s16);
        prepared.mKind = SoundNote::KIND_WAVE;
        mChannels[i] = mEngine->voices().play(prepared, true);
    }
    mUpdateChannel = nullptr;
}

void StreamPlayer::channelStop(u16 directRelease) {
    for (u32 i = 0; i < mHeader.channels; i++) {
        SoundNote* note = mChannels[i];
        if (note != nullptr) {
            mEngine->voices().releaseNote(note, directRelease);
        }
    }
}
