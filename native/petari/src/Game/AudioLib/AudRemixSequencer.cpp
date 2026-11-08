#include "Game/AudioLib/AudRemixSequencer.hpp"
#include "Game/AudioLib/AudRemixMgr.hpp"
#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundHandles.hpp"

namespace {
    const char* const cRemixNoteTrackSeId[] = {
        "SE_RS_REMIX_NOTE_GET_TRK0",  "SE_RS_REMIX_NOTE_GET_TRK1",  "SE_RS_REMIX_NOTE_GET_TRK2",  "SE_RS_REMIX_NOTE_GET_TRK3",
        "SE_RS_REMIX_NOTE_GET_TRK4",  "SE_RS_REMIX_NOTE_GET_TRK5",  "SE_RS_REMIX_NOTE_GET_TRK6",  "SE_RS_REMIX_NOTE_GET_TRK7",
        "SE_RS_REMIX_NOTE_GET_TRK8",  "SE_RS_REMIX_NOTE_GET_TRK9",  "SE_RS_REMIX_NOTE_GET_TRK10", "SE_RS_REMIX_NOTE_GET_TRK11",
        "SE_RS_REMIX_NOTE_GET_TRK12", "SE_RS_REMIX_NOTE_GET_TRK13", "SE_RS_REMIX_NOTE_GET_TRK14", "SE_RS_REMIX_NOTE_GET_TRK15",
    };
};  // namespace

AudRmxSeqNoteOnTimer::AudRmxSeqNoteOnTimer() {
    mNote = nullptr;
    mDelay = 0.0f;
    mRemaining = 0.0f;
    mTrack = nullptr;
    initData();
}

void AudRmxSeqNoteOnTimer::initData() {
    mNote = nullptr;
    mRemaining = 0.0f;
    mDelay = 0.0f;
    mTrack = nullptr;
}

void AudRmxSeqNoteOnTimer::setData(const RemixNoteTrackData* trackData, const RemixNoteData* data) {
    mTrack = trackData;
    mNote = data;
    mDelay = data->mStartTick;
    mRemaining = mDelay + data->mLength;
}

bool AudRmxSeqNoteOnTimer::update(f32 ticks) {
    if (mRemaining <= 0.0f) {
        initData();
        return false;
    }

    mRemaining -= ticks;

    if (mDelay <= 0.0f) {
        if (mTrack != nullptr && mNote != nullptr) {
            AudRemixSequencer* pSeq = AudWrap::getRemixSequencer();
            SoundID id = getFreeSeID();
            SoundHandle* pHandle = AudWrap::getRemixSeqObject()->startSound(id);
            AudWrap::getSystemSeObject()->writePort(pHandle, 0xc, mTrack->mTrackNo);
            AudWrap::getSystemSeObject()->writePort(pHandle, 0xb, mNote->mNoteNo);
            AudWrap::getSystemSeObject()->writePort(pHandle, 0xf, mNote->mVelocity);

            f32 tempo = pSeq->mTempo;
            u16 length = (s32)((mNote->mLength * 120.0f) / tempo);
            if (length < 1) {
                length = 1;
            } else if (length > 1440) {
                length = 1440;
            }
            AudWrap::getSystemSeObject()->writePort(pHandle, 0xe, length);
            mTrack = nullptr;
            mNote = nullptr;
        }
        return true;
    } else {
        mDelay -= ticks;
        return false;
    }
}

SoundID AudRmxSeqNoteOnTimer::getFreeSeID() {
    for (u32 i = 0; i < ARRAY_SIZE(::cRemixNoteTrackSeId); i++) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(::cRemixNoteTrackSeId[i]);
        SoundHandle* handle = AudWrap::getRemixSeqObject()->getHandleSoundID(id);
        if (handle == nullptr) {
            return id;
        }
    }

    SoundID id;
    id.setAnonymous();

    return id;
}

AudRemixSequencer::AudRemixSequencer() : mTimers(32) {
    mTempo = 120.0f;
    mTicksPerFrame = 0.0f;
    initNoteOnBuff();
    setTempo(120.0f);
}

void AudRemixSequencer::initNoteOnBuff() {
    for (int i = 0; i < mTimers.size(); i++) {
        mTimers[i].initData();
    }
}

void AudRemixSequencer::update() {
    for (int i = 0; i < mTimers.size(); i++) {
        bool isUpdate = (mTimers[i].mRemaining > 0.0f);
        if (isUpdate) {
            mTimers[i].update(mTicksPerFrame);
        }
    }
}

// tempo in beats a minute; 48 ticks a beat, 3600 frames a minute.
void AudRemixSequencer::setTempo(f32 tempo) {
    mTempo = tempo;
    mTicksPerFrame = 48.0f * tempo / 3600.0f;
}

AudRmxSeqNoteOnTimer* AudRemixSequencer::newNoteOnTimer() {
    for (int i = 0; i < mTimers.size(); i++) {
        bool isUsed = (mTimers[i].mRemaining > 0.0f);
        if (!isUsed) {
            return &mTimers[i];
        }
    }
    const int full = mTimers.size();
    mTimers.grow();
    return &mTimers[full];
}

void AudRemixSequencer::addNoteData(const RemixNoteTrackData* trackData, const RemixNoteData* data) {
    if (data->mNoteNo >= 0) {
        newNoteOnTimer()->setData(trackData, data);
    }
}
