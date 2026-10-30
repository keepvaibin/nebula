#pragma once

// The system settings the game reads (I-09: the host's settings, not the console's SYSCONF).

#include <revolution/types.h>

namespace NebulaSystemSettings {
    enum SoundMode {
        SoundModeMono = 0,
        SoundModeStereo = 1,
        SoundModeSurround = 2,
    };

    // The language, as the game's language tables index it (SC_LANGUAGE_*).
    u8 language();
    SoundMode soundMode();
    bool isWidescreen();
    // Whether the screen saver may dim the screen after the controllers go idle.
    bool screenSaver();
}  // namespace NebulaSystemSettings
