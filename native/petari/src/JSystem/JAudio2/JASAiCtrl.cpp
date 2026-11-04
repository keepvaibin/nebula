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

// The host's audio clock asks for each tick, 2.5 ms of sound (nebula_game_sound_frame, on the audio thread): commands, the
// voices' updates and one mix, then the sequencers, then the block goes to the host (which adds the movie's sound). A sound
// asked for starts in the next block.
extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // Blocks the clock has asked for and the driver has not mixed: a late audio thread catches up rather than losing the
    // clock's time.
    u32 sFramesAsked;
    const u32 cMaxCatchUp = 28;
}  // namespace

// On the host's audio clock thread.
extern "C" NEBULA_GAME_API void nebula_game_sound_frame(void) {
    __atomic_fetch_add(&sFramesAsked, 1u, __ATOMIC_RELAXED);
    GameThreads::enterAudio();
    NebulaArena* const previous = NebulaArena::currentSlot();
    NebulaArena::setCurrentSlot(NebulaArena::sSystemHeap);
    JASDriver::updateFrame();
    NebulaArena::setCurrentSlot(previous);
    GameThreads::leaveAudio();
}

// The driver up and the host's audio clock running.
void JASDriver::startAudio() {
    JASChannel::initBankDisposeMsgQueue();
    JASVoice::initAll();
    JASVoiceSlot::initAll();
    JASPoolAllocObject_MultiThreaded< JASChannel >::newMemPool(JASVoice::cVoices);
    startClock();
}

void JASDriver::startClock() {
    nebula_host()->sound_clock(nebula_host()->user, NEBULA_AUDIO_START, cTicksPerSecond);
}

void JASDriver::stopClock() {
    nebula_host()->sound_clock(nebula_host()->user, NEBULA_AUDIO_STOP, 0);
}

// The blocks the clock asked for (at most cMaxCatchUp; the rest are dropped), each to the output.
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
    updateSubFrame();
    const NebulaHostServices* const host = nebula_host();
    host->voices_frame(host->user);
}

void JASDriver::updateSubFrame() {
    syncCallback();
    JASChannel::receiveBankDisposeMsg();
    JASVoiceSlot::updateAll();

    subframeCallback();
    JASLfo::updateFreeRun(cLfoStep);
}


