#include "Game/System/NebulaSystemSettings.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    u8 setting(u32 item) {
        const NebulaHostServices* const host = nebula_host();
        return static_cast< u8 >(host->sc_get(host->user, item));
    }
}  // namespace

namespace NebulaSystemSettings {
    u8 language() {
        return setting(NEBULA_SC_LANGUAGE);
    }

    SoundMode soundMode() {
        return static_cast< SoundMode >(setting(NEBULA_SC_SOUND_MODE));
    }

    bool isWidescreen() {
        return setting(NEBULA_SC_ASPECT_RATIO) == 1;
    }

    bool screenSaver() {
        return setting(NEBULA_SC_SCREEN_SAVER_MODE) != 0;
    }
}  // namespace NebulaSystemSettings
