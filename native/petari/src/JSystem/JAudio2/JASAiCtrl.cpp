#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "Game/System/NebulaAtomic.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASVoiceSlot.hpp"
#include "JSystem/JAudio2/JASVoice.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASVoiceSlot.hpp"
#include "JSystem/JAudio2/JASVoice.hpp"
#include "JSystem/JAudio2/JASLfo.hpp"

#include <nebula_game_api.h>
#include <nebula_threads.h>
#include "Game/System/GameThreads.hpp"
#include "Game/System/NebulaArena.hpp"
#include "JSystem/JAudio2/JASDriverIF.hpp"

// The host's audio clock asks for each frame (nebula_game_sound_frame, raised on the audio thread as its event): the
// driver runs the frame's subframes - commands, the voices' updates and one mix each, then the sequencers - and ends
// the frame (the host adds the movie's sound).
extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // Frames the clock has asked for and the driver has not mixed: the audio event only says that there are some, so
    // a late audio thread catches up rather than losing the clock's time.
    u32 sFramesAsked;
    const u32 cMaxCatchUp = 4;
}  // namespace

// On the host's audio clock thread.
extern "C" NEBULA_GAME_API void nebula_game_sound_frame(void) {
    __atomic_fetch_add(&sFramesAsked, 1u, __ATOMIC_RELAXED);
    GameThreads::enterAudio();
    NebulaArena* const previous = NebulaArena::currentSlot();
    NebulaArena::setCurrentSlot(NebulaArena::sSystemHeap);
    JASDriver::updateFrame();
    JASDriver::updateFrameCallback();
    NebulaArena::setCurrentSlot(previous);
    GameThreads::leaveAudio();
}

u32 JASDriver::sOutputRate;

// The sound clock's rate, samples a second: pitches and tempos are worked out from it, so it is the host's clock exactly.
f32 JASDriver::sSoundRate = 32000.0f;

u32 JASDriver::sSubFrames = 0x00000007;

NebulaAtomic< u32 > JASDriver::sSubFrameCounter;

// The driver up and the host's audio clock running.
void JASDriver::startAudio() {
    setOutputRate(OUTPUT_RATE_0);
    JASChannel::initBankDisposeMsgQueue();
    JASVoice::initAll();
    JASVoiceSlot::initAll();
    JASPoolAllocObject_MultiThreaded< JASChannel >::newMemPool(0x48);
    startClock();
}

void JASDriver::startClock() {
    nebula_host()->sound_clock(nebula_host()->user, NEBULA_AUDIO_START, getFrameSamples());
}

void JASDriver::stopClock() {
    nebula_host()->sound_clock(nebula_host()->user, NEBULA_AUDIO_STOP, 0);
}

void JASDriver::setOutputRate(JASOutputRate rate) {
    sOutputRate = rate;
    if (rate == 0) {
        sSubFrames = 7;
        sSoundRate = 32000.0f;
    } else {
        sSubFrames = 10;
        sSoundRate = 48000.0f;
    }
}

// The frames the clock asked for (at most cMaxCatchUp; the rest are dropped): each one's subframes, then the frame to
// the output.
void JASDriver::updateFrame() {
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
        updateSubFrame();
    }
    const NebulaHostServices* const host = nebula_host();
    host->voices_frame(host->user);
}

void JASDriver::updateSubFrame() {
    syncCallback();
    JASChannel::receiveBankDisposeMsg();
    JASVoiceSlot::updateAll();

    subframeCallback();
    f32 freeRun = 32028.5f / getSoundRate();
    JASLfo::updateFreeRun(freeRun);
    sSubFrameCounter++;
    if (sSubFrameDone.isWaiting()) {
        sSubFrameDone.signal();
    }
}

f32 JASDriver::getSoundRate() {
    return sSoundRate;
}

u32 JASDriver::getSubFrames() {
    return sSubFrames;
}

u32 JASDriver::getFrameSamples() {
    return sSubFrames * 0x50;
}

