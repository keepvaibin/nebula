#pragma once

#include <stdint.h>

// The game's sounds a mod's cooked sound stands in for (sound.md 6; LumaCore's audio service, runtime/src/luma_audio.cpp,
// sets the replacer at the module's start when a mod replaces a sound, and leaves it null otherwise). Game thread.
namespace LumaGameSound {
    // True when a mod's sound plays instead of the game's sound `id` at `pPos` (null: heard everywhere); the game's sound
    // is then not started. `level`: the game renews it each frame (startLevelSound), and it lasts while renewed.
    bool replaced(uint32_t id, const float* pPos, bool level);
    // Once a frame (AudSystem::frameWork): replacements placed in the world follow the camera, and level ones that the
    // game stopped renewing end.
    void frame();
}  // namespace LumaGameSound
