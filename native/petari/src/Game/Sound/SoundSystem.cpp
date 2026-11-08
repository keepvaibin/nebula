#include "Game/Sound/SoundSystem.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/SoundMemory.hpp"

namespace {
    // The mode last sent (none yet: the engine's own).
    u32 sOutputMode = ~0u;

    SoundCommand& next(u16 kind) {
        return SoundEngine::get()->ring().next(kind, nullptr);
    }

    u64 send() {
        return SoundEngine::get()->ring().send();
    }
}  // namespace

void SoundSystem::create(NebulaArena* pArena, f32 outputLevel) {
    SoundMemory::setArena(pArena);
    SoundEngine::create(pArena);
    next(SOUND_OUTPUT_LEVEL).payload< f32 >() = outputLevel;
    send();
}

void SoundSystem::start() {
    SoundEngine::get()->startClock();
}

void SoundSystem::frame() {
    SoundMemory::collect();
}

void SoundSystem::setOutputMode(u32 mode) {
    if (mode == sOutputMode) {
        return;
    }
    next(SOUND_OUTPUT_MODE).mValue = mode;
    send();
    sOutputMode = mode;
}

SoundReset::SoundReset() {
    next(SOUND_RESET_INIT);
    send();
}

bool SoundReset::start(u32 ticks, bool stopClock) {
    if (!checkDone()) {
        return false;
    }
    ResetCommand reset = {};
    reset.mTime = ticks;
    reset.mStopClock = stopClock;
    next(SOUND_RESET_START).payload< ResetCommand >() = reset;
    mStartSerial = send();
    return true;
}

void SoundReset::resume() {
    next(SOUND_RESET_RESUME);
    send();
}

// Not done from the start until the audio thread has run it and finished.
bool SoundReset::checkDone() const {
    const SoundEngine* engine = SoundEngine::get();
    return engine->applied() >= mStartSerial && engine->resetDone();
}
