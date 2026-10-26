#include "galaxy/gx/render_config.h"
#include "galaxy/experimental_ultrawide_aspect.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <cmath>
#include <process.h>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

class ScopedEnv {
public:
    explicit ScopedEnv(const char* name, const char* value) : name_(name) {
        save_old();
        _putenv_s(name_.c_str(), value);
    }

    ~ScopedEnv() {
        _putenv_s(name_.c_str(), had_old_ ? old_value_.c_str() : "");
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void save_old() {
        char* old = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&old, &length, name_.c_str()) == 0 && old != nullptr) {
            had_old_ = true;
            old_value_ = old;
            std::free(old);
        }
    }

    std::string name_;
    bool had_old_ = false;
    std::string old_value_;
};

class ScopedWideEnv {
public:
    ScopedWideEnv(const wchar_t* name, const wchar_t* value) : name_(name) {
        wchar_t* old = nullptr;
        std::size_t length = 0;
        if (_wdupenv_s(&old, &length, name) == 0 && old != nullptr) {
            had_old_ = true;
            old_value_ = old;
            std::free(old);
        }
        _wputenv_s(name, value);
    }
    ~ScopedWideEnv() { _wputenv_s(name_.c_str(), had_old_ ? old_value_.c_str() : L""); }
private:
    std::wstring name_, old_value_;
    bool had_old_ = false;
};

}  // namespace

int main() {
    bool passed = true;

    {
        // The 2D layer's scale for the Wii's own layout density: 16:9 and 4:3 as the Wii, two at 21:9 and 32:9,
        // three at 9:16, four at 9:21 and narrower; no shape (the game's 16:9) one.
        const auto at = [](double aspect) {
            return galaxy::layout_density_scale(galaxy::make_experimental_ultrawide_aspect(aspect));
        };
        passed = expect(at(16.0 / 9.0) == 1u && at(4.0 / 3.0) == 1u && at(16.0 / 10.0) == 1u, "Wii shapes at 1x") &&
                 passed;
        passed = expect(at(21.0 / 9.0) == 2u && at(32.0 / 9.0) == 2u && at(5.0) == 2u, "wide shapes at 2x") && passed;
        passed = expect(at(1.0) == 2u && at(9.0 / 16.0) == 3u && at(9.0 / 21.0) == 4u && at(0.2) == 4u,
                        "narrow shapes by their height") &&
                 passed;
        passed = expect(galaxy::layout_density_scale(std::nullopt) == 1u, "no shape at 1x") && passed;
    }

    {
        const auto same = [](const std::vector<unsigned>& got, std::initializer_list<unsigned> want) {
            return std::equal(got.begin(), got.end(), want.begin(), want.end());
        };
        using galaxy::gx::aspect_window_sizes;
        using galaxy::gx::kAspectRatios;
        if (!aspect_window_sizes(0u, 4000u, 4000u).empty()) {
            std::cerr << "the Auto aspect has no fixed window sizes\n";
            passed = false;
        }
        for (unsigned mode = 1u; mode < galaxy::gx::kAspectModeCount; ++mode) {
            const auto sizes = aspect_window_sizes(mode, 2600u, 1400u);
            bool ok = sizes.size() >= 3u;
            for (std::size_t i = 0; i < sizes.size(); ++i) {
                const auto [w, h] = sizes[i];
                ok = ok && w * kAspectRatios[mode][1] == h * kAspectRatios[mode][0] &&
                     w <= 2600u && h <= 1400u && (i == 0u || sizes[i - 1u].second < h);
            }
            if (!ok) {
                std::cerr << "aspect " << mode << ": window sizes must have its shape, fit and grow\n";
                passed = false;
            }
        }
        const auto wide = aspect_window_sizes(5u, 2600u, 1400u);
        if (wide.empty() || wide.front() != std::pair{840u, 360u} || wide.back() != std::pair{2520u, 1080u}) {
            std::cerr << "21:9 window sizes within 2600 x 1400 must run from 840 x 360 to 2520 x 1080\n";
            passed = false;
        }
        using galaxy::gx::present_frame_rate;
        if (present_frame_rate(30.0f, 1u, 144.0) != 30.0 || present_frame_rate(144.0f, 1u, 144.0) != 144.0 ||
            present_frame_rate(240.0f, 1u, 144.0) != 144.0 || present_frame_rate(240.0f, 0u, 144.0) != 240.0 ||
            present_frame_rate(60.0f, 1u, 50.0) != 50.0 || present_frame_rate(0.0f, 0u, 50.0) != 60.0 ||
            present_frame_rate(0.0f, 1u, 240.0) != 240.0 || present_frame_rate(0.0f, 0u, 165.0) != 165.0 ||
            present_frame_rate(0.0f, 0u, 0.0) != 60.0) {
            std::cerr << "present rate must follow the cap, the display rate and VSync\n";
            passed = false;
        }
        using galaxy::gx::interpolated_frames_per_game_frame;
        if (interpolated_frames_per_game_frame(30.0) != 0u ||
            interpolated_frames_per_game_frame(60.0) != 0u ||
            interpolated_frames_per_game_frame(61.5) != 1u ||
            interpolated_frames_per_game_frame(75.0) != 1u ||
            interpolated_frames_per_game_frame(90.0) != 1u ||
            interpolated_frames_per_game_frame(120.0) != 1u ||
            interpolated_frames_per_game_frame(144.0) != 2u ||
            interpolated_frames_per_game_frame(165.0) != 2u ||
            interpolated_frames_per_game_frame(240.0) != 3u ||
            interpolated_frames_per_game_frame(360.0) != 5u ||
            interpolated_frames_per_game_frame(1000.0) != 7u) {
            std::cerr << "interpolated frames must be round(rate / 60) - 1, 0..7\n";
            passed = false;
        }
    }

    {
        const auto root = std::filesystem::temp_directory_path() /
            ("galaxy_config_validation_" + std::to_string(_getpid()));
        const auto path = root / "config.ini";
        std::filesystem::create_directories(root);
        ScopedEnv config_path("GALAXY_RENDER_CONFIG_PATH", path.string().c_str());
        for (const char* text : {"nan", "inf", "-1", "60junk", "0.00001", "10000000", "1e100"}) {
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            ScopedEnv fps("GALAXY_MAX_FPS", text);
            galaxy::gx::load_render_config_from_file();
            passed &= expect(galaxy::gx::get_render_config().max_fps == 60.0f,
                "malformed and unsafe FPS text preserves the prior cap");
        }
        ScopedEnv fps("GALAXY_MAX_FPS", "");
        {
            // The GPU chosen in Nebula Settings (config.ini gpu): the game starts on it and keeps it for the
            // session; a GPU chosen while it runs is kept in the file for the next start.
            passed &= expect(galaxy::gx::gpu_identity(0x10DEu, 0x2B85u, 0x89EF1043u) == "10de:2b85:89ef1043" &&
                    galaxy::gx::gpu_identity(0x1002u, 0x13C0u, 0x7E491462u) == "1002:13c0:7e491462",
                "a GPU's identity is its vendor, device and subsystem id in lowercase hex");
            const std::string started = galaxy::gx::gpu_identity(0x1002u, 0x13C0u, 0x7E491462u);
            const std::string chosen = galaxy::gx::gpu_identity(0x10DEu, 0x2B85u, 0x89EF1043u);
            const auto write = [&path](const std::string& text, std::uint64_t file_time) {
                {
                    std::ofstream file(path, std::ios::binary | std::ios::trunc);
                    file << text;
                }
                const HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                FILETIME time{static_cast<DWORD>(file_time), static_cast<DWORD>(file_time >> 32u)};
                const bool ok = file != INVALID_HANDLE_VALUE && SetFileTime(file, nullptr, nullptr, &time) != FALSE;
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                return ok;
            };
            const auto saved_gpu = [&path] {
                std::ifstream file(path, std::ios::binary);
                std::string line;
                while (std::getline(file, line)) {
                    if (line.rfind("gpu=", 0) == 0) return line.substr(4u);
                }
                return std::string("<none>");
            };
            const auto session_gpu = [] { return std::string(galaxy::gx::get_render_config().gpu.data()); };

            passed &= expect(write("gpu=" + started + "\naa=1\n", 132'000'000'000'000'000ull), "gpu fixture written");
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            galaxy::gx::load_render_config_from_file();
            passed &= expect(session_gpu() == started && galaxy::gx::next_start_gpu() == started,
                "the game starts on the GPU config.ini names");
            passed &= expect(galaxy::gx::save_render_config_to_file() && saved_gpu() == started,
                "the game's save keeps it");

            passed &= expect(write("gpu=" + chosen + "\naa=2\n", 132'000'000'100'000'000ull), "gpu fixture rewritten");
            passed &= expect(galaxy::gx::reload_render_config_if_changed() && galaxy::gx::get_render_config().aa_mode == 2u &&
                    session_gpu() == started && galaxy::gx::next_start_gpu() == chosen,
                "a GPU chosen while the game runs is read for the next start; the session keeps its GPU");
            passed &= expect(galaxy::gx::save_render_config_to_file() && saved_gpu() == chosen,
                "the game's save keeps the GPU chosen during the session");

            // While the in-game menu is open the game does not re-read config.ini; its own save still keeps
            // the gpu the file has.
            passed &= expect(write("gpu=\naa=2\n", 132'000'000'200'000'000ull), "gpu fixture set to automatic");
            galaxy::gx::RenderConfig live = galaxy::gx::get_render_config();
            live.sharpness = 40u;
            galaxy::gx::set_render_config(live);
            passed &= expect(galaxy::gx::save_render_config_to_file() && saved_gpu().empty() &&
                    galaxy::gx::next_start_gpu().empty() && session_gpu() == started,
                "a save that follows a change Nebula Settings made unread keeps that change's gpu");
            passed &= expect(write("gpu=" + chosen + "\n", 132'000'000'300'000'000ull), "gpu fixture rewritten again");
            live.gpu.fill('\0');
            std::memcpy(live.gpu.data(), started.c_str(), started.size());
            galaxy::gx::set_render_config(live);
            passed &= expect(galaxy::gx::save_render_config_to_file() && saved_gpu() == chosen,
                "the game's own gpu is never what it saves");
            {
                ScopedEnv override_gpu("GALAXY_GPU", started.c_str());
                passed &= expect(write("aa=1\n", 132'000'000'400'000'000ull), "gpu fixture without the key");
                galaxy::gx::load_render_config_from_file();
                passed &= expect(session_gpu() == started && galaxy::gx::next_start_gpu().empty() &&
                        galaxy::gx::save_render_config_to_file() && saved_gpu().empty(),
                    "GALAXY_GPU chooses this session's GPU and is not saved; a file without the key is automatic");
            }
            galaxy::gx::load_render_config_from_file();
            passed &= expect(galaxy::gx::get_render_config().gpu[0] == '\0', "an empty gpu is the automatic choice");
        }
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(), -1.0f, 0.00001f, 10000000.0f}) {
            galaxy::gx::RenderConfig direct{};
            direct.max_fps = invalid;
            direct.texture_lod_bias = 99u;
            direct.anisotropic_filtering = 0u;
            galaxy::gx::set_render_config(direct);
            const auto value = galaxy::gx::get_render_config();
            passed &= expect(value.max_fps == 60.0f && value.texture_lod_bias == 8u &&
                value.anisotropic_filtering == 1u, "direct config applies the same numeric boundary checks");
        }
        for (float rate : {1.0f, 30.0f, 59.940002f, 60.0f}) {
            galaxy::gx::RenderConfig original{};
            original.max_fps = rate;
            galaxy::gx::set_render_config(original);
            passed &= expect(galaxy::gx::save_render_config_to_file(), "FPS precision fixture saves");
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            galaxy::gx::load_render_config_from_file();
            passed &= expect(galaxy::gx::get_render_config().max_fps == rate,
                "frame-rate caps survive exact float round trips");
        }
        {
            // The bloom's mode and strength and the ray-traced effects, each away from its default.
            galaxy::gx::RenderConfig original{};
            original.bloom_mode = galaxy::gx::kBloomNative;
            original.bloom = 140u;
            original.rt_reflections = false;
            original.rt_lens_bloom = false;
            original.rt_sun_rays = false;
            original.rt_water = false;
            galaxy::gx::set_render_config(original);
            passed &= expect(galaxy::gx::save_render_config_to_file(), "bloom and effects fixture saves");
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            galaxy::gx::load_render_config_from_file();
            const auto loaded = galaxy::gx::get_render_config();
            passed &= expect(loaded.bloom_mode == galaxy::gx::kBloomNative && loaded.bloom == 140u &&
                                 !loaded.rt_reflections && !loaded.rt_lens_bloom && !loaded.rt_sun_rays &&
                                 !loaded.rt_water,
                             "the bloom's mode and strength and the ray-traced effects survive a save");
            const galaxy::gx::RenderConfig defaults{};
            passed &= expect(defaults.bloom_mode == galaxy::gx::kBloomEnhanced && defaults.rt_reflections &&
                                 defaults.rt_lens_bloom &&
                                 defaults.rt_sun_rays && defaults.rt_water,
                             "enhanced bloom and every ray-traced effect by default");
        }
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << std::string(511u, 'x') << "max_fps=0\n";
        }
        galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
        galaxy::gx::load_render_config_from_file();
        passed &= expect(galaxy::gx::get_render_config().max_fps == 60.0f,
            "an overlong physical INI line cannot smuggle an unlimited-cap suffix");
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "max_fps=0" << std::string(260u, ' ') << "junk\n";
        }
        galaxy::gx::load_render_config_from_file();
        passed &= expect(galaxy::gx::get_render_config().max_fps == 60.0f,
            "an oversized value cannot hide a malformed suffix beyond the parser buffer");
        {
            const auto unicode_path = root / L"\u03a9-\u65e5-settings.ini";
            ScopedWideEnv unicode_override(L"GALAXY_RENDER_CONFIG_PATH", unicode_path.c_str());
            galaxy::gx::RenderConfig value{};
            value.max_fps = 59.940002f;
            galaxy::gx::set_render_config(value);
            passed &= expect(galaxy::gx::save_render_config_to_file() &&
                std::filesystem::exists(unicode_path), "the explicit Unicode path is preserved by publication");
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            galaxy::gx::load_render_config_from_file();
            passed &= expect(galaxy::gx::get_render_config().max_fps == value.max_fps,
                "Unicode config paths reload the exact file that was saved");
        }
        {
            // Nebula Settings saves config.ini while the game runs: a newer file is read over the live
            // settings, and the game's own save is not read back.
            const auto stamp = [&path](std::uint64_t file_time) {
                const HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                FILETIME time{static_cast<DWORD>(file_time), static_cast<DWORD>(file_time >> 32u)};
                const bool ok = file != INVALID_HANDLE_VALUE && SetFileTime(file, nullptr, nullptr, &time) != FALSE;
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                return ok;
            };
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << "aa=1\nsharpness=20\n";
            }
            passed &= expect(stamp(133'000'000'000'000'000ull), "config fixture time set");
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            galaxy::gx::load_render_config_from_file();
            passed &= expect(!galaxy::gx::reload_render_config_if_changed(), "an unchanged config.ini is not re-read");
            galaxy::gx::RenderConfig live = galaxy::gx::get_render_config();
            live.per_pixel_lighting = true;
            galaxy::gx::set_render_config(live);
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << "aa=3\nsharpness=50\n";
            }
            passed &= expect(stamp(133'000'000'000'000'000ull), "config fixture time kept");
            passed &= expect(!galaxy::gx::reload_render_config_if_changed() &&
                    galaxy::gx::get_render_config().aa_mode == 1u,
                "the same last-write time keeps the live settings");
            passed &= expect(stamp(133'000'000'100'000'000ull), "config fixture time advanced");
            passed &= expect(galaxy::gx::reload_render_config_if_changed(), "a newer config.ini is re-read");
            const galaxy::gx::RenderConfig reloaded = galaxy::gx::get_render_config();
            passed &= expect(reloaded.aa_mode == 3u && reloaded.sharpness == 50u && reloaded.per_pixel_lighting,
                "the file's settings apply over the live ones, and a setting it does not name keeps its value");
            passed &= expect(galaxy::gx::save_render_config_to_file() && !galaxy::gx::reload_render_config_if_changed(),
                "the game's own save is not read back");
        }
        std::filesystem::remove_all(root);
    }

    const galaxy::gx::RenderConfig defaults{};
    passed &= expect(
        defaults.vsync_interval == 0 &&
            !defaults.wait_for_frame_latency &&
            defaults.swap_chain_frame_latency == 2 &&
            defaults.flip_discard &&
            defaults.max_fps == 60.0f,
        "default Release config keeps a 60 Hz cap for non-realtime-VI launches "
        "and the flip-discard swap chain");

    const galaxy::gx::GxTimingConfig& timing = galaxy::gx::get_gx_timing_config();
    passed &= expect(
        timing.render_queue_depth == 2 &&
            !timing.vi_render_fifo_wait &&
            timing.pe_events_gpu_fence &&
            !timing.render_live_memory_wait &&
            !timing.render_memory_snapshot &&
            timing.render_live_memory_reads &&
            timing.pe_events_at_consumption,
        "GX timing is the shipped profile: queue depth 2, live memory reads, PE events at submission");

    galaxy::gx::RenderConfig config{};
    config.msaa_samples = 8;
    config.taa_enabled = true;
    config.ray_tracing_enabled = true;
    config.rt_shadow_quality = 3;
    config.rt_ao_enabled = true;
    config.rt_reflections = false;
    config.swap_chain_frame_latency = 99;
    config.efb_scale = 99;
    galaxy::gx::set_render_config(config);

    const galaxy::gx::RenderConfig sanitized =
        galaxy::gx::get_render_config();
    passed &= expect(
        sanitized.msaa_samples == 8,
        "set_render_config keeps MSAA 8x");
    passed &= expect(
        !sanitized.taa_enabled,
        "set_render_config clamps unimplemented TAA off");
    passed &= expect(
        sanitized.ray_tracing_enabled &&
            sanitized.rt_shadow_quality == 0 &&
            !sanitized.rt_ao_enabled &&
            !sanitized.rt_reflections,
        "set_render_config keeps ray tracing and its reflections choice, and clamps the reserved DXR keys off");
    passed &= expect(
        sanitized.swap_chain_frame_latency == 2,
        "set_render_config clamps swap-chain latency to the frame ring");
    passed &= expect(
        sanitized.efb_scale == galaxy::gx::kMaxEfbScale,
        "set_render_config clamps EFB scale to the implemented maximum");

    galaxy::gx::stage_next_start_efb_scale(0u);
    passed &= expect(
        galaxy::gx::get_render_config().efb_scale == galaxy::gx::kMaxEfbScale,
        "staging the next-start EFB scale leaves the live scale");

    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("galaxy_render_config_tests_" +
             std::to_string(static_cast<unsigned>(_getpid())));
        const std::filesystem::path path = root / "config.ini";
        std::filesystem::remove_all(root);
        const ScopedEnv config_path(
            "GALAXY_RENDER_CONFIG_PATH",
            path.string().c_str());
        const ScopedEnv efb_scale("GALAXY_EFB_SCALE", "");

        galaxy::gx::stage_next_start_efb_scale(2u);
        passed &= expect(
            galaxy::gx::save_render_config_to_file(),
            "staged next-start EFB scale saves to an isolated config file");
        std::ifstream file(path, std::ios::binary);
        const std::string contents{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
        file.close();
        passed &= expect(
            contents.find("efb_scale=2") != std::string::npos &&
                galaxy::gx::get_render_config().efb_scale == galaxy::gx::kMaxEfbScale,
            "saved config contains the staged scale while the live scale remains unchanged");

        HANDLE locked = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        passed &= expect(
            locked != INVALID_HANDLE_VALUE,
            "render settings destination can be locked for failure injection");
        galaxy::gx::stage_next_start_efb_scale(3u);
        const bool locked_save = galaxy::gx::save_render_config_to_file();
        std::ifstream unchanged_file(path, std::ios::binary);
        const std::string unchanged{
            std::istreambuf_iterator<char>(unchanged_file),
            std::istreambuf_iterator<char>()};
        unchanged_file.close();
        passed &= expect(
            !locked_save &&
                unchanged.find("efb_scale=2") != std::string::npos &&
                unchanged.find("efb_scale=3") == std::string::npos,
            "render settings replace failure preserves the complete old file");
        if (locked != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(locked);
        }
        galaxy::gx::stage_next_start_efb_scale(2u);
        passed &= expect(
            galaxy::gx::save_render_config_to_file(),
            "render settings atomically recover after the destination unlocks");

        galaxy::gx::load_render_config_from_file();
        passed &= expect(
            galaxy::gx::get_render_config().efb_scale == 2u,
            "a new startup promotes the persisted staged scale to the active scale");
        std::filesystem::remove_all(root);
    }

    {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("galaxy_custom_window_size_tests_" +
             std::to_string(static_cast<unsigned>(_getpid())));
        const std::filesystem::path path = root / "config.ini";
        std::filesystem::remove_all(root);
        const ScopedEnv config_path(
            "GALAXY_RENDER_CONFIG_PATH", path.string().c_str());
        const ScopedEnv width_env("GALAXY_WINDOW_WIDTH", "");
        const ScopedEnv height_env("GALAXY_WINDOW_HEIGHT", "");
        galaxy::gx::RenderConfig custom{};
        custom.window_width = 3440u;
        custom.window_height = 1440u;
        galaxy::gx::set_render_config(custom);
        passed &= expect(
            galaxy::gx::save_render_config_to_file(),
            "a non-preset window size persists to config.ini");
        galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
        galaxy::gx::load_render_config_from_file();
        passed &= expect(
            galaxy::gx::get_render_config().window_width == 3440u &&
                galaxy::gx::get_render_config().window_height == 1440u,
            "a saved custom aspect and resolution survive reload");

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "aspect=4\n";
        }
        galaxy::gx::load_render_config_from_file();
        passed &= expect(galaxy::gx::get_render_config().aspect_mode == 5u,
                         "a numeric aspect is an index of the list before 3:2 (4 is 21:9)");
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "aspect=3:2\n";
        }
        galaxy::gx::load_render_config_from_file();
        passed &= expect(galaxy::gx::get_render_config().aspect_mode == 2u, "aspect=3:2 reads by name");
        {
            galaxy::gx::RenderConfig wide = galaxy::gx::get_render_config();
            wide.aspect_mode = 6u;
            galaxy::gx::set_render_config(wide);
            passed &= expect(galaxy::gx::save_render_config_to_file(), "the aspect saves");
            std::ifstream file(path, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            passed &= expect(text.find("aspect=32:9\n") != std::string::npos, "the aspect saves by name");
            file.close();
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            galaxy::gx::load_render_config_from_file();
            passed &= expect(galaxy::gx::get_render_config().aspect_mode == 6u, "a saved aspect name reads back");
            galaxy::gx::set_render_config(custom);
            passed &= expect(galaxy::gx::save_render_config_to_file(), "the custom size saves again");
        }

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "window_width=2561junk\n"
                    "window_height=2147483648\n";
        }
        galaxy::gx::load_render_config_from_file();
        passed &= expect(
            galaxy::gx::get_render_config().window_width == 3440u &&
                galaxy::gx::get_render_config().window_height == 1440u,
            "numeric prefixes and dimensions beyond Win32 int are rejected");

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "window_width=1\nwindow_height=2147483647\n";
        }
        galaxy::gx::load_render_config_from_file();
        passed &= expect(
            galaxy::gx::get_render_config().window_width == 1u &&
                galaxy::gx::get_render_config().window_height ==
                    static_cast<unsigned>(std::numeric_limits<int>::max()),
            "positive non-preset Win32-representable dimensions parse exactly");

        {
            const ScopedEnv invalid_width("GALAXY_WINDOW_WIDTH", "1920pixels");
            const ScopedEnv invalid_height("GALAXY_WINDOW_HEIGHT", "0");
            galaxy::gx::load_render_config_from_file();
            passed &= expect(
                galaxy::gx::get_render_config().window_width == 1u &&
                    galaxy::gx::get_render_config().window_height ==
                        static_cast<unsigned>(
                            std::numeric_limits<int>::max()),
                "malformed and zero window environment overrides are ignored");
        }

        galaxy::gx::RenderConfig direct{};
        direct.window_width = 0u;
        direct.window_height = std::numeric_limits<unsigned>::max();
        galaxy::gx::set_render_config(direct);
        passed &= expect(
            galaxy::gx::get_render_config().window_width == 1u &&
                galaxy::gx::get_render_config().window_height ==
                    static_cast<unsigned>(std::numeric_limits<int>::max()),
            "direct render settings cannot escape signed Win32 dimensions");
        std::filesystem::remove_all(root);
    }

    {
        ScopedEnv msaa("GALAXY_MSAA_SAMPLES", "8");
        ScopedEnv taa("GALAXY_TAA_ENABLED", "1");
        ScopedEnv rt("GALAXY_RAY_TRACING_ENABLED", "1");
        ScopedEnv rt_shadow("GALAXY_RT_SHADOW_QUALITY", "3");
        ScopedEnv rt_ao("GALAXY_RT_AO_ENABLED", "1");
        ScopedEnv rt_reflect("GALAXY_RT_REFLECTIONS", "0");
        galaxy::gx::load_render_config_from_file();
    }
    const galaxy::gx::RenderConfig loaded =
        galaxy::gx::get_render_config();
    passed &= expect(
        loaded.msaa_samples == 8 &&
            !loaded.taa_enabled &&
            loaded.ray_tracing_enabled &&
            loaded.rt_shadow_quality == 0 &&
            !loaded.rt_ao_enabled &&
            !loaded.rt_reflections,
        "file/env render config enables ray tracing, takes its reflections choice, and no unimplemented feature");

    {
        ScopedEnv lod_bias("GALAXY_TEXTURE_LOD_BIAS", "99");
        ScopedEnv aniso("GALAXY_ANISOTROPIC_FILTERING", "99");
        ScopedEnv mipmaps("GALAXY_ENHANCED_MIPMAPS", "0");
        galaxy::gx::load_render_config_from_file();
    }
    const galaxy::gx::RenderConfig quality =
        galaxy::gx::get_render_config();
    passed &= expect(
        quality.texture_lod_bias == 8,
        "texture LOD bias env is clamped to the supported range");
    passed &= expect(
        quality.anisotropic_filtering == 16,
        "anisotropic filtering env is clamped to the supported range");
    passed &= expect(
        !quality.enhanced_mipmaps,
        "enhanced mipmaps env override is applied");

    {
        ScopedEnv efb_scale("GALAXY_EFB_SCALE", "4");
        galaxy::gx::load_render_config_from_file();
        passed &= expect(
            galaxy::gx::get_render_config().efb_scale == 4u,
            "EFB scale 4x is accepted from the release environment");
    }
    {
        ScopedEnv efb_scale("GALAXY_EFB_SCALE", "6");
        galaxy::gx::load_render_config_from_file();
        passed &= expect(
            galaxy::gx::get_render_config().efb_scale == 6u,
            "opt-in EFB scale 6x is accepted from the release environment");
    }
    {
        ScopedEnv efb_scale("GALAXY_EFB_SCALE", "0");
        galaxy::gx::load_render_config_from_file();
        passed &= expect(
            galaxy::gx::get_render_config().efb_scale == 1u,
            "unsupported low EFB scale clamps to native 1x");
    }

    {
        ScopedEnv wait("GALAXY_WAIT_FOR_FRAME_LATENCY", "0");
        ScopedEnv latency("GALAXY_SWAP_CHAIN_FRAME_LATENCY", "99");
        ScopedEnv tearing("GALAXY_ALLOW_TEARING", "1");
        ScopedEnv discard("GALAXY_FLIP_DISCARD", "0");
        galaxy::gx::load_render_config_from_file();
    }
    const galaxy::gx::RenderConfig pacing =
        galaxy::gx::get_render_config();
    passed &= expect(
        !pacing.wait_for_frame_latency,
        "frame latency wait env override is applied");
    passed &= expect(
        pacing.swap_chain_frame_latency == 2,
        "swap-chain latency env override is clamped to the supported range");
    passed &= expect(
        pacing.allow_tearing,
        "allow tearing env override is applied");
    passed &= expect(
        !pacing.flip_discard,
        "flip discard env override is applied");

    {
        ScopedEnv decouple("GALAXY_FRAME_RATE_DECOUPLE", "1");
        galaxy::gx::load_render_config_from_file();
    }
    passed &= expect(
        !galaxy::gx::get_render_config().frame_rate_decouple,
        "frame-rate decoupling stays off");

    galaxy::gx::RenderFeatureCaps caps{};
    caps.d3d12_device_created = true;
    caps.d3d_feature_level = 0xC100u;
    caps.adapter_vendor_id = 0x10DEu;
    caps.adapter_device_id = 0x2684u;
    caps.adapter_subsys_id = 0x12345678u;
    caps.adapter_revision = 0xA1u;
    caps.adapter_luid_low = 0x11223344u;
    caps.adapter_luid_high = 0x55667788u;
    std::snprintf(
        caps.adapter_description.data(),
        caps.adapter_description.size(),
        "Synthetic RTX Adapter");
    caps.dxr_device5_supported = true;
    caps.dxr_raytracing_tier = 11;
    caps.streamline_bridge_built = false;
    caps.dxr = galaxy::gx::make_render_feature_status(
        false,
        true,
        false,
        false,
        "DXR runtime is available; native RT effects are not built");
    caps.reflex = galaxy::gx::make_render_feature_status(
        false,
        false,
        false,
        true,
        "Streamline Reflex integration is not built");
    caps.dlss_super_resolution = galaxy::gx::make_render_feature_status(
        true,
        true,
        false,
        true,
        "available");
    caps.dlss_frame_generation = galaxy::gx::make_render_feature_status(
        true,
        true,
        true,
        true,
        "available");
    galaxy::gx::set_render_feature_caps(caps);

    const galaxy::gx::RenderFeatureCaps readback =
        galaxy::gx::get_render_feature_caps();
    passed &= expect(
        readback.d3d12_device_created &&
            readback.d3d_feature_level == 0xC100u &&
            readback.adapter_vendor_id == 0x10DEu &&
            readback.adapter_device_id == 0x2684u &&
            readback.adapter_subsys_id == 0x12345678u &&
            readback.adapter_revision == 0xA1u &&
            readback.adapter_luid_low == 0x11223344u &&
            readback.adapter_luid_high == 0x55667788u &&
            std::strcmp(
                readback.adapter_description.data(),
                "Synthetic RTX Adapter") == 0 &&
            readback.dxr_device5_supported &&
            readback.dxr_raytracing_tier == 11 &&
            readback.dxr.runtime_supported &&
            !readback.dxr.implemented &&
            !readback.dxr.renderer_ready &&
            !readback.dxr.settings_exposed,
        "render feature caps preserve DXR runtime/support status");
    passed &= expect(
        std::strstr(readback.dxr.reason.data(), "native RT effects") != nullptr,
        "render feature caps keep diagnostic reason text");
    passed &= expect(
            !readback.streamline_bridge_built &&
            !readback.streamline.implemented &&
            !readback.streamline.runtime_supported &&
            !readback.streamline.renderer_ready &&
            !readback.streamline.settings_exposed &&
            !readback.reflex.implemented &&
            !readback.reflex.runtime_supported &&
            !readback.reflex.renderer_ready &&
            !readback.reflex.settings_exposed,
        "render feature caps keep Streamline-dependent features hidden");
    passed &= expect(
        !readback.dlss_super_resolution.renderer_ready &&
            !readback.dlss_super_resolution.settings_exposed &&
            !readback.dlss_frame_generation.settings_exposed,
        "DLSS feature caps cannot be exposed without renderer/Streamline/Reflex gates");

    caps.adapter_description.fill('x');
    caps.msaa.reason.fill('x');
    caps.taa.reason.fill('x');
    caps.dxr.reason.fill('x');
    caps.streamline.reason.fill('x');
    caps.reflex.reason.fill('x');
    caps.dlss_super_resolution.reason.fill('x');
    caps.dlss_frame_generation.reason.fill('x');
    galaxy::gx::set_render_feature_caps(caps);
    const auto terminated = galaxy::gx::get_render_feature_caps();
    passed &= expect(terminated.adapter_description.back() == '\0' &&
        terminated.msaa.reason.back() == '\0' && terminated.taa.reason.back() == '\0' &&
        terminated.dxr.reason.back() == '\0' && terminated.streamline.reason.back() == '\0' &&
        terminated.reflex.reason.back() == '\0' && terminated.dlss_super_resolution.reason.back() == '\0' &&
        terminated.dlss_frame_generation.reason.back() == '\0',
        "direct capability strings are always terminated before UI snapshots");

    if (!passed) {
        return 1;
    }

    std::cout << "Render config feature gate tests passed\n";
    return 0;
}
