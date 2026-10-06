#include "galaxy/runtime_settings.h"
#include "galaxy/atomic_file.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Shlobj.h>

#include <algorithm>
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
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#pragma comment(lib, "Shell32.lib")

namespace galaxy {

namespace {

RuntimeSettings s_settings{};
std::uint64_t s_input_mode_generation = 1u;
SRWLOCK s_settings_lock = SRWLOCK_INIT;

RuntimeSettings sanitize_settings(RuntimeSettings settings) {
    switch (settings.input_mode) {
    case RuntimeInputMode::KeyboardMouse:
    case RuntimeInputMode::Controller:
    case RuntimeInputMode::RealWiimote:
        break;
    case RuntimeInputMode::Auto:
    default:
        settings.input_mode = RuntimeInputMode::KeyboardMouse;
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
    return settings;
}

float parse_gain(const char* value, float fallback) {
    struct NumericLocale {
        _locale_t handle = _create_locale(LC_NUMERIC, "C");
        ~NumericLocale() {
            if (handle != nullptr) {
                _free_locale(handle);
            }
        }
    };
    static const NumericLocale locale;
    if (locale.handle == nullptr) {
        throw std::runtime_error("cannot create runtime settings numeric locale");
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = _strtof_l(value, &end, locale.handle);
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
    if (text == "controller" || text == "xinput" || text == "gamepad") {
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
    }
}

std::filesystem::path settings_path() {
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

    wchar_t app_data[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(
            nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, app_data))) {
        return {};
    }
    return std::filesystem::path(app_data) / L"Nebula" /
           L"runtime_settings.ini";
}

void parse_settings_file(RuntimeSettings& settings) {
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        return;
    }

    FILE* file = nullptr;
    const int open_error = _wfopen_s(&file, path.c_str(), L"rb");
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
        apply_kv(settings, key_text.c_str(), value_text.c_str());
    }
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
}

}  // namespace

RuntimeSettings get_runtime_settings() {
    AcquireSRWLockShared(&s_settings_lock);
    const RuntimeSettings copy = s_settings;
    ReleaseSRWLockShared(&s_settings_lock);
    return copy;
}

RuntimeInputModeState get_runtime_input_mode_state() {
    AcquireSRWLockShared(&s_settings_lock);
    const RuntimeInputModeState copy{
        s_settings.input_mode,
        s_input_mode_generation,
    };
    ReleaseSRWLockShared(&s_settings_lock);
    return copy;
}

void set_runtime_settings(const RuntimeSettings& settings) {
    const RuntimeSettings sanitized = sanitize_settings(settings);
    AcquireSRWLockExclusive(&s_settings_lock);
    if (s_settings.input_mode != sanitized.input_mode) {
        if (s_input_mode_generation ==
            std::numeric_limits<std::uint64_t>::max()) {
            ReleaseSRWLockExclusive(&s_settings_lock);
            throw std::overflow_error(
                "runtime input-mode generation overflow");
        }
        ++s_input_mode_generation;
    }
    s_settings = sanitized;
    ReleaseSRWLockExclusive(&s_settings_lock);
}

void load_runtime_settings_from_file() {
    RuntimeSettings settings{};
    parse_settings_file(settings);
    apply_env_overrides(settings);
    set_runtime_settings(settings);
}

bool save_runtime_settings_to_file() {
    try {
        const RuntimeSettings settings = get_runtime_settings();
        const std::filesystem::path path = settings_path();
        if (path.empty()) {
            return false;
        }

        std::ostringstream contents;
        contents.imbue(std::locale::classic());
        contents << "input_mode=" << runtime_input_mode_name(settings.input_mode)
                 << '\n'
                 << std::setprecision(std::numeric_limits<float>::max_digits10)
                 << "audio_master_gain=" << settings.audio.master << '\n'
                 << "audio_music_gain=" << settings.audio.music << '\n'
                 << "audio_sfx_gain=" << settings.audio.sfx << '\n'
                 << "audio_voice_gain=" << settings.audio.voice << '\n'
                 << "audio_ambience_gain=" << settings.audio.ambience << '\n';
        if (!contents) {
            return false;
        }
        return io::write_text_file_atomically(path, contents.str());
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
        return "keyboard_mouse";
    }
}

}  // namespace galaxy
