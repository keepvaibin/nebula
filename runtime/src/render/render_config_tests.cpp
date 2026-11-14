#include "galaxy/render/render_config.h"
#include "galaxy/render/resolution_fit.h"
#include "galaxy/render/resolution_governor.h"
#include "galaxy/launch_args.h"
#include "galaxy/test_temp.h"
#include "galaxy/experimental_ultrawide_aspect.h"

#include "galaxy/windows_api.h"

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

// The run's --settings folder for one scope.
class ScopedSettingsFolder {
public:
    explicit ScopedSettingsFolder(const std::filesystem::path& folder) : old_(galaxy::g_launch_args.settings) {
        galaxy::g_launch_args.settings = folder;
    }
    ~ScopedSettingsFolder() { galaxy::g_launch_args.settings = old_; }
    ScopedSettingsFolder(const ScopedSettingsFolder&) = delete;
    ScopedSettingsFolder& operator=(const ScopedSettingsFolder&) = delete;

private:
    std::filesystem::path old_;
};

}  // namespace

int main() {
    bool passed = true;

    {
        const auto same = [](const std::vector<unsigned>& got, std::initializer_list<unsigned> want) {
            return std::equal(got.begin(), got.end(), want.begin(), want.end());
        };
        using galaxy::render::aspect_window_sizes;
        using galaxy::render::kAspectRatios;
        if (!aspect_window_sizes(0u, 4000u, 4000u).empty()) {
            std::cerr << "the Auto aspect has no fixed window sizes\n";
            passed = false;
        }
        for (unsigned mode = 1u; mode < galaxy::render::kAspectModeCount; ++mode) {
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
        using galaxy::render::present_frame_rate;
        if (present_frame_rate(30.0f, 1u, 144.0) != 30.0 || present_frame_rate(144.0f, 1u, 144.0) != 144.0 ||
            present_frame_rate(240.0f, 1u, 144.0) != 144.0 || present_frame_rate(240.0f, 0u, 144.0) != 240.0 ||
            present_frame_rate(60.0f, 1u, 50.0) != 50.0 || present_frame_rate(0.0f, 0u, 50.0) != 60.0 ||
            present_frame_rate(0.0f, 1u, 240.0) != 240.0 || present_frame_rate(0.0f, 0u, 165.0) != 165.0 ||
            present_frame_rate(0.0f, 0u, 0.0) != 60.0) {
            std::cerr << "present rate must follow the cap, the display rate and VSync\n";
            passed = false;
        }
        using galaxy::render::interpolated_frames_per_game_frame;
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
        const auto root = galaxy::test_temp_path("galaxy_config_validation");
        const auto path = root / "config.ini";
        std::filesystem::create_directories(root);
        const ScopedSettingsFolder config_folder(root);
        for (const char* text : {"nan", "inf", "-1", "60junk", "0.00001", "10000000", "1e100"}) {
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << "max_fps=" << text << '\n';
            }
            galaxy::render::load_render_config_from_file();
            passed &= expect(galaxy::render::get_render_config().max_fps == 60.0f,
                "malformed and unsafe FPS text preserves the prior cap");
        }
        {
            // The GPU chosen in Nebula Settings (config.ini gpu): the game starts on it and keeps it for the
            // session; a GPU chosen while it runs is kept in the file for the next start.
            passed &= expect(galaxy::render::gpu_identity(0x10DEu, 0x2B85u, 0x89EF1043u) == "10de:2b85:89ef1043" &&
                    galaxy::render::gpu_identity(0x1002u, 0x13C0u, 0x7E491462u) == "1002:13c0:7e491462",
                "a GPU's identity is its vendor, device and subsystem id in lowercase hex");
            const std::string started = galaxy::render::gpu_identity(0x1002u, 0x13C0u, 0x7E491462u);
            const std::string chosen = galaxy::render::gpu_identity(0x10DEu, 0x2B85u, 0x89EF1043u);
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
            const auto session_gpu = [] { return std::string(galaxy::render::get_render_config().gpu.data()); };

            passed &= expect(write("gpu=" + started + "\naa=1\n", 132'000'000'000'000'000ull), "gpu fixture written");
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            galaxy::render::load_render_config_from_file();
            passed &= expect(session_gpu() == started && galaxy::render::next_start_gpu() == started,
                "the game starts on the GPU config.ini names");
            passed &= expect(galaxy::render::save_render_config_to_file() && saved_gpu() == started,
                "the game's save keeps it");

            passed &= expect(write("gpu=" + chosen + "\naa=2\n", 132'000'000'100'000'000ull), "gpu fixture rewritten");
            passed &= expect(galaxy::render::reload_render_config_if_changed() && galaxy::render::get_render_config().aa_mode == 2u &&
                    session_gpu() == started && galaxy::render::next_start_gpu() == chosen,
                "a GPU chosen while the game runs is read for the next start; the session keeps its GPU");
            passed &= expect(galaxy::render::save_render_config_to_file() && saved_gpu() == chosen,
                "the game's save keeps the GPU chosen during the session");

            // While the in-game menu is open the game does not re-read config.ini; its own save still keeps
            // the gpu the file has.
            passed &= expect(write("gpu=\naa=2\n", 132'000'000'200'000'000ull), "gpu fixture set to automatic");
            galaxy::render::RenderConfig live = galaxy::render::get_render_config();
            live.sharpness = 40u;
            galaxy::render::set_render_config(live);
            passed &= expect(galaxy::render::save_render_config_to_file() && saved_gpu().empty() &&
                    galaxy::render::next_start_gpu().empty() && session_gpu() == started,
                "a save that follows a change Nebula Settings made unread keeps that change's gpu");
            passed &= expect(write("gpu=" + chosen + "\n", 132'000'000'300'000'000ull), "gpu fixture rewritten again");
            live.gpu.fill('\0');
            std::memcpy(live.gpu.data(), started.c_str(), started.size());
            galaxy::render::set_render_config(live);
            passed &= expect(galaxy::render::save_render_config_to_file() && saved_gpu() == chosen,
                "the game's own gpu is never what it saves");
            passed &= expect(write("aa=1\n", 132'000'000'400'000'000ull), "gpu fixture without the key");
            galaxy::render::load_render_config_from_file();
            passed &= expect(galaxy::render::get_render_config().gpu[0] == '\0' && galaxy::render::next_start_gpu().empty() &&
                    galaxy::render::save_render_config_to_file() && saved_gpu().empty(),
                "a file without the key is the automatic choice, and saves as one");
        }
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(), -1.0f, 0.00001f, 10000000.0f}) {
            galaxy::render::RenderConfig direct{};
            direct.max_fps = invalid;
            direct.texture_lod_bias = 99u;
            direct.anisotropic_filtering = 0u;
            galaxy::render::set_render_config(direct);
            const auto value = galaxy::render::get_render_config();
            passed &= expect(value.max_fps == 60.0f && value.texture_lod_bias == 8u &&
                value.anisotropic_filtering == 1u, "direct config applies the same numeric boundary checks");
        }
#if !defined(NEBULA_GPU_METAL)
        {
            // The tracer running: the scene single-sampled, anti-aliased by FSR 3; the player's choices kept (plan 16.3).
            galaxy::render::RenderConfig chosen{};
            chosen.msaa_samples = 4u;
            chosen.upscaler = 1u;
            chosen.spatial_quality = 3u;
            galaxy::render::set_render_config(chosen);
            galaxy::render::set_render_tracing(true, true);
            const auto traced = galaxy::render::get_render_config();
            const auto kept = galaxy::render::get_render_config_chosen();
            galaxy::render::set_render_tracing(true, false);
            const auto no_fsr3 = galaxy::render::get_render_config();
            galaxy::render::set_render_tracing(false, false);
            const auto after = galaxy::render::get_render_config();
            passed &= expect(traced.msaa_samples == 1u && traced.upscaler == 2u && traced.temporal_quality == 3u &&
                    kept.msaa_samples == 4u && kept.upscaler == 1u && no_fsr3.msaa_samples == 1u && no_fsr3.upscaler == 1u &&
                    after == galaxy::render::get_render_config_chosen() && after.msaa_samples == 4u,
                "with the tracer running the scene is single-sampled and FSR 3 anti-aliases it; off, the choices return");
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
        }
#endif
        for (float rate : {1.0f, 30.0f, 59.940002f, 60.0f}) {
            galaxy::render::RenderConfig original{};
            original.max_fps = rate;
            galaxy::render::set_render_config(original);
            passed &= expect(galaxy::render::save_render_config_to_file(), "FPS precision fixture saves");
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            galaxy::render::load_render_config_from_file();
            passed &= expect(galaxy::render::get_render_config().max_fps == rate,
                "frame-rate caps survive exact float round trips");
        }
        {
            // The bloom's strength and ray tracing's parts, each away from its default (plan 19.1).
            using galaxy::render::RtPart;
            galaxy::render::RenderConfig original{};
            original.bloom = 140u;
            original.rt_lens_bloom = false;
            original.rt_quality = galaxy::render::kRtPresetCustom;
            original.rt_parts = {3u, 0u, 1u, 2u, 3u, 0u};
            original.rt_probe_ms = 0.75f;
            original.rt_chosen = true;
            galaxy::render::set_render_config(original);
            passed &= expect(galaxy::render::save_render_config_to_file(), "bloom and parts fixture saves");
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            galaxy::render::load_render_config_from_file();
            const auto loaded = galaxy::render::get_render_config();
            passed &= expect(loaded.bloom == 140u && !loaded.rt_lens_bloom && loaded.rt_quality == galaxy::render::kRtPresetCustom &&
                                 loaded.rt_parts == original.rt_parts && galaxy::render::rt_part(loaded, RtPart::Lights) == 0u &&
                                 loaded.rt_probe_ms == 0.75f && loaded.rt_chosen,
                             "the bloom's strength, a custom set of parts and the tracer's measure survive a save");
            galaxy::render::RenderConfig low{};
            low.rt_quality = 2u;
            galaxy::render::set_render_config(low);
            const auto set_low = galaxy::render::get_render_config();
            const galaxy::render::RenderConfig defaults{};
            passed &= expect(set_low.rt_parts == std::array<std::uint8_t, 6>{1u, 1u, 1u, 0u, 1u, 1u} && defaults.rt_quality == 0u &&
                                 defaults.rt_parts == std::array<std::uint8_t, 6>{2u, 2u, 2u, 2u, 2u, 2u} && defaults.rt_lens_bloom &&
                                 defaults.rt_probe_ms == 0.0f && !defaults.rt_chosen,
                             "a preset sets every part; medium by default");
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
        }
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << std::string(511u, 'x') << "max_fps=0\n";
        }
        galaxy::render::set_render_config(galaxy::render::RenderConfig{});
        galaxy::render::load_render_config_from_file();
        passed &= expect(galaxy::render::get_render_config().max_fps == 60.0f,
            "an overlong physical INI line cannot smuggle an unlimited-cap suffix");
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "max_fps=0" << std::string(260u, ' ') << "junk\n";
        }
        galaxy::render::load_render_config_from_file();
        passed &= expect(galaxy::render::get_render_config().max_fps == 60.0f,
            "an oversized value cannot hide a malformed suffix beyond the parser buffer");
        {
            const auto unicode_folder = root / L"\u03a9-\u65e5";
            std::filesystem::create_directories(unicode_folder);
            const auto unicode_path = unicode_folder / "config.ini";
            const ScopedSettingsFolder unicode_settings(unicode_folder);
            galaxy::render::RenderConfig value{};
            value.max_fps = 59.940002f;
            galaxy::render::set_render_config(value);
            passed &= expect(galaxy::render::save_render_config_to_file() &&
                std::filesystem::exists(unicode_path), "the explicit Unicode path is preserved by publication");
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            galaxy::render::load_render_config_from_file();
            passed &= expect(galaxy::render::get_render_config().max_fps == value.max_fps,
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
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            galaxy::render::load_render_config_from_file();
            passed &= expect(!galaxy::render::reload_render_config_if_changed(), "an unchanged config.ini is not re-read");
            galaxy::render::RenderConfig live = galaxy::render::get_render_config();
            live.per_pixel_lighting = true;
            galaxy::render::set_render_config(live);
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << "aa=3\nsharpness=50\n";
            }
            passed &= expect(stamp(133'000'000'000'000'000ull), "config fixture time kept");
            passed &= expect(!galaxy::render::reload_render_config_if_changed() &&
                    galaxy::render::get_render_config().aa_mode == 1u,
                "the same last-write time keeps the live settings");
            passed &= expect(stamp(133'000'000'100'000'000ull), "config fixture time advanced");
            passed &= expect(galaxy::render::reload_render_config_if_changed(), "a newer config.ini is re-read");
            const galaxy::render::RenderConfig reloaded = galaxy::render::get_render_config();
            passed &= expect(reloaded.aa_mode == 3u && reloaded.sharpness == 50u && reloaded.per_pixel_lighting,
                "the file's settings apply over the live ones, and a setting it does not name keeps its value");
            passed &= expect(galaxy::render::save_render_config_to_file() && !galaxy::render::reload_render_config_if_changed(),
                "the game's own save is not read back");
        }
        std::filesystem::remove_all(root);
    }

    const galaxy::render::RenderConfig defaults{};
    passed &= expect(
        defaults.vsync_interval == 0 &&
            defaults.max_fps == 60.0f,
        "default Release config keeps a 60 Hz cap for launches without a realtime clock "
        "and the flip-discard swap chain");

    const galaxy::render::RenderTimingConfig& timing = galaxy::render::get_frame_timing_config();
    passed &= expect(
        timing.render_queue_depth == 2,
        "the timing is the shipped profile: queue depth 2");

    galaxy::render::RenderConfig config{};
    config.msaa_samples = 8;
    config.taa_enabled = true;
    config.ray_tracing_enabled = true;
    config.rt_quality = galaxy::render::kRtPresetCustom;
    config.rt_parts[static_cast<std::size_t>(galaxy::render::RtPart::Reflect)] = 0u;
    galaxy::render::set_render_config(config);

    const galaxy::render::RenderConfig sanitized =
        galaxy::render::get_render_config();
    passed &= expect(
        sanitized.msaa_samples == 8,
        "set_render_config keeps MSAA 8x");
    passed &= expect(
        !sanitized.taa_enabled,
        "set_render_config clamps unimplemented TAA off");
    passed &= expect(
        sanitized.ray_tracing_enabled &&
            galaxy::render::rt_part(sanitized, galaxy::render::RtPart::Reflect) == 0u &&
            sanitized.rt_quality == galaxy::render::kRtPresetCustom,
        "set_render_config keeps ray tracing and its reflections choice");

    {
        const std::filesystem::path root = galaxy::test_temp_path("galaxy_render_config_tests");
        const std::filesystem::path path = root / "config.ini";
        std::filesystem::remove_all(root);
        const ScopedSettingsFolder config_folder(root);
        const auto stage_bloom = [](unsigned bloom) {
            galaxy::render::RenderConfig staged = galaxy::render::get_render_config();
            staged.bloom = bloom;
            galaxy::render::set_render_config(staged);
        };

        stage_bloom(20u);
        passed &= expect(
            galaxy::render::save_render_config_to_file(),
            "a setting saves to an isolated config file");
        std::ifstream file(path, std::ios::binary);
        const std::string contents{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
        file.close();
        passed &= expect(contents.find("bloom=20") != std::string::npos, "the saved config holds the setting");

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
        stage_bloom(30u);
        const bool locked_save = galaxy::render::save_render_config_to_file();
        std::ifstream unchanged_file(path, std::ios::binary);
        const std::string unchanged{
            std::istreambuf_iterator<char>(unchanged_file),
            std::istreambuf_iterator<char>()};
        unchanged_file.close();
        passed &= expect(
            !locked_save &&
                unchanged.find("bloom=20") != std::string::npos &&
                unchanged.find("bloom=30") == std::string::npos,
            "render settings replace failure preserves the complete old file");
        if (locked != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(locked);
        }
        stage_bloom(20u);
        passed &= expect(
            galaxy::render::save_render_config_to_file(),
            "render settings atomically recover after the destination unlocks");

        galaxy::render::load_render_config_from_file();
        passed &= expect(
            galaxy::render::get_render_config().bloom == 20u,
            "a new startup reads the persisted setting");
        std::filesystem::remove_all(root);
    }

    {
        const std::filesystem::path root = galaxy::test_temp_path("galaxy_custom_window_size_tests");
        const std::filesystem::path path = root / "config.ini";
        std::filesystem::remove_all(root);
        const ScopedSettingsFolder config_folder(root);
        galaxy::render::RenderConfig custom{};
        custom.internal_height = 240u;
        custom.window_width = 3440u;
        custom.window_height = 1440u;
        galaxy::render::set_render_config(custom);
        passed &= expect(
            galaxy::render::save_render_config_to_file(),
            "a non-preset window size persists to config.ini");
        galaxy::render::set_render_config(galaxy::render::RenderConfig{});
        galaxy::render::load_render_config_from_file();
        passed &= expect(
            galaxy::render::get_render_config().window_width == 3440u &&
                galaxy::render::get_render_config().window_height == 1440u,
            "a saved custom aspect and resolution survive reload");
        passed &= expect(galaxy::render::get_render_config().internal_height == 240u,
            "internal resolution persists independently of the output resolution");

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "aspect=4\n";
        }
        galaxy::render::load_render_config_from_file();
        passed &= expect(galaxy::render::get_render_config().aspect_mode == 5u,
                         "a numeric aspect is an index of the list before 3:2 (4 is 21:9)");
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "aspect=3:2\n";
        }
        galaxy::render::load_render_config_from_file();
        passed &= expect(galaxy::render::get_render_config().aspect_mode == 2u, "aspect=3:2 reads by name");
        {
            galaxy::render::RenderConfig wide = galaxy::render::get_render_config();
            wide.aspect_mode = 6u;
            galaxy::render::set_render_config(wide);
            passed &= expect(galaxy::render::save_render_config_to_file(), "the aspect saves");
            std::ifstream file(path, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            passed &= expect(text.find("aspect=32:9\n") != std::string::npos, "the aspect saves by name");
            file.close();
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            galaxy::render::load_render_config_from_file();
            passed &= expect(galaxy::render::get_render_config().aspect_mode == 6u, "a saved aspect name reads back");
            galaxy::render::set_render_config(custom);
            passed &= expect(galaxy::render::save_render_config_to_file(), "the custom size saves again");
        }

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "window_width=2561junk\n"
                    "window_height=2147483648\n";
        }
        galaxy::render::load_render_config_from_file();
        passed &= expect(
            galaxy::render::get_render_config().window_width == 3440u &&
                galaxy::render::get_render_config().window_height == 1440u,
            "numeric prefixes and dimensions beyond Win32 int are rejected");

        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "window_width=1\nwindow_height=2147483647\n";
        }
        galaxy::render::load_render_config_from_file();
        passed &= expect(
            galaxy::render::get_render_config().window_width == 1u &&
                galaxy::render::get_render_config().window_height ==
                    static_cast<unsigned>(std::numeric_limits<int>::max()),
            "positive non-preset Win32-representable dimensions parse exactly");

        {
            std::ofstream file(path, std::ios::binary | std::ios::app);
            file << "window_width=1920pixels\nwindow_height=0\n";
        }
        galaxy::render::load_render_config_from_file();
        passed &= expect(
            galaxy::render::get_render_config().window_width == 1u &&
                galaxy::render::get_render_config().window_height ==
                    static_cast<unsigned>(std::numeric_limits<int>::max()),
            "a malformed or zero window size after a good one is ignored");

        galaxy::render::RenderConfig direct{};
        direct.window_width = 0u;
        direct.window_height = std::numeric_limits<unsigned>::max();
        galaxy::render::set_render_config(direct);
        passed &= expect(
            galaxy::render::get_render_config().window_width == 1u &&
                galaxy::render::get_render_config().window_height ==
                    static_cast<unsigned>(std::numeric_limits<int>::max()),
            "direct render settings cannot escape signed Win32 dimensions");
        std::filesystem::remove_all(root);
    }

    {
        const auto root = galaxy::test_temp_path("galaxy_config_features");
        std::filesystem::create_directories(root);
        const ScopedSettingsFolder config_folder(root);
        const auto load = [&root](const char* text) {
            {
                std::ofstream file(root / "config.ini", std::ios::binary | std::ios::trunc);
                file << text;
            }
            galaxy::render::load_render_config_from_file();
            return galaxy::render::get_render_config();
        };
        const galaxy::render::RenderConfig loaded = load(
            "msaa_samples=8\ntaa_enabled=1\nray_tracing_enabled=1\nrt_quality=custom\nrt_reflect=off\nrt_sun=high\n");
        passed &= expect(
            loaded.msaa_samples == 8 &&
                !loaded.taa_enabled &&
                loaded.ray_tracing_enabled &&
                galaxy::render::rt_part(loaded, galaxy::render::RtPart::Reflect) == 0u &&
                galaxy::render::rt_part(loaded, galaxy::render::RtPart::Sun) == 3u,
            "the file's render config enables ray tracing, takes its parts by name, and no unimplemented feature");

        const galaxy::render::RenderConfig quality =
            load("texture_lod_bias=99\nanisotropic_filtering=99\nenhanced_mipmaps=0\n");
        passed &= expect(
            quality.texture_lod_bias == 8,
            "texture LOD bias is clamped to the supported range");
        passed &= expect(
            quality.anisotropic_filtering == 16,
            "anisotropic filtering is clamped to the supported range");
        passed &= expect(
            !quality.enhanced_mipmaps,
            "enhanced mipmaps off is applied");

        passed &= expect(
            !load("frame_rate_decouple=1\n").frame_rate_decouple,
            "frame-rate decoupling stays off");
        std::filesystem::remove_all(root);
    }

    galaxy::render::RenderFeatureCaps caps{};
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
    caps.dxr = galaxy::render::make_render_feature_status(
        false,
        true,
        false,
        false,
        "DXR runtime is available; native RT effects are not built");
    caps.reflex = galaxy::render::make_render_feature_status(
        false,
        false,
        false,
        true,
        "Streamline Reflex integration is not built");
    caps.dlss_super_resolution = galaxy::render::make_render_feature_status(
        true,
        true,
        false,
        true,
        "available");
    caps.dlss_frame_generation = galaxy::render::make_render_feature_status(
        true,
        true,
        true,
        true,
        "available");
    galaxy::render::set_render_feature_caps(caps);

    const galaxy::render::RenderFeatureCaps readback =
        galaxy::render::get_render_feature_caps();
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
    galaxy::render::set_render_feature_caps(caps);
    const auto terminated = galaxy::render::get_render_feature_caps();
    passed &= expect(terminated.adapter_description.back() == '\0' &&
        terminated.msaa.reason.back() == '\0' && terminated.taa.reason.back() == '\0' &&
        terminated.dxr.reason.back() == '\0' && terminated.streamline.reason.back() == '\0' &&
        terminated.reflex.reason.back() == '\0' && terminated.dlss_super_resolution.reason.back() == '\0' &&
        terminated.dlss_frame_generation.reason.back() == '\0',
        "direct capability strings are always terminated before UI snapshots");

    {
        // The first start: no config.ini; a saved file ends it.
        const std::filesystem::path root = galaxy::test_temp_path("galaxy_first_start_tests");
        std::filesystem::remove_all(root);
        const ScopedSettingsFolder config_folder(root);
        galaxy::render::set_render_config(galaxy::render::RenderConfig{});
        galaxy::render::load_render_config_from_file();
        passed &= expect(galaxy::render::render_config_first_start(), "no config.ini is the first start");
        passed &= expect(galaxy::render::save_render_config_to_file(), "the first start's settings save");
        galaxy::render::load_render_config_from_file();
        passed &= expect(!galaxy::render::render_config_first_start(), "a saved config.ini is no first start");
        std::filesystem::remove_all(root);
    }
    passed &= expect(galaxy::render::first_start_internal_height(true, 2400u) == 480u &&
                         galaxy::render::first_start_internal_height(true, 480u) == 0u &&
                         galaxy::render::first_start_internal_height(false, 2160u) == 1080u &&
                         galaxy::render::first_start_internal_height(false, 1080u) == 0u,
                     "the first start's table: 480 lines shared, 1080 dedicated, match display on outputs no taller");
    {
        galaxy::render::RenderConfig preset{};
        preset.upscaler = 2u;
        preset.aa_mode = 1u;
        preset.msaa_samples = 4u;
        preset.ambient_occlusion = true;
        preset.bloom = 150u;
        preset.enhanced_lighting = true;
        preset.max_fps = 144.0f;
        preset.vsync_interval = 1u;
        preset.curved_surfaces = true;
        galaxy::render::apply_performance_preset(preset);
        passed &= expect(preset.internal_height == 480u && preset.upscaler == 0u && preset.aa_mode == 0u && preset.msaa_samples == 1u &&
                             !preset.ambient_occlusion && preset.bloom == 0u && !preset.enhanced_lighting && !preset.per_pixel_lighting &&
                             preset.max_fps == 60.0f && preset.frame_generation == 0u && !preset.ray_tracing_enabled && !preset.hdr &&
                             preset.sharpness == 0u && preset.anisotropic_filtering == 1u && !preset.enhanced_mipmaps,
                         "the performance preset turns every optional pass off at 480 lines");
        passed &= expect(!preset.hud_output_resolution && preset.hud_internal_resolution && !galaxy::render::render_hud_at_output(preset),
                         "the performance preset draws the 2D layer at 480 lines and enlarges once at present");
        galaxy::render::RenderConfig exact{};
        exact.internal_height = 720u;
        const bool before = galaxy::render::render_hud_at_output(exact);
        exact.hud_internal_resolution = true;
        const bool internal = galaxy::render::render_hud_at_output(exact);
        exact.hud_output_resolution = true;
        const bool asked = galaxy::render::render_hud_at_output(exact);
        exact = {};
        exact.hud_internal_resolution = true;
        passed &= expect(before && !internal && asked && !galaxy::render::render_hud_at_output(exact),
                         "an internal height takes the window-size 2D layer unless HUD at internal; DISPLAY wins");
        passed &= expect(preset.vsync_interval == 1u && preset.curved_surfaces && preset.post_process,
                         "the performance preset keeps the present path and choices that are no pass");
    }
    {
        using galaxy::render::GpuTimeFit;
        // gpu ms = 2 + 4 a million pixels, sampled at 480 and 1080 lines of a 1920x1080 output.
        const double p480 = galaxy::render::scene_pixels(480u, 1920u, 1080u), p1080 = galaxy::render::scene_pixels(0u, 1920u, 1080u);
        const double pixels[] = {p480, p480, p1080, p1080};
        const double ms[] = {2.0 + 4e-6 * p480, 2.0 + 4e-6 * p480, 2.0 + 4e-6 * p1080, 2.0 + 4e-6 * p1080};
        const GpuTimeFit fit = galaxy::render::fit_gpu_time(pixels, ms, 4u);
        passed &= expect(fit.valid && std::fabs(fit.a_ms - 2.0) < 1e-9 && std::fabs(fit.b_ms_per_pixel - 4e-6) < 1e-12,
                         "the fit finds the line through its samples");
        passed &= expect(!galaxy::render::fit_gpu_time(pixels, ms, 2u).valid, "one pixel count fixes no fit");
        passed &= expect(p480 == 853.0 * 480.0 || p480 == 854.0 * 480.0, "480 lines of 16:9 are about 854 x 480 pixels");
        // A budget of 0.8 x 16.67 = 13.3 ms. 480 lines: 2 + 1.64 = 3.64 ms; 720: 2 + 3.69 = 5.69; 1080: 2 + 8.29 = 10.3.
        // x 2.5: 9.1, 14.2, 25.7, so 480; x 2: 7.3, 11.4, 20.6, so 720; x 1: all fit, so match display.
        const double budget = galaxy::render::kFitFrameShare * 1000.0 / 60.0;
        passed &= expect(galaxy::render::largest_fitting_height(fit, 1920u, 1080u, galaxy::render::kFitHeavyStage, budget) == 480u,
                         "a slow GPU's fit keeps 480 lines");
        passed &= expect(galaxy::render::largest_fitting_height(fit, 1920u, 1080u, 2.0, budget) == 720u &&
                             galaxy::render::largest_fitting_height(fit, 1920u, 1080u, 1.0, budget) == 0u,
                         "the largest offered height under the budget is chosen");
        GpuTimeFit fast = fit;
        fast.b_ms_per_pixel = 1e-7;
        passed &= expect(galaxy::render::largest_fitting_height(fast, 1920u, 1080u, galaxy::render::kFitHeavyStage, budget) == 0u,
                         "a fast GPU's fit matches the display");
        passed &= expect(galaxy::render::largest_fitting_height(GpuTimeFit{}, 1920u, 1080u, 1.0, budget) == 480u &&
                             galaxy::render::largest_fitting_height(fast, 854u, 480u, 1.0, budget) == 0u,
                         "no fit keeps 480 lines; an output of 480 lines matches the display");
        passed &= expect(galaxy::render::largest_fitting_height(fast, 1920u, 1080u, galaxy::render::kFitHeavyStage, budget, 2160u) == 2160u &&
                             galaxy::render::largest_fitting_height(fast, 1920u, 1080u, galaxy::render::kFitHeavyStage, budget, 1440u) == 1440u &&
                             galaxy::render::largest_fitting_height(fit, 1920u, 1080u, 1.0, budget, 2160u) == 0u,
                         "above the output only up to max_height, and only what fits");
    }

    {
        using galaxy::render::ResolutionGovernor;
        using galaxy::render::resolution_rectangle;
        passed &= expect(resolution_rectangle(1920u, 1080u, 8u) == std::pair{1920u, 1080u} &&
                             resolution_rectangle(1919u, 1081u, 8u) == std::pair{1919u, 1081u} &&
                             resolution_rectangle(1920u, 1080u, 6u) == std::pair{1440u, 810u} &&
                             resolution_rectangle(854u, 480u, 7u) == std::pair{747u, 420u} &&
                             resolution_rectangle(1u, 1u, 4u) == std::pair{1u, 1u},
                         "a step's rectangle is the target at 8/8 and n/8 of it rounded once otherwise");
        ResolutionGovernor governor;
        const double budget = 13.33;
        bool held = true;
        for (unsigned f = 0; f < 200u; ++f) {
            held &= governor.update(10.0, budget) == 8u;
        }
        passed &= expect(held, "a GPU inside 90 % of the budget keeps the whole target");
        unsigned frames = 0u;
        while (governor.eighths() == 8u && frames < 100u) {
            (void)governor.update(20.0, budget);
            ++frames;
        }
        // 20 ms at 64/64; 80 % of the budget is 10.67 ms: 7/8 would take 15.3, 6/8 11.25, 5/8 7.8. So 5/8.
        passed &= expect(frames == ResolutionGovernor::kOverFrames && governor.eighths() == 5u,
                         "over the budget: down after three frames, to the largest step that fits");
        // At 5/8 the scene takes 20 x 25/64 = 7.8 ms; 6/8 would take 11.25 ms, over 75 %: it stays.
        held = true;
        for (unsigned f = 0; f < 300u; ++f) {
            held &= governor.update(7.8, budget) == 5u;
        }
        passed &= expect(held, "no step up while the next step would pass 75 % of the budget");
        // A lighter scene: 5 ms at 5/8 predicts 7.2 ms at 6/8: one step up after 60 frames.
        frames = 0u;
        while (governor.eighths() == 5u && frames < 400u) {
            (void)governor.update(5.0, budget);
            ++frames;
        }
        passed &= expect(governor.eighths() == 6u && frames == ResolutionGovernor::kRoomFrames,
                         "room for the next step: up one step after 60 frames");
        governor.reset();
        passed &= expect(governor.eighths() == 8u, "reset returns to the whole target");
        held = true;
        for (unsigned f = 0; f < 50u; ++f) {
            held &= governor.update(0.0, budget) == 8u;
        }
        passed &= expect(held, "no measurement moves nothing");
    }

    if (!passed) {
        return 1;
    }

    std::cout << "Render config feature gate tests passed\n";
    return 0;
}
