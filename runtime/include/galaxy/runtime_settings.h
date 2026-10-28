// Player settings in runtime_settings.ini: input mode, audio gains, text speed, language, Discord status.
#pragma once

#include <filesystem>
#include <cstdint>
#include <span>
#include <string>

namespace galaxy {

// Auto: keyboard, mouse and every controller are live together and the pointer follows whichever moved
// last. KeyboardMouse and Controller keep only that source.
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
    // In-engine cutscene sounds (the game's DEMO sound category) and the movies' audio.
    float cutscene = 1.0f;
    // What the game plays on the Wii Remote speaker, mixed into the output; off unless chosen.
    float wiimote_speaker = 0.0f;

    friend bool operator==(const RuntimeAudioSettings&, const RuntimeAudioSettings&) = default;
};

// The style of the game's button icons: the controller in use (a Wii Remote's for the keyboard and controllers it does
// not know), or one style always.
enum class ButtonIcons : std::uint8_t { Auto, Wii, Xbox, Nintendo, PlayStation, Keyboard };

// The game's languages, as the Wii's SC language codes SCGetLanguage returns; the disc's release picks each one's
// folder (UsEnglish on North America's disc, EuEnglish on Europe's, JpJapanese on Japan's). Auto: the system's language
// where the disc has it, else English, else the disc's first (game_language_resolved).
enum class GameLanguage : std::uint8_t {
    Japanese = 0,
    English = 1,
    German = 2,
    French = 3,
    Spanish = 4,
    Italian = 5,
    Dutch = 6,
    Auto = 0xFF,
};
// Every language a release's disc holds, in the order the settings step through them.
inline constexpr GameLanguage kGameLanguages[] = {GameLanguage::English, GameLanguage::German,  GameLanguage::French,
                                                  GameLanguage::Spanish, GameLanguage::Italian, GameLanguage::Dutch,
                                                  GameLanguage::Japanese};

struct RuntimeSettings {
    RuntimeInputMode input_mode = RuntimeInputMode::Auto;
    RuntimeAudioSettings audio{};
    // Steps of dialogue text revealed per frame: 1 the game's pace, 2 or 3; 4 each page at once (instant).
    unsigned text_speed = 1u;
    // Takes effect at the next start.
    GameLanguage language = GameLanguage::Auto;
    // Discord shows what the player is doing (galaxy, star, Power Stars) while Discord runs.
    bool discord_status = true;
    ButtonIcons button_icons = ButtonIcons::Auto;

    friend bool operator==(const RuntimeSettings&, const RuntimeSettings&) = default;
};

// The languages on the game's disc as it is read (its language folders, the mods' among them): the language setting
// offers only these, and a language whose folder is missing starts as Auto does. Every language until it is set.
void set_disc_languages(std::span<const std::string> folders) noexcept;
[[nodiscard]] bool game_language_on_disc(GameLanguage language) noexcept;
// The language `setting` plays in on this disc: itself where the disc has it; else (and for Auto) the system's
// language where the disc has it, else English, else the first of kGameLanguages the disc has.
[[nodiscard]] GameLanguage game_language_resolved(GameLanguage setting) noexcept;

// The language the game runs in this session: the setting when the game first asks (at boot), then
// fixed, so a change made while playing never mixes two languages.
[[nodiscard]] GameLanguage game_language_for_boot() noexcept;
// Whether the game has asked yet, and if so, its language.
[[nodiscard]] bool running_game_language(GameLanguage& language) noexcept;
// "auto", "japanese", "english", "german", "french", "spanish", "italian" or "dutch" (runtime_settings.ini).
[[nodiscard]] const char* game_language_key(GameLanguage language) noexcept;
// "auto", "wii", "xbox", "nintendo" or "playstation" (runtime_settings.ini).
[[nodiscard]] const char* button_icons_key(ButtonIcons icons) noexcept;

// The generation changes only when the input mode changes, so device FIFOs can reject reports acquired
// under an older source.
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
// Returns false on an invalid explicit path or a failed write; a bad override never redirects the save to
// the normal settings file.
[[nodiscard]] bool save_runtime_settings_to_file();
// The folder of the settings file (GALAXY_RUNTIME_SETTINGS_PATH's, else Nebula's data folder); empty when unknown.
[[nodiscard]] std::filesystem::path runtime_settings_folder() noexcept;

[[nodiscard]] const char* runtime_input_mode_name(RuntimeInputMode mode);

}  // namespace galaxy
