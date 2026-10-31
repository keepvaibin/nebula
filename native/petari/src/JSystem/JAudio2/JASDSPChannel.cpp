#include "JSystem/JAudio2/JASDSPChannel.hpp"
#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JAudio2/JASAramStream.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"

#include <nebula_game_api.h>

static const s32 MAX_DSP_CHANNELS = 64;

JASDSPChannel* JASDSPChannel::sDspChannels;

JASDSPChannel::JASDSPChannel()
    : mStatus(STATUS_INACTIVE), mPriority(-1), mFlags(0), field_0xc(0), mCallback(NULL), mCallbackData(NULL), mChannel(NULL) {
}

void JASDSPChannel::free() {
    mCallback = NULL;
    mCallbackData = NULL;
}

void JASDSPChannel::start() {
    mFlags |= 1;
}

void JASDSPChannel::drop() {
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

void JASDSPChannel::initAll() {
    sDspChannels = new (JASDram, 0x20) JASDSPChannel[MAX_DSP_CHANNELS];
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        sDspChannels[i].mChannel = JASDsp::getDSPHandle(i);
    }
}

JASDSPChannel* JASDSPChannel::alloc(u8 i_priority, Callback i_callback, void* i_callbackData) {
    JASDSPChannel* channel = getLowestChannel(i_priority);
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

JASDSPChannel* JASDSPChannel::allocForce(u8 i_priority, Callback i_callback, void* i_callbackData) {
    JASDSPChannel* channel = getLowestChannel(i_priority);
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

void JASDSPChannel::setPriority(u8 i_priority) {
    mPriority = i_priority;
}

JASDSPChannel* JASDSPChannel::getLowestChannel(int i_priority) {
    s16 best_priority = 0xff;
    int best_index = -1;
    int best_unknown = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        JASDSPChannel* channel = &sDspChannels[i];
        s16 priority = channel->mPriority;
        if (priority < 0) {
            return &sDspChannels[i];
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
    return &sDspChannels[best_index];
}

JASDSPChannel* JASDSPChannel::getLowestActiveChannel() {
    s16 best_priority = 0xff;
    int best_index = -1;
    int best_unknown = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        JASDSPChannel* channel = &sDspChannels[i];
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
    return &sDspChannels[best_index];
}

void JASDSPChannel::updateProc() {
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
            }
        }
    }
}

// The voices' updates, then the host mixes the subframe.
void JASDSPChannel::updateAll() {
    for (u32 i = 0; i < MAX_DSP_CHANNELS; i++) {
        sDspChannels[i].updateProc();
    }
    JASDsp::mixSubframe();
}

int JASDSPChannel::killActiveChannel() {
    JASDSPChannel* channel = getLowestActiveChannel();
    if (channel == NULL) {
        return 0;
    } else {
        channel->drop();
        return 1;
    }
}

JASDSPChannel* JASDSPChannel::getHandle(u32 i_index) {
    return &sDspChannels[i_index];
}

u32 JASDSPChannel::getNumUse() {
    u32 count = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        if (sDspChannels[i].mStatus == STATUS_ACTIVE) {
            count++;
        }
    }
    return count;
}

u32 JASDSPChannel::getNumFree() {
    u32 count = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        if (sDspChannels[i].mStatus == STATUS_INACTIVE) {
            count++;
        }
    }
    return count;
}

u32 JASDSPChannel::getNumBreak() {
    u32 count = 0;
    for (int i = 0; i < MAX_DSP_CHANNELS; i++) {
        if (sDspChannels[i].mStatus == STATUS_DROP) {
            count++;
        }
    }
    return count;
}

// The sound on each DSP channel, for the host's volume categories (music, voices, effects, ambience, cutscenes), from
// the game's own objects, each step checked by type: a DSP channel's JASChannel is the one whose update callback it
// carries and which points back at it; the JASChannel's callback says what its data is (a track's channel manager or a
// stream); the track's root (or the stream) names the sound that owns it (JAISe, JAISeq and JAIStream set it when
// built). Called on the game's audio thread, which starts and ends the channels; every object it reads lives in a pool
// or inside a sound, so a sound ending meanwhile leaves a stale name, never a bad read.
namespace {
constexpr uint32_t kDspChannels = 64;

uint32_t owner_id(const JAISound* sound) {
    return sound != nullptr ? static_cast<uint32_t>(sound->getID()) : 0u;
}
}  // namespace

extern "C" NEBULA_GAME_API uint32_t nebula_game_audio_channel_owner(uint32_t channel, uint32_t* route) {
    *route = 0u;
    JASDSPChannel* const channels = JASDSPChannel::sDspChannels;
    if (channels == nullptr || channel >= kDspChannels) {
        return 0u;
    }
    JASDSPChannel& dsp = channels[channel];
    if (dsp.mCallback != JASChannel::dspUpdateCallback) {
        return 0u;
    }
    const auto* voice = static_cast<const JASChannel*>(dsp.mCallbackData);
    if (voice == nullptr || voice->mDspCh != &dsp) {
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
    if (voice->mCallback == JASAramStream::channelCallback) {
        const auto* stream = static_cast<const JASAramStream*>(voice->mCallbackData);
        if (stream != nullptr && stream->mNebulaOwner != nullptr) {
            *route = 3u;
            return owner_id(stream->mNebulaOwner);
        }
    }
    return 0u;
}
