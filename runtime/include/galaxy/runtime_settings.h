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
    // In-engine cutscene sounds (the game's DEMO sound category) and the
    // pre-rendered movies' audio.
    float cutscene = 1.0f;
};

// Auto: keyboard, mouse and every controller are live together and the pointer
// follows whichever moved last. KeyboardMouse and Controller are overrides that
// keep only that source.
struct RuntimeSettings {
    RuntimeInputMode input_mode = RuntimeInputMode::Auto;
    RuntimeAudioSettings audio{};
};

// A coherent input-source snapshot. The generation changes only when the selected input mode changes,
// so device FIFOs can reject reports acquired under an older source. Unrelated audio-setting writes are
// not treated as an input transition.
struct RuntimeInputModeState {
    RuntimeInputMode mode = RuntimeInputMode::Auto;
    std::uint64_t generation = 1u;
};

[[nodiscard]] RuntimeSettings get_runtime_settings();
[[nodiscard]] RuntimeInputModeState get_runtime_input_mode_state();
void set_runtime_settings(const RuntimeSettings& settings);
// Missing files keep the defaults. Invalid explicit paths and read failures throw before any setting is
// published. Gains use the C numeric locale and complete finite tokens. A malformed value keeps its prior
// or default gain.
void load_runtime_settings_from_file();
// Re-reads only the audio gains when the settings file's last-write time changes, for example when an
// external editor or NebulaSettings.exe writes during play. Read or parse failures keep the current gains.
// Single caller thread.
bool reload_runtime_audio_settings_if_changed() noexcept;
// Invalid explicit paths and formatting/write/replace failures return false;
// a bad override never redirects a save to the normal user settings file.
[[nodiscard]] bool save_runtime_settings_to_file();

[[nodiscard]] const char* runtime_input_mode_name(RuntimeInputMode mode);

}  // namespace galaxy
