#pragma once

// The system settings the game reads (the host's settings).


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
}  // namespace NebulaSystemSettings
