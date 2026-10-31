#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "JSystem/JAudio2/JASAudioThread.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASCmdStack.hpp"
#include "JSystem/JAudio2/JASCriticalSection.hpp"
#include "JSystem/JAudio2/JASDSPChannel.hpp"
#include "JSystem/JAudio2/JASDspInterface.hpp"
#include "JSystem/JAudio2/JASLfo.hpp"
#include "JSystem/JAudio2/JASProbe.hpp"

#include <nebula_game_api.h>
#include <nebula_kernel.h>
#include "Game/System/NebulaKernel.hpp"

// The host's audio clock asks for each frame (nebula_game_audio_tick, raised on the audio thread as its event): the
// driver runs the frame's subframes - commands, the voices' updates and one mix each, then the sequencers - and ends
// the frame with the samples of the mix callback (the movie player's).
extern "C" const NebulaHostServices* nebula_host(void);

extern "C" NEBULA_GAME_API void nebula_game_audio_tick(void) {
    nebula_raise_event(NEBULA_EVENT_AUDIO);
}

JASDriver::MixCallback JASDriver::extMixCallback;

u32 JASDriver::sOutputRate;

JASMixMode JASDriver::sMixMode = MIX_MODE_EXTRA;

f32 JASDriver::sDacRate = 32028.5f;

u32 JASDriver::sSubFrames = 0x00000007;

volatile u32 JASDriver::sSubFrameCounter;

namespace {
    // A mix callback's samples as stereo pairs (right, left), for the modes that return another layout.
    s16 sMixPairs[10 * 80 * 2];

    const s16* mixPairs(const s16* samples, u32 count) {
        switch (JASDriver::sMixMode) {
        case MIX_MODE_MONO:
            for (u32 i = 0; i < count; i++) {
                sMixPairs[i * 2] = samples[i];
                sMixPairs[i * 2 + 1] = samples[i];
            }
            return sMixPairs;
        case MIX_MODE_MONO_WIDE:
            for (u32 i = 0; i < count; i++) {
                sMixPairs[i * 2] = samples[i];
                sMixPairs[i * 2 + 1] = JASCalc::clamp< s16, s32 >(-samples[i]);
            }
            return sMixPairs;
        case MIX_MODE_EXTRA:
            for (u32 i = 0; i < count; i++) {
                sMixPairs[i * 2] = samples[count + i];
                sMixPairs[i * 2 + 1] = samples[i];
            }
            return sMixPairs;
        case MIX_MODE_INTERLEAVE:
        default:
            return samples;
        }
    }
}  // namespace

void JASDriver::initAI(void (*callback)(void)) {
    setOutputRate(OUTPUT_RATE_0);
    JASChannel::initBankDisposeMsgQueue();
    NebulaKernel::setEventHandler(NEBULA_EVENT_AUDIO, callback);
    NebulaKernel::enableEvent(NEBULA_EVENT_AUDIO);
}

void JASDriver::startDMA() {
    nebula_host()->audio_control(nebula_host()->user, NEBULA_AUDIO_START, getFrameSamples());
}

void JASDriver::stopDMA() {
    nebula_host()->audio_control(nebula_host()->user, NEBULA_AUDIO_STOP, 0);
}

void JASDriver::setOutputRate(JASOutputRate rate) {
    sOutputRate = rate;
    if (rate == 0) {
        sSubFrames = 7;
        sDacRate = 32000.0f;
    } else {
        sSubFrames = 10;
        sDacRate = 48000.0f;
    }
    sDacRate *= 1.0008897f;
}

// One frame: its subframes, then the mix callback's samples with it to the output.
void JASDriver::updateDac() {
    for (u32 i = 0; i < getSubFrames(); i++) {
        updateDSP();
    }
    const u32 frameSamples = getFrameSamples();
    const s16* stream = nullptr;
    if (extMixCallback != nullptr) {
        JASProbe::start(5, "MOVIE-MIX");
        const s16* samples = extMixCallback(frameSamples);
        JASProbe::stop(5);
        if (samples != nullptr) {
            stream = mixPairs(samples, frameSamples);
        }
    }
    const NebulaHostServices* const host = nebula_host();
    host->audio_frame(host->user, stream, stream != nullptr ? frameSamples : 0);
}

void JASDriver::updateDSP() {
    JASProbe::start(3, "SFR-UPDATE");
    JASPortCmd::execAllCommand();
    syncCallback();
    JASChannel::receiveBankDisposeMsg();
    JASDSPChannel::updateAll();

    subframeCallback();
    f32 freeRun = 32028.5f / getDacRate();
    JASLfo::updateFreeRun(freeRun);
    JASProbe::stop(3);
    sSubFrameCounter++;
}

void JASDriver::registerMixCallback(MixCallback callback, JASMixMode mode) {
    extMixCallback = callback;
    sMixMode = mode;
}

f32 JASDriver::getDacRate() {
    return sDacRate;
}

u32 JASDriver::getSubFrames() {
    return sSubFrames;
}

u32 JASDriver::getFrameSamples() {
    return sSubFrames * 0x50;
}

u32 JASDriver::getSubFrameCounter() {
    return sSubFrameCounter;
}
