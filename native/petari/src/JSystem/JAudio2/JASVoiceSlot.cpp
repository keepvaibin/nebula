#include "JSystem/JAudio2/JASVoiceSlot.hpp"
#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JAudio2/JASStream.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"

#include <nebula_game_api.h>

static const s32 MAX_DSP_CHANNELS = 64;

JASVoiceSlot* JASVoiceSlot::sSlots;

JASVoiceSlot::JASVoiceSlot()
    : mStatus(STATUS_INACTIVE), mPriority(-1), mFlags(0), field_0xc(0), mCallback(NULL), mCallbackData(NULL), mChannel(NULL) {
}

void JASVoiceSlot::free() {
    mCallback = NULL;
    mCallbackData = NULL;
}

void JASVoiceSlot::start() {
    mFlags |= 1;
}

void JASVoiceSlot::drop() {
    if (mCallback != NULL) {
        mCallback(CB_DROP, NULL, mCallbackData);
    }
    mCallback = NULL;
    mCallbackData = NULL;
    mPriority = -1;
    mFlags &= ~1;
    if (mStatus == STATUS_ACTIVE) {
        mFlags |= 2;
        mStatus = STATUS_DROP;
    }
}

void JASVoiceSlot::initAll() {
    sSlots = new (JASDram, 0x20) JASVoiceSlot[MAX_DSP_CHANNELS];
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        sSlots[i].mChannel = JASVoice::getVoice(i);
    }
}

JASVoiceSlot* JASVoiceSlot::alloc(u8 i_priority, Callback i_callback, void* i_callbackData) {
    JASVoiceSlot* channel = getLowestChannel(i_priority);
    if (channel == NULL) {
        return NULL;
    }
    channel->drop();
    channel->mPriority = i_priority;
    channel->field_0xc = 0;
    channel->mCallback = i_callback;
    channel->mCallbackData = i_callbackData;
    return channel;
}

JASVoiceSlot* JASVoiceSlot::allocForce(u8 i_priority, Callback i_callback, void* i_callbackData) {
    JASVoiceSlot* channel = getLowestChannel(i_priority);
    if (channel == NULL) {
        return NULL;
    }
    channel->mStatus = STATUS_INACTIVE;
    channel->drop();
    channel->mPriority = i_priority;
    channel->field_0xc = 0;
    channel->mCallback = i_callback;
    channel->mCallbackData = i_callbackData;
    return channel;
}

void JASVoiceSlot::setPriority(u8 i_priority) {
    mPriority = i_priority;
}

JASVoiceSlot* JASVoiceSlot::getLowestChannel(int i_priority) {
    s16 best_priority = 0xff;
    int best_index = -1;
    int best_unknown = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        JASVoiceSlot* channel = &sSlots[i];
        s16 priority = channel->mPriority;
        if (priority < 0) {
            return &sSlots[i];
        }
        if (priority <= i_priority && priority <= best_priority) {
            if (priority != best_priority || channel->field_0xc > best_unknown) {
                best_unknown = channel->field_0xc;
                best_index = i;
                best_priority = priority;
            }
        }
    }
    if (best_index < 0) {
        return NULL;
    }
    return &sSlots[best_index];
}

JASVoiceSlot* JASVoiceSlot::getLowestActiveChannel() {
    s16 best_priority = 0xff;
    int best_index = -1;
    int best_unknown = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        JASVoiceSlot* channel = &sSlots[i];
        if (channel->mStatus == STATUS_ACTIVE) {
            s16 priority = channel->mPriority;
            if (priority < 0x7f && priority <= best_priority) {
                if (priority != best_priority || channel->field_0xc > best_unknown) {
                    best_unknown = channel->field_0xc;
                    best_index = i;
                    best_priority = priority;
                }
            }
        }
    }
    if (best_index < 0) {
        return NULL;
    }
    return &sSlots[best_index];
}

void JASVoiceSlot::updateProc() {
    int ret = 0;
    if (mChannel->isFinish()) {
        mFlags &= ~2;
        if (mStatus == STATUS_ACTIVE) {
            if (mCallback != NULL) {
                ret = mCallback(CB_STOP, NULL, mCallbackData);
            } else {
                ret = -1;
            }
            if (ret < 0) {
                mPriority = -1;
            }
        }
        mStatus = STATUS_INACTIVE;
        mChannel->replyFinishRequest();
    } else if (mFlags & 2) {
        mFlags &= ~2;
        mChannel->forceStop();
    } else if (mStatus != STATUS_DROP) {
        if ((mFlags & 1) && mStatus == STATUS_INACTIVE) {
            mFlags &= ~1;
            mStatus = STATUS_ACTIVE;
            mChannel->init();
            if (mCallback != NULL) {
                ret = mCallback(CB_START, mChannel, mCallbackData);
            } else {
                ret = 0;
            }
            if (ret < 0) {
                mStatus = STATUS_INACTIVE;
                if (mCallback != NULL) {
                    ret = mCallback(CB_STOP, NULL, mCallbackData);
                } else {
                    ret = -1;
                }
                if (ret < 0) {
                    mPriority = -1;
                }
            } else {
                mChannel->playStart();
                nameSound();
            }
        } else if (mStatus != STATUS_INACTIVE) {
            if (mCallback != NULL) {
                ret = mCallback(CB_PLAY, mChannel, mCallbackData);
            } else {
                ret = 0;
            }
            if (ret < 0) {
                mStatus = STATUS_INACTIVE;
                if (mCallback != NULL) {
                    ret = mCallback(CB_STOP, NULL, mCallbackData);
                } else {
                    ret = -1;
                }
                if (ret < 0) {
                    mPriority = -1;
                }
                mChannel->playStop();
            } else {
                field_0xc++;
                if (mChannel->sound_route == 0) {
                    nameSound();
                }
            }
        }
    }
}

// The voices' updates, then the host mixes the subframe.
void JASVoiceSlot::updateAll() {
    for (u32 i = 0; i < MAX_DSP_CHANNELS; i++) {
        sSlots[i].updateProc();
    }
    JASVoice::mixSubframe();
}

int JASVoiceSlot::killActiveChannel() {
    JASVoiceSlot* channel = getLowestActiveChannel();
    if (channel == NULL) {
        return 0;
    } else {
        channel->drop();
        return 1;
    }
}

JASVoiceSlot* JASVoiceSlot::getHandle(u32 i_index) {
    return &sSlots[i_index];
}

u32 JASVoiceSlot::getNumUse() {
    u32 count = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        if (sSlots[i].mStatus == STATUS_ACTIVE) {
            count++;
        }
    }
    return count;
}

u32 JASVoiceSlot::getNumFree() {
    u32 count = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        if (sSlots[i].mStatus == STATUS_INACTIVE) {
            count++;
        }
    }
    return count;
}

u32 JASVoiceSlot::getNumBreak() {
    u32 count = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        if (sSlots[i].mStatus == STATUS_DROP) {
            count++;
        }
    }
    return count;
}

namespace {
uint32_t owner_id(const JAISound* sound) {
    return sound != nullptr ? static_cast<uint32_t>(sound->getID()) : 0u;
}

// The sound playing on a voice, for the host's volume categories (music, voices, effects, ambience, cutscenes), from
// the game's own objects, each step checked by type: the voice's JASChannel is the one whose update callback it
// carries and which points back at it; the JASChannel's callback says what its data is (a track's channel manager or a
// stream); the track's root (or the stream) names the sound that owns it (JAISe, JAISeq and JAIStream set it when
// built). Every object it reads lives in a pool or inside a sound, so a sound ending meanwhile leaves a stale name,
// never a bad read.
uint32_t voice_owner(JASVoiceSlot& dsp, uint32_t* route) {
    *route = 0u;
    if (dsp.mCallback != JASChannel::voiceUpdateCallback) {
        return 0u;
    }
    const auto* voice = static_cast<const JASChannel*>(dsp.mCallbackData);
    if (voice == nullptr || voice->mSlot != &dsp) {
        return 0u;
    }
    if (voice->mCallback == JASTrack::channelUpdateCallback) {
        const auto* manager = static_cast<const JASTrack::TChannelMgr*>(voice->mCallbackData);
        const JASTrack* track = manager != nullptr ? manager->mTrack : nullptr;
        for (int depth = 0; track != nullptr && depth < 16; ++depth) {
            if (track->mNebulaOwner != nullptr) {
                *route = track->mNebulaOwnerRoute;
                return owner_id(track->mNebulaOwner);
            }
            track = track->mParent != track ? track->mParent : nullptr;
        }
        return 0u;
    }
    if (voice->mCallback == JASStream::channelCallback) {
        const auto* stream = static_cast<const JASStream*>(voice->mCallbackData);
        if (stream != nullptr && stream->mNebulaOwner != nullptr) {
            *route = 3u;
            return owner_id(stream->mNebulaOwner);
        }
    }
    return 0u;
}
}  // namespace

// Names the voice's sound on the record once the channel's callback has run (its owner may link only then).
void JASVoiceSlot::nameSound() {
    if (mStatus != STATUS_ACTIVE || mChannel == NULL) {
        return;
    }
    uint32_t route = 0u;
    mChannel->sound_id = voice_owner(*this, &route);
    mChannel->sound_route = static_cast< u16 >(route);
}
