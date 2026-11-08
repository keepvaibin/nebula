#pragma once

// The system settings the game reads (the host's settings).


namespace NebulaSystemSettings {
    enum SoundMode {
        SoundModeMono = 0,
        SoundModeStereo = 1,
        SoundModeSurround = 2,
    };

    // The host's language: a SystemLanguage (Language.hpp), or a number past them that the game reads as English.
    u8 language();
    SoundMode soundMode();
    bool isWidescreen();
}  // namespace NebulaSystemSettings
