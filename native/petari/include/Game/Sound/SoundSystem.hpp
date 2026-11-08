#pragma once

#include <nebula_types.h>

class NebulaArena;

// The game thread's hold on the sound engine as a whole: made once, its output, and the reset that fades it out.
namespace SoundSystem {
    // The engine, its arena and its output level (before anything plays).
    void create(NebulaArena* pArena, f32 outputLevel);
    // The engine's clock: after the archive has set its banks up.
    void start();
    // Each game frame: the memory the audio thread has let go.
    void frame();

    // 0 mono, 1 stereo, 2 surround; sent when it changes.
    void setOutputMode(u32 mode);
}  // namespace SoundSystem

// The fade to silence before a reset (and the clock's stop after it, when asked), and the level back after.
class SoundReset {
public:
    SoundReset();
    // False while one runs.
    bool start(u32 ticks, bool stopClock);
    void resume();
    bool checkDone() const;

private:
    u64 mStartSerial = 0;
};
