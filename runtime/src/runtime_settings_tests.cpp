#include "galaxy/runtime_settings.h"

#include <Windows.h>

#include <cstdlib>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class ScopedEnv {
public:
    explicit ScopedEnv(const char* name) : name_(name) {
        char* old = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&old, &length, name_) == 0 && old != nullptr) {
            had_old_ = true;
            old_value_ = old;
            std::free(old);
        }
    }

    ~ScopedEnv() {
        _putenv_s(name_, had_old_ ? old_value_.c_str() : "");
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    bool had_old_ = false;
    std::string old_value_;
};

class ScopedWideEnv {
public:
    explicit ScopedWideEnv(const wchar_t* name) : name_(name) {
        wchar_t* old = nullptr;
        std::size_t length = 0u;
        if (_wdupenv_s(&old, &length, name_) != 0) {
            throw std::runtime_error("cannot snapshot test environment");
        }
        if (old != nullptr) {
            try {
                old_value_ = old;
                had_old_ = true;
            } catch (...) {
                std::free(old);
                throw;
            }
            std::free(old);
        }
    }
    ~ScopedWideEnv() {
        (void)_wputenv_s(name_, had_old_ ? old_value_.c_str() : L"");
    }
    ScopedWideEnv(const ScopedWideEnv&) = delete;
    ScopedWideEnv& operator=(const ScopedWideEnv&) = delete;
private:
    const wchar_t* name_;
    bool had_old_ = false;
    std::wstring old_value_;
};

class ScopedNumericLocale {
public:
    ScopedNumericLocale() : old_thread_mode_(_configthreadlocale(_ENABLE_PER_THREAD_LOCALE)) {
        if (old_thread_mode_ == -1) {
            throw std::runtime_error("cannot isolate test numeric locale");
        }
        try {
            const char* previous = std::setlocale(LC_NUMERIC, nullptr);
            if (previous == nullptr) {
                throw std::runtime_error("cannot snapshot test numeric locale");
            }
            old_value_ = previous;
        } catch (...) {
            (void)_configthreadlocale(old_thread_mode_);
            throw;
        }
    }
    ~ScopedNumericLocale() {
        (void)std::setlocale(LC_NUMERIC, old_value_.c_str());
        (void)_configthreadlocale(old_thread_mode_);
    }
    ScopedNumericLocale(const ScopedNumericLocale&) = delete;
    ScopedNumericLocale& operator=(const ScopedNumericLocale&) = delete;
private:
    int old_thread_mode_;
    std::string old_value_;
};

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        throw std::runtime_error("cannot write settings fixture");
    }
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    bool passed = true;

    const ScopedWideEnv scoped_path(L"GALAXY_RUNTIME_SETTINGS_PATH");
    const ScopedEnv scoped_input("GALAXY_INPUT_MODE");
    const ScopedEnv scoped_master("GALAXY_AUDIO_MASTER_GAIN");
    const ScopedEnv scoped_music("GALAXY_AUDIO_MUSIC_GAIN");
    const ScopedEnv scoped_sfx("GALAXY_AUDIO_SFX_GAIN");
    const ScopedEnv scoped_effects("GALAXY_AUDIO_EFFECTS_GAIN");
    const ScopedEnv scoped_voice("GALAXY_AUDIO_VOICE_GAIN");
    const ScopedEnv scoped_ambience("GALAXY_AUDIO_AMBIENCE_GAIN");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("galaxy_runtime_settings_tests_" +
         std::to_string(GetCurrentProcessId()) + "_" +
         std::to_string(GetTickCount64()));
    const std::filesystem::path settings_path = root / "runtime_settings.ini";
    if (!std::filesystem::create_directory(root)) {
        std::cerr << "FAILED: fixture directory was not newly created\n";
        return 1;
    }
    _wputenv_s(L"GALAXY_RUNTIME_SETTINGS_PATH", settings_path.c_str());

    _putenv_s("GALAXY_INPUT_MODE", "");
    _putenv_s("GALAXY_AUDIO_MASTER_GAIN", "");
    _putenv_s("GALAXY_AUDIO_MUSIC_GAIN", "");
    _putenv_s("GALAXY_AUDIO_SFX_GAIN", "");
    _putenv_s("GALAXY_AUDIO_EFFECTS_GAIN", "");
    _putenv_s("GALAXY_AUDIO_VOICE_GAIN", "");
    _putenv_s("GALAXY_AUDIO_AMBIENCE_GAIN", "");
    std::filesystem::remove(settings_path);
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::KeyboardMouse,
        "missing settings default to keyboard/mouse, not auto");

    write_text(settings_path, "input_mode=auto\n");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::KeyboardMouse,
        "stale file input_mode=auto is ignored");

    write_text(settings_path, "input_mode=controller\n");
    _putenv_s("GALAXY_INPUT_MODE", "auto");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::Controller,
        "env input_mode=auto does not override a concrete file mode");

    _putenv_s("GALAXY_INPUT_MODE", "keyboard_mouse");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::KeyboardMouse,
        "env input_mode=keyboard_mouse is accepted");

    _putenv_s("GALAXY_INPUT_MODE", "controller");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::Controller,
        "env input_mode=controller is accepted");

    _putenv_s("GALAXY_INPUT_MODE", "wiimote");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::RealWiimote,
        "env input_mode=wiimote selects the real Wii Remote source");
    passed &= expect(
        std::string(galaxy::runtime_input_mode_name(
            galaxy::RuntimeInputMode::RealWiimote)) == "real_wiimote",
        "RuntimeInputMode::RealWiimote serializes as real_wiimote");

    write_text(settings_path, "input_mode=real_wiimote\n");
    _putenv_s("GALAXY_INPUT_MODE", "");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::RealWiimote,
        "file input_mode=real_wiimote round-trips back to RealWiimote");

    galaxy::RuntimeSettings settings{};
    settings.input_mode = galaxy::RuntimeInputMode::Auto;
    galaxy::set_runtime_settings(settings);
    passed &= expect(
        galaxy::get_runtime_settings().input_mode ==
            galaxy::RuntimeInputMode::KeyboardMouse,
        "direct RuntimeSettings writes sanitize auto to keyboard/mouse");
    passed &= expect(
        std::string(galaxy::runtime_input_mode_name(
            galaxy::RuntimeInputMode::Auto)) == "keyboard_mouse",
        "RuntimeInputMode::Auto is not user-serialized as auto");

    const galaxy::RuntimeInputModeState generation_origin =
        galaxy::get_runtime_input_mode_state();
    galaxy::RuntimeSettings audio_only = galaxy::get_runtime_settings();
    audio_only.audio.master = 0.5f;
    galaxy::set_runtime_settings(audio_only);
    const galaxy::RuntimeInputModeState after_audio_only =
        galaxy::get_runtime_input_mode_state();
    passed &= expect(
        after_audio_only.mode == generation_origin.mode &&
            after_audio_only.generation == generation_origin.generation,
        "unrelated audio settings preserve the input-mode generation");

    write_text(
        settings_path,
        "input_mode=keyboard_mouse\n"
        "audio_master_gain=0.900\n"
        "audio_music_gain=0.800\n"
        "audio_sfx_gain=0.700\n"
        "audio_voice_gain=0.600\n"
        "audio_ambience_gain=0.500\n");
    galaxy::load_runtime_settings_from_file();
    const galaxy::RuntimeAudioSettings loaded_audio =
        galaxy::get_runtime_settings().audio;
    passed &= expect(
        loaded_audio.master == 0.9f && loaded_audio.music == 0.8f &&
            loaded_audio.sfx == 0.7f && loaded_audio.voice == 0.6f &&
            loaded_audio.ambience == 0.5f,
        "all five persisted audio gains load independently");

    _putenv_s("GALAXY_AUDIO_AMBIENCE_GAIN", "0.250");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().audio.ambience == 0.25f,
        "ambience environment override is independent from SFX");
    _putenv_s("GALAXY_AUDIO_AMBIENCE_GAIN", "");

    galaxy::RuntimeSettings invalid_audio = galaxy::get_runtime_settings();
    invalid_audio.audio.master = -1.0f;
    invalid_audio.audio.music = 2.0f;
    invalid_audio.audio.sfx = std::numeric_limits<float>::quiet_NaN();
    galaxy::set_runtime_settings(invalid_audio);
    const galaxy::RuntimeAudioSettings sanitized_audio =
        galaxy::get_runtime_settings().audio;
    passed &= expect(
        sanitized_audio.master == 0.0f && sanitized_audio.music == 2.0f &&
            sanitized_audio.sfx == 1.0f,
        "direct audio-setting writes clamp or repair invalid gains");

    galaxy::RuntimeSettings controller_settings = audio_only;
    controller_settings.input_mode = galaxy::RuntimeInputMode::Controller;
    galaxy::set_runtime_settings(controller_settings);
    const galaxy::RuntimeInputModeState after_controller =
        galaxy::get_runtime_input_mode_state();
    passed &= expect(
        after_controller.mode == galaxy::RuntimeInputMode::Controller &&
            after_controller.generation == generation_origin.generation + 1u &&
            galaxy::get_runtime_settings().input_mode == after_controller.mode,
        "actual input-mode change advances one coherent generation");

    controller_settings.audio.voice = 0.25f;
    galaxy::set_runtime_settings(controller_settings);
    const galaxy::RuntimeInputModeState after_same_controller =
        galaxy::get_runtime_input_mode_state();
    passed &= expect(
        after_same_controller.mode == galaxy::RuntimeInputMode::Controller &&
            after_same_controller.generation == after_controller.generation,
        "same-mode settings write does not advance the input generation");

    controller_settings.input_mode = galaxy::RuntimeInputMode::Auto;
    galaxy::set_runtime_settings(controller_settings);
    const galaxy::RuntimeInputModeState after_sanitized_auto =
        galaxy::get_runtime_input_mode_state();
    passed &= expect(
        after_sanitized_auto.mode == galaxy::RuntimeInputMode::KeyboardMouse &&
            after_sanitized_auto.generation == after_controller.generation + 1u &&
            galaxy::get_runtime_settings().input_mode ==
                after_sanitized_auto.mode,
        "sanitized auto transition advances exactly one coherent generation");

    _putenv_s("GALAXY_INPUT_MODE", "");
    write_text(settings_path, "sentinel=old\n");
    HANDLE locked = CreateFileW(
        settings_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    passed &= expect(
        locked != INVALID_HANDLE_VALUE,
        "runtime settings destination can be locked for failure injection");
    passed &= expect(
        !galaxy::save_runtime_settings_to_file() &&
            read_text(settings_path) == "sentinel=old\n",
        "runtime settings replace failure preserves the complete old file");
    if (locked != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(locked);
    }
    passed &= expect(
        galaxy::save_runtime_settings_to_file() &&
            read_text(settings_path).find("input_mode=keyboard_mouse") !=
                std::string::npos &&
            read_text(settings_path).find("audio_ambience_gain=") !=
                std::string::npos,
        "runtime settings atomically replace the destination with ambience persistence after recovery");
    std::size_t temp_count = 0u;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().filename().wstring().find(L".tmp.") !=
            std::wstring::npos) {
            ++temp_count;
        }
    }
    passed &= expect(
        temp_count == 0u,
        "runtime settings leave no sibling temporary after failure/recovery");

    for (const char* invalid : {"0.25junk", "1e999", "1e-999", "nan", "inf", "0,5"}) {
        write_text(settings_path, "audio_master_gain=0.75\n");
        _putenv_s("GALAXY_AUDIO_MASTER_GAIN", invalid);
        galaxy::load_runtime_settings_from_file();
        passed &= expect(
            galaxy::get_runtime_settings().audio.master == 0.75f,
            "malformed/range-error gain preserves the valid file value");
    }
    for (const char* valid : {"+0.5", "0x1p-1", "5e-1", " .5 "}) {
        _putenv_s("GALAXY_AUDIO_MASTER_GAIN", valid);
        galaxy::load_runtime_settings_from_file();
        passed &= expect(
            galaxy::get_runtime_settings().audio.master == 0.5f,
            "complete C-locale decimal/hex/exponent/sign gain is accepted");
    }
    _putenv_s("GALAXY_AUDIO_MASTER_GAIN", "");

    {
        const ScopedNumericLocale locale_scope;
        bool comma_locale = false;
        for (const char* candidate : {"de-DE", "German_Germany.1252", "fr-FR"}) {
            if (std::setlocale(LC_NUMERIC, candidate) != nullptr) {
                comma_locale = true;
                break;
            }
        }
        if (comma_locale) {
            write_text(settings_path, "audio_master_gain=0.900\n");
            galaxy::load_runtime_settings_from_file();
            passed &= expect(
                galaxy::get_runtime_settings().audio.master == 0.9f,
                "saved decimal gain is independent of the current comma numeric locale");
        } else {
            std::cout << "SKIP: comma numeric locale unavailable\n";
        }
    }

    write_text(settings_path, "\xEF\xBB\xBF" "audio_master_gain=0.625");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().audio.master == 0.625f,
        "UTF-8 BOM and a final complete record without newline are accepted");

    write_text(
        settings_path,
        "#" + std::string(510u, 'x') +
            "audio_master_gain=0\ninput_mode=controller\n");
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().audio.master == 1.0f &&
            galaxy::get_runtime_settings().input_mode == galaxy::RuntimeInputMode::Controller,
        "a long comment is one record, not an injected continuation setting");

    const auto before_failed_load = galaxy::get_runtime_input_mode_state();
    std::string corrupt = "input_mode=real_wiimote\naudio_master_gain=0.25";
    corrupt.push_back('\0');
    corrupt += "\n";
    write_text(settings_path, corrupt);
    bool corrupt_rejected = false;
    try {
        galaxy::load_runtime_settings_from_file();
    } catch (const std::exception&) {
        corrupt_rejected = true;
    }
    const auto after_failed_load = galaxy::get_runtime_input_mode_state();
    passed &= expect(
        corrupt_rejected && after_failed_load.mode == before_failed_load.mode &&
            after_failed_load.generation == before_failed_load.generation,
        "invalid file data cannot publish a partially parsed source transition");

    write_text(settings_path, "input_mode=real_wiimote\n" + std::string(4097u, '#'));
    bool overlong_rejected = false;
    try {
        galaxy::load_runtime_settings_from_file();
    } catch (const std::exception&) {
        overlong_rejected = true;
    }
    passed &= expect(
        overlong_rejected &&
            galaxy::get_runtime_input_mode_state().generation == before_failed_load.generation,
        "an overlong logical line rejects the whole file before publication");

    write_text(settings_path, "input_mode=real_wiimote\n");
    HANDLE unreadable = CreateFileW(
        settings_path.c_str(), GENERIC_READ, 0u, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    passed &= expect(unreadable != INVALID_HANDLE_VALUE, "settings read failure fixture is locked");
    if (unreadable != INVALID_HANDLE_VALUE) {
        bool read_rejected = false;
        try {
            galaxy::load_runtime_settings_from_file();
        } catch (const std::exception&) {
            read_rejected = true;
        }
        (void)CloseHandle(unreadable);
        passed &= expect(
            read_rejected &&
                galaxy::get_runtime_input_mode_state().generation == before_failed_load.generation,
            "file access error preserves the prior published settings generation");
    }

    galaxy::RuntimeSettings invalid_mode = galaxy::get_runtime_settings();
    invalid_mode.input_mode = static_cast<galaxy::RuntimeInputMode>(99u);
    galaxy::set_runtime_settings(invalid_mode);
    const auto sanitized_mode = galaxy::get_runtime_input_mode_state();
    passed &= expect(
        sanitized_mode.mode == galaxy::RuntimeInputMode::KeyboardMouse &&
            sanitized_mode.generation == before_failed_load.generation + 1u,
        "invalid direct input mode becomes one coherent keyboard/mouse transition");
    galaxy::set_runtime_settings(invalid_mode);
    passed &= expect(
        galaxy::get_runtime_input_mode_state().generation == sanitized_mode.generation,
        "repeated invalid direct mode does not invent more transitions");

    galaxy::RuntimeSettings fractional = galaxy::get_runtime_settings();
    fractional.audio.master = 0.0004f;
    fractional.audio.music = 0.12345679f;
    galaxy::set_runtime_settings(fractional);
    if (galaxy::save_runtime_settings_to_file()) {
        galaxy::load_runtime_settings_from_file();
        passed &= expect(
            galaxy::get_runtime_settings().audio.master == fractional.audio.master &&
                galaxy::get_runtime_settings().audio.music == fractional.audio.music,
            "finite direct gains round-trip without three-decimal truncation");
    } else {
        passed &= expect(false, "fractional settings fixture saves");
    }

    const auto unicode_path = root / L"\u6e38\u620f-settings.ini";
    write_text(unicode_path, "audio_master_gain=0.375\n");
    _wputenv_s(L"GALAXY_RUNTIME_SETTINGS_PATH", unicode_path.c_str());
    galaxy::load_runtime_settings_from_file();
    passed &= expect(
        galaxy::get_runtime_settings().audio.master == 0.375f &&
            galaxy::save_runtime_settings_to_file(),
        "wide settings override preserves the actual Unicode destination");

    const std::wstring too_long_override(300u, L'x');
    _wputenv_s(L"GALAXY_RUNTIME_SETTINGS_PATH", too_long_override.c_str());
    bool override_rejected = false;
    try {
        galaxy::load_runtime_settings_from_file();
    } catch (const std::exception&) {
        override_rejected = true;
    }
    passed &= expect(override_rejected, "unsupported override length fails explicitly");
    // Do not call save if the rejection regresses: that old path could redirect
    // into the user's real settings. The negative test must preserve user files.
    if (override_rejected) {
        passed &= expect(!galaxy::save_runtime_settings_to_file(),
            "invalid override save fails without selecting a fallback destination");
    }
    _wputenv_s(L"GALAXY_RUNTIME_SETTINGS_PATH", settings_path.c_str());

    std::filesystem::remove_all(root);

    if (!passed) {
        return 1;
    }
    std::cout << "Runtime settings tests passed\n";
    return 0;
}
