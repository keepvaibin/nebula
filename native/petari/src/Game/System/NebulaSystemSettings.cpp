#include "Game/System/NebulaSystemSettings.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    u8 setting(u32 item) {
        const NebulaHostServices* const host = nebula_host();
        return static_cast< u8 >(host->system_setting(host->user, item));
    }
}  // namespace

namespace NebulaSystemSettings {
    u8 language() {
        return setting(NEBULA_SETTING_LANGUAGE);
    }

    SoundMode soundMode() {
        return static_cast< SoundMode >(setting(NEBULA_SETTING_SOUND_MODE));
    }

    bool isWidescreen() {
        return setting(NEBULA_SETTING_WIDESCREEN) == 1;
    }

}  // namespace NebulaSystemSettings
