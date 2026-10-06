#pragma once

#include <cstdint>

namespace galaxy {

enum class RuntimeInputMode : std::uint32_t {
    Auto = 0,
    KeyboardMouse = 1,
    Controller = 2,
    RealWiimote = 3,
};

struct RuntimeAudioSettings {
    float master = 1.0f;
    float music = 1.0f;
    float sfx = 1.0f;
    float voice = 1.0f;
    float ambience = 1.0f;
};

struct RuntimeSettings {
    RuntimeInputMode input_mode = RuntimeInputMode::KeyboardMouse;
    RuntimeAudioSettings audio{};
};

// A coherent input-source snapshot. The generation changes only when the
// selected input mode changes, so device FIFOs can reject reports acquired
// under an older source without treating unrelated audio-setting writes as an
// input transition.
struct RuntimeInputModeState {
    RuntimeInputMode mode = RuntimeInputMode::KeyboardMouse;
    std::uint64_t generation = 1u;
};

[[nodiscard]] RuntimeSettings get_runtime_settings();
[[nodiscard]] RuntimeInputModeState get_runtime_input_mode_state();
void set_runtime_settings(const RuntimeSettings& settings);
// Missing files retain defaults. Invalid explicit paths or file/read failures
// throw before publishing any settings. Gains use the C numeric locale and
// complete finite tokens; malformed values retain their prior/default gain.
void load_runtime_settings_from_file();
// Invalid explicit paths and formatting/write/replace failures return false;
// a bad override never redirects a save to the normal user settings file.
[[nodiscard]] bool save_runtime_settings_to_file();

[[nodiscard]] const char* runtime_input_mode_name(RuntimeInputMode mode);

}  // namespace galaxy
