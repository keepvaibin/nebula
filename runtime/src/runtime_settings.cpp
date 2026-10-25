#include "galaxy/runtime_settings.h"
#include "galaxy/atomic_file.h"
#include "galaxy/input_bindings.h"
#include "galaxy/platform_paths.h"
#include "galaxy/c_locale_number.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#pragma comment(lib, "Shell32.lib")
#endif

namespace galaxy {

namespace {

RuntimeSettings s_settings{};
std::uint64_t s_input_mode_generation = 1u;
std::shared_mutex s_settings_lock;

RuntimeSettings sanitize_settings(RuntimeSettings settings) {
    switch (settings.input_mode) {
    case RuntimeInputMode::Auto:
    case RuntimeInputMode::KeyboardMouse:
    case RuntimeInputMode::Controller:
    case RuntimeInputMode::RealWiimote:
        break;
    default:
        settings.input_mode = RuntimeInputMode::Auto;
        break;
    }
    const auto sanitize_gain = [](float gain) {
        return std::isfinite(gain) ? std::clamp(gain, 0.0f, 4.0f) : 1.0f;
    };
    settings.audio.master = sanitize_gain(settings.audio.master);
    settings.audio.music = sanitize_gain(settings.audio.music);
    settings.audio.sfx = sanitize_gain(settings.audio.sfx);
    settings.audio.voice = sanitize_gain(settings.audio.voice);
    settings.audio.ambience = sanitize_gain(settings.audio.ambience);
    settings.audio.cutscene = sanitize_gain(settings.audio.cutscene);
    settings.audio.wiimote_speaker = std::isfinite(settings.audio.wiimote_speaker)
        ? std::clamp(settings.audio.wiimote_speaker, 0.0f, 4.0f)
        : 0.0f;
    settings.text_speed = std::clamp(settings.text_speed, 1u, 4u);
    if (settings.language != GameLanguage::French && settings.language != GameLanguage::Spanish) {
        settings.language = GameLanguage::English;
    }
    return settings;
}

float parse_gain(const char* value, float fallback) {
    if (galaxy::c_numeric_locale() == nullptr) {
        throw std::runtime_error("cannot create runtime settings numeric locale");
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = galaxy::strtof_c(value, &end);
    if (end == value || errno == ERANGE || !std::isfinite(parsed)) {
        return fallback;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
        ++end;
    }
    if (*end != '\0') {
        return fallback;
    }
    return std::clamp(parsed, 0.0f, 4.0f);
}

RuntimeInputMode parse_input_mode(
    const char* value,
    RuntimeInputMode fallback) {
    std::string text(value);
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    if (text == "auto") {
        return RuntimeInputMode::Auto;
    }
    if (text == "controller" || text == "xinput" || text == "gamepad" ||
        text == "switch" || text == "switch_pro") {
        return RuntimeInputMode::Controller;
    }
    if (text == "keyboard" || text == "keyboard_mouse" ||
        text == "keyboard-mouse" || text == "kbm" || text == "mouse") {
        return RuntimeInputMode::KeyboardMouse;
    }
    if (text == "wiimote" || text == "wii_remote" || text == "wii-remote" ||
        text == "real_wiimote" || text == "real-wiimote") {
        return RuntimeInputMode::RealWiimote;
    }
    return fallback;
}

std::string_view trim(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1u);
}

void apply_kv(RuntimeSettings& settings, const char* key, const char* value) {
    if (std::strcmp(key, "input_mode") == 0) {
        settings.input_mode = parse_input_mode(value, settings.input_mode);
    } else if (std::strcmp(key, "audio_master_gain") == 0) {
        settings.audio.master = parse_gain(value, settings.audio.master);
    } else if (std::strcmp(key, "audio_music_gain") == 0) {
        settings.audio.music = parse_gain(value, settings.audio.music);
    } else if (std::strcmp(key, "audio_sfx_gain") == 0) {
        settings.audio.sfx = parse_gain(value, settings.audio.sfx);
    } else if (std::strcmp(key, "audio_voice_gain") == 0) {
        settings.audio.voice = parse_gain(value, settings.audio.voice);
    } else if (std::strcmp(key, "audio_ambience_gain") == 0) {
        settings.audio.ambience = parse_gain(value, settings.audio.ambience);
    } else if (std::strcmp(key, "audio_cutscene_gain") == 0) {
        settings.audio.cutscene = parse_gain(value, settings.audio.cutscene);
    } else if (std::strcmp(key, "audio_wiimote_speaker_gain") == 0) {
        settings.audio.wiimote_speaker = parse_gain(value, settings.audio.wiimote_speaker);
    } else if (std::strcmp(key, "text_speed") == 0) {
        if (value[0] >= '1' && value[0] <= '4' && value[1] == '\0') {
            settings.text_speed = static_cast<unsigned>(value[0] - '0');
        }
    } else if (std::strcmp(key, "discord_status") == 0) {
        settings.discord_status = value[0] != '0';
    } else if (std::strcmp(key, "button_icons") == 0) {
        for (unsigned i = 0; i <= static_cast<unsigned>(ButtonIcons::PlayStation); ++i) {
            if (_stricmp(value, button_icons_key(static_cast<ButtonIcons>(i))) == 0) {
                settings.button_icons = static_cast<ButtonIcons>(i);
            }
        }
    } else if (std::strcmp(key, "language") == 0) {
        for (const GameLanguage language : {GameLanguage::English, GameLanguage::French, GameLanguage::Spanish}) {
            if (_stricmp(value, game_language_key(language)) == 0) settings.language = language;
        }
    }
}

std::filesystem::path settings_path() {
#if defined(_WIN32)
    wchar_t* override_value = nullptr;
    std::size_t override_length = 0u;
    const int query_error = _wdupenv_s(
        &override_value,
        &override_length,
        L"GALAXY_RUNTIME_SETTINGS_PATH");
    const std::unique_ptr<wchar_t, decltype(&std::free)> owned_override(
        override_value, &std::free);
    if (query_error != 0) {
        throw std::runtime_error("cannot read GALAXY_RUNTIME_SETTINGS_PATH");
    }
    if (override_value != nullptr && override_length > 1u) {
        if (override_length > MAX_PATH) {
            throw std::runtime_error(
                "GALAXY_RUNTIME_SETTINGS_PATH exceeds the supported path length");
        }
        const std::filesystem::path path(override_value);
        if (path.filename().empty()) {
            throw std::runtime_error(
                "GALAXY_RUNTIME_SETTINGS_PATH must name a settings file");
        }
        return path;
    }

#else
    if (const char* override_value = std::getenv("GALAXY_RUNTIME_SETTINGS_PATH");
        override_value != nullptr && override_value[0] != '\0') {
        const std::filesystem::path path(override_value);
        if (path.filename().empty()) {
            throw std::runtime_error("GALAXY_RUNTIME_SETTINGS_PATH must name a settings file");
        }
        return path;
    }
#endif
    const std::filesystem::path folder = nebula_data_directory();
    return folder.empty() ? folder : folder / "runtime_settings.ini";
}

// A file of an older controls version carries that version's defaults: those bindings move to the
// current defaults, and a version-1 file's keyboard_mouse becomes auto (from version 2 the one-source
// modes are overrides). Choices that differ from the old defaults stay.
void migrate_settings_file(RuntimeSettings& settings, input::ControlsConfig& controls,
                           unsigned version) {
    if (version >= input::kControlsVersion) {
        return;
    }
    const unsigned changed = input::migrate_legacy_controls(controls, version);
    const bool mode = version < 2u && settings.input_mode == RuntimeInputMode::KeyboardMouse;
    if (mode) {
        settings.input_mode = RuntimeInputMode::Auto;
    }
    std::fprintf(stderr, "[settings] controls version %u -> %u: %u defaults updated%s\n",
                 version, input::kControlsVersion, changed,
                 mode ? ", input_mode keyboard_mouse -> auto" : "");
}

void parse_settings_file(RuntimeSettings& settings, input::ControlsConfig& controls) {
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        return;
    }
    unsigned controls_version = 0u;

    FILE* file = open_path(path, "rb");
    const int open_error = file != nullptr ? 0 : errno;
    const std::unique_ptr<FILE, decltype(&std::fclose)> owned_file(file, &std::fclose);
    if (open_error != 0 || file == nullptr) {
        if (open_error == ENOENT) {
            return;
        }
        throw std::runtime_error("cannot open runtime settings file");
    }

    std::string line;
    bool first_line = true;
    for (;;) {
        line.clear();
        int character = 0;
        while ((character = std::fgetc(file)) != EOF && character != '\n') {
            if (character == 0 || line.size() >= 4096u) {
                throw std::runtime_error("invalid or overlong runtime settings line");
            }
            line.push_back(static_cast<char>(character));
        }
        if (std::ferror(file) != 0) {
            throw std::runtime_error("cannot read runtime settings file");
        }
        if (character == EOF && line.empty()) {
            break;
        }
        std::string_view record(line);
        if (first_line && record.starts_with("\xEF\xBB\xBF")) {
            record.remove_prefix(3u);
        }
        first_line = false;
        const std::size_t equals = record.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(record.substr(0u, equals));
        if (key.empty() || key.front() == '#' || key.front() == ';') {
            continue;
        }
        const std::string key_text(key);
        const std::string value_text(trim(record.substr(equals + 1u)));
        if (key_text == "controls_version") {
            controls_version = static_cast<unsigned>(std::strtoul(value_text.c_str(), nullptr, 10));
        } else if (!input::apply_controls_setting(controls, key_text, value_text)) {
            apply_kv(settings, key_text.c_str(), value_text.c_str());
        }
    }
    migrate_settings_file(settings, controls, controls_version);
}

void apply_env(RuntimeSettings& settings, const char* env, const char* key) {
    char value[128]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), env) == 0 && length > 1u) {
        apply_kv(settings, key, value);
    }
}

void apply_env_overrides(RuntimeSettings& settings) {
    apply_env(settings, "GALAXY_INPUT_MODE", "input_mode");
    apply_env(settings, "GALAXY_AUDIO_MASTER_GAIN", "audio_master_gain");
    apply_env(settings, "GALAXY_AUDIO_MUSIC_GAIN", "audio_music_gain");
    apply_env(settings, "GALAXY_AUDIO_EFFECTS_GAIN", "audio_sfx_gain");
    apply_env(settings, "GALAXY_AUDIO_SFX_GAIN", "audio_sfx_gain");
    apply_env(settings, "GALAXY_AUDIO_VOICE_GAIN", "audio_voice_gain");
    apply_env(
        settings, "GALAXY_AUDIO_AMBIENCE_GAIN", "audio_ambience_gain");
    apply_env(
        settings, "GALAXY_AUDIO_CUTSCENE_GAIN", "audio_cutscene_gain");
    apply_env(
        settings, "GALAXY_AUDIO_WIIMOTE_SPEAKER_GAIN", "audio_wiimote_speaker_gain");
    apply_env(settings, "GALAXY_TEXT_SPEED", "text_speed");
    apply_env(settings, "GALAXY_LANGUAGE", "language");
}

}  // namespace

std::filesystem::path runtime_settings_folder() noexcept {
    try {
        return settings_path().parent_path();
    } catch (...) {
        return {};
    }
}

RuntimeSettings get_runtime_settings() {
    s_settings_lock.lock_shared();
    const RuntimeSettings copy = s_settings;
    s_settings_lock.unlock_shared();
    return copy;
}

namespace {
// 0 until the game first asks for its language, then that language's SC code.
std::atomic<std::uint8_t> s_running_language{0u};
// A bit for each GameLanguage on the disc (bit `value`); all until set_disc_languages.
std::atomic<std::uint32_t> s_disc_languages{~0u};

// The North American disc's folder for a language (the native game is RMGE01's).
const char* disc_folder(GameLanguage language) noexcept {
    switch (language) {
    case GameLanguage::French: return "UsFrench";
    case GameLanguage::Spanish: return "UsSpanish";
    case GameLanguage::English: break;
    }
    return "UsEnglish";
}
}  // namespace

void set_disc_languages(std::span<const std::string> folders) noexcept {
    std::uint32_t bits = 1u << static_cast<unsigned>(GameLanguage::English);  // English whatever (the fallback)
    for (const GameLanguage language : {GameLanguage::French, GameLanguage::Spanish}) {
        if (std::find(folders.begin(), folders.end(), disc_folder(language)) != folders.end()) {
            bits |= 1u << static_cast<unsigned>(language);
        }
    }
    s_disc_languages.store(bits, std::memory_order_release);
}

bool game_language_on_disc(GameLanguage language) noexcept {
    return (s_disc_languages.load(std::memory_order_acquire) >> static_cast<unsigned>(language) & 1u) != 0u;
}

GameLanguage game_language_for_boot() noexcept {
    std::uint8_t running = s_running_language.load(std::memory_order_acquire);
    if (running == 0u) {
        std::uint8_t expected = 0u;
        GameLanguage setting = get_runtime_settings().language;
        if (!game_language_on_disc(setting)) setting = GameLanguage::English;
        const auto chosen = static_cast<std::uint8_t>(setting);
        running = s_running_language.compare_exchange_strong(expected, chosen, std::memory_order_acq_rel)
            ? chosen : expected;
    }
    return static_cast<GameLanguage>(running);
}

bool running_game_language(GameLanguage& language) noexcept {
    const std::uint8_t running = s_running_language.load(std::memory_order_acquire);
    if (running == 0u) return false;
    language = static_cast<GameLanguage>(running);
    return true;
}

const char* button_icons_key(ButtonIcons icons) noexcept {
    static constexpr const char* kKeys[] = {"auto", "wii", "xbox", "nintendo", "playstation"};
    return kKeys[std::min<std::size_t>(static_cast<std::size_t>(icons), std::size(kKeys) - 1u)];
}

const char* game_language_key(GameLanguage language) noexcept {
    switch (language) {
    case GameLanguage::French: return "french";
    case GameLanguage::Spanish: return "spanish";
    case GameLanguage::English: break;
    }
    return "english";
}

RuntimeInputModeState get_runtime_input_mode_state() {
    s_settings_lock.lock_shared();
    const RuntimeInputModeState copy{
        s_settings.input_mode,
        s_input_mode_generation,
    };
    s_settings_lock.unlock_shared();
    return copy;
}

void set_runtime_settings(const RuntimeSettings& settings) {
    const RuntimeSettings sanitized = sanitize_settings(settings);
    s_settings_lock.lock();
    if (s_settings.input_mode != sanitized.input_mode) {
        if (s_input_mode_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
            s_settings_lock.unlock();
            throw std::overflow_error(
                "runtime input-mode generation overflow");
        }
        ++s_input_mode_generation;
    }
    s_settings = sanitized;
    s_settings_lock.unlock();
}

namespace {

// A running session never changes its input source because the file changed: a real Wii Remote, or a
// controller that is not connected, would fail it. The source the file names (Nebula Settings may have saved
// a new one) is written back by the game's saves unless the game changed its own source since the file was
// last read or written. Guarded by s_settings_lock.
RuntimeInputMode s_file_input_mode = RuntimeInputMode::Auto;
RuntimeInputMode s_input_mode_at_sync = RuntimeInputMode::Auto;

void record_input_mode_sync(RuntimeInputMode file_mode, RuntimeInputMode live_mode) {
    s_settings_lock.lock();
    s_file_input_mode = file_mode;
    s_input_mode_at_sync = live_mode;
    s_settings_lock.unlock();
}

}  // namespace

void load_runtime_settings_from_file() {
    RuntimeSettings settings{};
    input::ControlsConfig controls = input::default_controls_config();
    parse_settings_file(settings, controls);
    const RuntimeInputMode file_mode = sanitize_settings(settings).input_mode;
    apply_env_overrides(settings);
    set_runtime_settings(settings);
    input::set_controls_config(controls);
    record_input_mode_sync(file_mode, get_runtime_settings().input_mode);
}

bool save_runtime_settings_to_file() {
    try {
        const RuntimeSettings settings = get_runtime_settings();
        const std::filesystem::path path = settings_path();
        if (path.empty()) {
            return false;
        }
        s_settings_lock.lock_shared();
        const RuntimeInputMode input_mode =
            settings.input_mode == s_input_mode_at_sync ? s_file_input_mode : settings.input_mode;
        s_settings_lock.unlock_shared();

        std::ostringstream contents;
        contents.imbue(std::locale::classic());
        contents << "input_mode=" << runtime_input_mode_name(input_mode)
                 << '\n'
                 << "controls_version=" << input::kControlsVersion << '\n'
                 << std::setprecision(std::numeric_limits<float>::max_digits10)
                 << "audio_master_gain=" << settings.audio.master << '\n'
                 << "audio_music_gain=" << settings.audio.music << '\n'
                 << "audio_sfx_gain=" << settings.audio.sfx << '\n'
                 << "audio_voice_gain=" << settings.audio.voice << '\n'
                 << "audio_ambience_gain=" << settings.audio.ambience << '\n'
                 << "audio_cutscene_gain=" << settings.audio.cutscene << '\n'
                 << "audio_wiimote_speaker_gain=" << settings.audio.wiimote_speaker << '\n'
                 << "text_speed=" << settings.text_speed << '\n'
                 << "language=" << game_language_key(settings.language) << '\n'
                 << "discord_status=" << (settings.discord_status ? 1 : 0) << '\n'
                 << "button_icons=" << button_icons_key(settings.button_icons) << '\n'
                 << input::format_controls_settings(input::get_controls_config());
        if (!contents) {
            return false;
        }
        if (!io::write_text_file_atomically(path, contents.str())) {
            return false;
        }
        record_input_mode_sync(input_mode, settings.input_mode);
        return true;
    } catch (...) {
        return false;
    }
}

const char* runtime_input_mode_name(RuntimeInputMode mode) {
    switch (mode) {
    case RuntimeInputMode::KeyboardMouse:
        return "keyboard_mouse";
    case RuntimeInputMode::Controller:
        return "controller";
    case RuntimeInputMode::RealWiimote:
        return "real_wiimote";
    case RuntimeInputMode::Auto:
    default:
        return "auto";
    }
}

}  // namespace galaxy
