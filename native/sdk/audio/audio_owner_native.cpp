// The sound on each DSP channel, for the host's volume categories (music, voices, effects, ambience, cutscenes), from
// the game's own objects, each step checked by type: a DSP channel's JASChannel is the one whose update callback it
// carries and which points back at it; the JASChannel's callback says what its data is (a track's channel manager or a
// stream); the track's root (or the stream) names the sound that owns it (JAISe, JAISeq and JAIStream set it when
// built). Called on the game's audio thread, which starts and ends the channels; every object it reads lives in a pool
// or inside a sound, so a sound ending meanwhile leaves a stale name, never a bad read.
#include <nebula_game_api.h>

#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JAudio2/JASAramStream.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASDSPChannel.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"

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
