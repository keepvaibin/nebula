#include "JSystem/JAudio2/JASStream.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASVoice.hpp"
#include "JSystem/JAudio2/JASDriverIF.hpp"

#include "Game/System/NebulaArena.hpp"
#include "JSystem/JSupport/JSupport.hpp"

#include <cstdlib>
#include <cstring>

JASStream::JASStream() : mPosition(), mLoopCount(), mLoaded(), mCanceled() {
    mUpdateChannel = nullptr;
    mPaused = false;
    mData = nullptr;
    mCallback = nullptr;
    mCallbackData = nullptr;
    mHeader = Header();
    mVolume = 1.0f;
    mPitch = 1.0f;
    mNebulaOwner = nullptr;
    for (int i = 0; i < CHANNEL_MAX; i++) {
        mChannels[i] = nullptr;
        mChannelVolume[i] = 1.0f;
        mChannelPan[i] = 0.5f;
        mChannelFxMix[i] = 0.0f;
        mChannelDolby[i] = 0.0f;
    }

    for (int i = 0; i < 6; i++) {
        mMixConfig[i] = 0;
    }
}

JASStream::~JASStream() {
}

void JASStream::init(StreamCallback i_callback, void* i_callbackData) {
    mPaused = false;
    mLoaded = false;
    mCanceled = false;
    mVolume = 1.0f;
    mPitch = 1.0f;
    for (int i = 0; i < CHANNEL_MAX; i++) {
        mChannelVolume[i] = 1.0f;
        mChannelPan[i] = 0.5f;
        mChannelFxMix[i] = 0.0f;
        mChannelDolby[i] = 0.0f;
    }

    mMixConfig[0] = 0xffff;
    mCallback = i_callback;
    mCallbackData = i_callbackData;
    mCommands.init(mCommandSlots, ARRAY_SIZE(mCommandSlots));
}

bool JASStream::prepare(s32 entry) {
    if (!GameAssets::open(entry, &mFile)) {
        return false;
    }

    load();
    return JASDriver::registerSubFrameCallback(channelProcCallback, this);
}

bool JASStream::start() {
    return mCommands.send((void*)0);
}

bool JASStream::stop(u16 param_0) {
    return mCommands.send((void*)(uintptr_t)(param_0 << 0x10 | 1));
}

bool JASStream::pause(bool param_0) {
    return mCommands.send(param_0 ? (void*)2 : (void*)3);
}

bool JASStream::cancel() {
    mCanceled = true;
    finish();
    return true;
}

void JASStream::finish() {
    JASDriver::rejectCallback(channelProcCallback, this);
    // Cleared first: the callback tells the game thread it may free this stream.
    const StreamCallback callback = mCallback;
    if (callback != nullptr) {
        mCallback = nullptr;
        callback(CB_START, this, mCallbackData);
    }
}

void JASStream::load() {
    if (mCanceled) {
        return;
    }

    const u8* const data = static_cast< const u8* >(GameAssets::map(mFile.mEntry));
    if (data == nullptr) {
        NEBULA_PANIC("a stream of %u bytes cannot be loaded", mFile.mLength);
    }

    Header header;
    memcpy(&header, data, sizeof(header));
    if (header.tag != 'NSTR') {
        NEBULA_PANIC("a stream not cooked by this version of the importer");
    }

    mData = data;
    mHeader = header;
    mVolume = header.volume / 127.0f;
    mLoaded = true;
    if (mCallback != nullptr) {
        mCallback(CB_STOP, this, mCallbackData);
    }
}

s32 JASStream::channelProcCallback(void* i_this) {
    return static_cast< JASStream* >(i_this)->channelProc();
}

void JASStream::channelCallback(u32 i_callbackType, JASChannel* i_channel, JASVoice* i_dspChannel, void* i_this) {
    static_cast< JASStream* >(i_this)->updateChannel(i_callbackType, i_channel, i_dspChannel);
}

void JASStream::updateChannel(u32 i_callbackType, JASChannel* i_channel, JASVoice* i_dspChannel) {
    switch (i_callbackType) {
    case JASChannel::CB_START:
        if (mUpdateChannel == nullptr) {
            mUpdateChannel = i_channel;
            mPosition = 0;
            mLoopCount = 0;
        }
        break;
    case JASChannel::CB_PLAY:
        if (i_dspChannel->starting == 0 && i_channel == mUpdateChannel) {
            const u32 position = i_dspChannel->address / sizeof(s16);
            if (position < mPosition && mHeader.loop != 0) {
                mLoopCount = mLoopCount + 1;
            }
            mPosition = position;
        }
        break;
    case JASChannel::CB_STOP: {
        bool open_channel = false;
        for (int i = 0; i < CHANNEL_MAX; i++) {
            if (i_channel == mChannels[i]) {
                mChannels[i] = nullptr;
            } else if (mChannels[i] != nullptr) {
                open_channel = true;
            }
        }

        if (!open_channel) {
            mCanceled = true;
            finish();
        }
        break;
    }
    }

    i_channel->setPauseFlag(mPaused);
}

s32 JASStream::channelProc() {
    if (!mLoaded) {
        return 0;
    }

    void* msg;
    while (mCommands.receive(&msg)) {
        switch ((u32)(uintptr_t)msg & 0xff) {
        case 0:
            channelStart();
            break;
        case 1:
            channelStop(JSUHiHalf((u32)(uintptr_t)msg));
            break;
        case 2:
            mPaused = true;
            break;
        case 3:
            mPaused = false;
            break;
        }
    }

    for (u32 i = 0; i < mHeader.channels; i++) {
        JASChannel* channel = mChannels[i];
        if (channel != nullptr) {
            JASChannelParams params;
            params.mVolume = mVolume * mChannelVolume[i];
            params.mPitch = mPitch;
            params.mPan = mChannelPan[i];
            params.mFxMix = mChannelFxMix[i];
            params.mDolby = mChannelDolby[i];
            channel->setParams(params);
        }
    }

    return 0;
}

static JASOscillator::Point const OSC_RELEASE_TABLE[2] = {
    {0x0000, 0x0002, 0x0000},
    {0x000F, 0x0000, 0x0000},
};

static JASOscillator::Data const OSC_ENV = {0, 1.0f, nullptr, OSC_RELEASE_TABLE, 1.0f, 0.0f};

void JASStream::channelStart() {
    for (u32 i = 0; i < mHeader.channels; i++) {
        JASWaveInfo wave_info;
        wave_info.mFormat = 3;  // PCM16
        wave_info.mLoopFlags = mHeader.loop != 0 ? -1 : 0;
        wave_info.mSampleLoopStart = mHeader.loop_start;
        wave_info.mSampleLoopEnd = mHeader.loop_end;
        wave_info.mSampleCount = mHeader.loop_end;
        static u32 const one = 1;
        wave_info._24 = &one;
        JASChannel* jc = new JASChannel(channelCallback, this);
        jc->setPriority(0x7f7f);
        for (u32 j = 0; j < 6; j++) {
            jc->setMixConfig(j, mMixConfig[j]);
        }

        jc->setSourceRate(mHeader.rate);
        jc->setOscInit(0, &OSC_ENV);
        jc->mWaveInfo = wave_info;
        jc->mWavePtr = mData + sizeof(Header) + i * static_cast< size_t >(mHeader.samples) * sizeof(s16);
        jc->mWaveLimit = jc->mWavePtr + static_cast< size_t >(mHeader.samples) * sizeof(s16);
        jc->mChannelType = JASChannel::CH_WAVE;
        jc->playForce();
        mChannels[i] = jc;
    }

    mUpdateChannel = nullptr;
}

void JASStream::channelStop(u16 i_directRelease) {
    for (u32 i = 0; i < mHeader.channels; i++) {
        JASChannel* channel = mChannels[i];
        if (channel != nullptr) {
            channel->release(i_directRelease);
        }
    }
}
