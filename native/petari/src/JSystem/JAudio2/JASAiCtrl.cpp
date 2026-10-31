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
// the frame (the host adds the movie's sound).
extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // Frames the clock has asked for and the driver has not mixed: the audio event only says that there are some, so
    // a late audio thread catches up rather than losing the clock's time.
    u32 sFramesAsked;
    const u32 cMaxCatchUp = 4;
}  // namespace

extern "C" NEBULA_GAME_API void nebula_game_audio_tick(void) {
    __atomic_fetch_add(&sFramesAsked, 1u, __ATOMIC_RELAXED);
    nebula_raise_event(NEBULA_EVENT_AUDIO);
}

u32 JASDriver::sOutputRate;

f32 JASDriver::sDacRate = 32028.5f;

u32 JASDriver::sSubFrames = 0x00000007;

volatile u32 JASDriver::sSubFrameCounter;

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

// The frames the clock asked for (at most cMaxCatchUp; the rest are dropped): each one's subframes, then the frame to
// the output.
void JASDriver::updateDac() {
    u32 frames = __atomic_exchange_n(&sFramesAsked, 0u, __ATOMIC_RELAXED);
    if (frames > cMaxCatchUp) {
        frames = cMaxCatchUp;
    }
    for (u32 frame = 0; frame < frames; frame++) {
        mixFrame();
    }
}

void JASDriver::mixFrame() {
    for (u32 i = 0; i < getSubFrames(); i++) {
        updateDSP();
    }
    const NebulaHostServices* const host = nebula_host();
    host->audio_frame(host->user);
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
