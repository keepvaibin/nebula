#include "Game/Sound/StreamSound.hpp"
#include "Game/Sound/SoundAudience.hpp"
#include "Game/Sound/SoundChild.hpp"
#include "Game/Sound/SoundMemory.hpp"
#include "Game/Sound/StreamManager.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cstring>
#include <new>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_managerMixOut__f1, "managerMixOut_.f1", 0.5f);

StreamSound::StreamSound(StreamManager* streamMgr_) {
    mPrepareState = 0;
    streamMgr = streamMgr_;
    mIsPaused = false;
    for (int i = 0; i < NUM_CHILDREN; i++) {
        children[i] = nullptr;
        mSentKnown[i] = false;
    }
    mStream = new (SoundMemory::alloc(sizeof(StreamPlayer))) StreamPlayer();
    mRoom = nullptr;
    mMessageRoom = StreamPlayer::cInlineMessages;
    mPosted = 0;
    mLoaded = false;
    mFinishBase = 0;
    mCanceled = false;
    mInitSerial = 0;
}

StreamSound::~StreamSound() {
    next(SOUND_STREAM_RELEASE);
    send();
    if (mRoom != nullptr) {
        SoundMemory::retire(mRoom, mRoom->mReleased);
    }
    SoundMemory::retire(mStream, mStream->mReleased);
}

SoundCommand& StreamSound::next(u16 kind) {
    return SoundEngine::get()->ring().next(kind, mStream);
}

// The audio thread's queue holds at most the messages sent less those it has taken (a count it only raises).
SoundCommand& StreamSound::nextMessage(u16 kind) {
    if (mPosted - static_cast< u32 >(mStream->mConsumed) >= mMessageRoom) {
        const u32 capacity = mMessageRoom * 2;
        StreamPlayer::MessageRoom* pRoom =
            new (SoundMemory::alloc(StreamPlayer::MessageRoom::bytes(capacity))) StreamPlayer::MessageRoom();
        pRoom->mCapacity = capacity;
        next(SOUND_STREAM_ROOM).payload< StreamPlayer::MessageRoom* >() = pRoom;
        send();
        if (mRoom != nullptr) {
            SoundMemory::retire(mRoom, mRoom->mReleased);
        }
        mRoom = pRoom;
        mMessageRoom = capacity;
    }
    mPosted++;
    return next(kind);
}

u64 StreamSound::send() {
    return SoundEngine::get()->ring().send();
}

void StreamSound::sendChannel(u32 channel, const SoundParams& rValues, f32 pitch) {
    StreamMixCommand mix;
    mix.mChannel = static_cast< u8 >(channel);
    mix.mVolume = rValues.mVolume;
    mix.mPan = rValues.mPan;
    mix.mFxMix = rValues.mFxMix;
    mix.mDolby = rValues.mDolby;
    mix.mPitch = pitch;
    if (channel < NUM_CHILDREN && mSentKnown[channel] && std::memcmp(&mix, &mSent[channel], sizeof(mix)) == 0) {
        return;
    }
    SoundCommand& rCommand = next(SOUND_STREAM_MIX);
    rCommand.payload< StreamMixCommand >() = mix;
    send();
    if (channel < NUM_CHILDREN) {
        mSent[channel] = mix;
        mSentKnown[channel] = true;
    } else {
        for (int i = 0; i < NUM_CHILDREN; i++) {
            mSentKnown[i] = false;
        }
    }
}

void StreamSound::initStream() {
    next(SOUND_STREAM_INIT);
    mInitSerial = send();
    mLoaded = false;
    mCanceled = false;
    for (int i = 0; i < NUM_CHILDREN; i++) {
        mSentKnown[i] = false;
    }
}

// The file is mapped: it is there now or never, and loaded as it is prepared.
bool StreamSound::prepareStream() {
    GameAssets::File file;
    if (!GameAssets::open(mStreamFileEntry, &file)) {
        return false;
    }
    const u8* const data = static_cast< const u8* >(GameAssets::map(mStreamFileEntry));
    if (data == nullptr) {
        NEBULA_PANIC("a stream of %u bytes cannot be mapped", file.mLength);
    }
    u32 tag;
    std::memcpy(&tag, data, sizeof(tag));
    if (tag != StreamPlayer::cTag) {
        NEBULA_PANIC("a stream not cooked by this version of the importer");
    }
    SoundCommand& rCommand = next(SOUND_STREAM_PREPARE);
    rCommand.payload< const u8* >() = data;
    rCommand.mValue = (u32)getID();
    send();
    mLoaded = true;
    return true;
}

bool StreamSound::finished() const {
    return mCanceled || static_cast< u32 >(mStream->mFinishes) != mFinishBase;
}

bool StreamSound::isPaused() const {
    return SoundEngine::get()->applied() >= mInitSerial ? static_cast< bool >(mStream->mShownPaused) : false;
}

void StreamSound::startID_(SoundID id, s32 streamFileEntry, const Vec3f* posPtr, SoundAudience* audience, int category) {
    mCategory = category;
    mStreamFileEntry = streamFileEntry;
    startSound_(id, posPtr, audience);
    mPrepareState = 0;
}

bool StreamSound::prepare_prepareStream_() {
    switch (mPrepareState) {
    case 0:
        initStream();
        mPrepareState = 1;
        mPrepareCount = 0;
        break;
    case 1:
        if (mAudible != nullptr) {
            const SoundParams& rSoundParams = mAudible->getOuterParams(0);
            sendChannel(0xff, rSoundParams, rSoundParams.mPitch);
        }

        mLoaded = false;

        if (prepareStream()) {
            mPrepareState = 2;
        }
        break;
    case 2:
        if (mLoaded) {
            mLoaded = false;
            mPrepareState = 3;
        } else {
            increasePrepareCount_();
        }
        break;
    case 3:
        return true;
    case 4:
        break;
    }

    return false;
}

void StreamSound::prepare_() {
    if (finished()) {
        return;
    }

    switch (mStatus.getState()) {
    case SoundStatus::State_DEAD:
        break;
    case SoundStatus::State_PREPARE:
        if (prepare_prepareStream_()) {
            mStatus.setPlaying();
            prepare_startStream_();
        }
        break;
    case SoundStatus::State_LOCK_PREPARE:
        if (prepare_prepareStream_()) {
            mStatus.setReadyLocked();
        }
        break;
    case SoundStatus::State_READY:
        mStatus.setPlaying();
        prepare_startStream_();
        break;
    case SoundStatus::State_PLAYING:
        break;
    }
}

void StreamSound::prepare_startStream_() {
    nextMessage(SOUND_STREAM_START);
    send();
    mFinishBase = mStream->mFinishes;
    mCanceled = false;
    mIsPaused = false;
    mPrepareState = 4;
}

void StreamSound::managerMixOut_(const SoundParams& inParams, SoundActivity activity) {
    SoundParams outParams;
    mParams.mixOutAll(inParams, &outParams, (mStatus.isMute() || activity.isMute()) ? 0.0f : mFader.getIntensity());

    // A stream is heard from the first listener only.
    const SoundParams* mixParams = &outParams;
    if (mAudible != nullptr && mAudience != nullptr) {
        mAudience->mixChannelOut(outParams, mAudible, 0);
        mixParams = &mAudible->getOuterParams(0);
    }

    for (int i = 0; i < NUM_CHILDREN; i++) {
        SoundParams channel;
        if (children[i] != nullptr) {
            const SoundParams& rChild = children[i]->mMove.mParams;
            channel.mVolume = rChild.mVolume * mixParams->mVolume;
            channel.mPan = (rChild.mPan + mixParams->mPan) - cNum_managerMixOut__f1;
            channel.mFxMix = rChild.mFxMix + mixParams->mFxMix;
            channel.mDolby = rChild.mDolby + mixParams->mDolby;
        } else {
            channel.mVolume = mixParams->mVolume;
            channel.mPan = mixParams->mPan;
            channel.mFxMix = mixParams->mFxMix;
            channel.mDolby = mixParams->mDolby;
        }
        sendChannel(static_cast< u32 >(i), channel, mixParams->mPitch);
    }

    prepare_();

    if (mPrepareState == 4) {
        const bool isPaused = mStatus.isPaused() || activity.isPaused();
        if (isPaused != mIsPaused) {
            SoundCommand& rCommand = nextMessage(SOUND_STREAM_PAUSE);
            rCommand.mArg = isPaused ? 1 : 0;
            send();
            mIsPaused = isPaused;
        }
    }
}

void StreamSound::dieStream_() {
    dieSound_();

    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (children[i] != nullptr) {
            delete children[i];
            children[i] = nullptr;
        }
    }
}

bool StreamSound::tryDie_() {
    if (finished()) {
        dieStream_();
        return true;
    }

    switch (mPrepareState) {
    case 0:
    case 1:
        dieStream_();
        return true;
    case 2:
    case 3:
        mPrepareState = 5;
        next(SOUND_STREAM_CANCEL);
        send();
        mCanceled = true;
        break;
    case 4: {
        mPrepareState = 6;
        SoundCommand& rCommand = nextMessage(SOUND_STREAM_STOP);
        rCommand.mArg = 10;
        send();
        break;
    }
    }

    return false;
}

void StreamSound::managerCalc_() {
    if (finished()) {
        mPrepareState = 0;
        stopSound_();
    }

    if (calcSound_()) {
        for (int i = 0; i < NUM_CHILDREN; i++) {
            if (children[i] != nullptr) {
                children[i]->calc();
            }
        }
    }
}

s32 StreamSound::getNumChild() const {
    return NUM_CHILDREN;
}

SoundChild* StreamSound::getChild(int index) {
    if (children[index] == nullptr) {
        children[index] = new (SoundMemory::arena(), 0) SoundChild();
    }
    return children[index];
}

void StreamSound::releaseChild(int index) {
    if (children[index] != nullptr) {
        delete children[index];
        children[index] = nullptr;
    }
}

PlayerLink* StreamSound::getPlayer() {
    return nullptr;
}

PlayerLink* StreamSound::getChildTrack(int, TrackPath*) {
    return nullptr;
}

StreamSound* StreamSound::asStream() {
    return this;
}

SoundTempo* StreamSound::getTempoMgr() {
    return nullptr;
}
