#include "galaxy/render/render_config.h"
#include "galaxy/render/resolution_fit.h"
#include "galaxy/render/resolution_governor.h"
#include "galaxy/launch_args.h"
#include "galaxy/test_temp.h"
#include "galaxy/display_aspect.h"

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
            const auto sizes = aspect_window_sizes(mode, 4000u, 1400u);
            bool ok = sizes.size() >= 3u;
            for (std::size_t i = 0; i < sizes.size(); ++i) {
                const auto [w, h] = sizes[i];
                ok = ok && w * kAspectRatios[mode][1] == h * kAspectRatios[mode][0] &&
                     w <= 4000u && h <= 1400u && (i == 0u || sizes[i - 1u].second < h);
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
        // Every shape from a phone held upright to three 32:9 panels: the game's screen width in even units, 832 at
        // 16:9; 48:9 at 4K (11520 x 2160) three times as wide; anything wider held to 96:9.
        const auto units = [](double aspect) {
            const auto shape = galaxy::make_display_aspect(aspect);
            return shape ? shape->game_screen_width : 0u;
        };
        const auto held = [](double aspect) {
            const auto shape = galaxy::make_display_aspect(aspect);
            return shape ? shape->content_aspect : 0.0;
        };
        if (units(16.0 / 9.0) != 832u || units(4.0 / 3.0) != 624u || units(9.0 / 21.0) != 624u ||
            units(32.0 / 9.0) != 1664u || units(11520.0 / 2160.0) != 2496u || units(96.0 / 9.0) != 4992u ||
            units(40.0) != 4992u || held(40.0) != galaxy::kWidestContentAspect ||
            held(0.1) != galaxy::kNarrowestContentAspect || units(0.0) != 0u ||
            units(std::numeric_limits<double>::quiet_NaN()) != 0u ||
            galaxy::display_aspect_extent(11520, 2160) != (11520u << 16u | 2160u) ||
            galaxy::display_aspect_extent(15360, 1440) != (15360u << 16u | 1440u)) {
            std::cerr << "the game's shape must follow the output from 9:21 to 96:9 and hold beyond\n";
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
        {
            // The DXGI sync interval (Direct3D 12 and 11 share it, P-022).
            using galaxy::render::dxgi_sync_interval;
            int asked = 0;
            const auto on = [&asked] { ++asked; return true; };
            const auto off = [&asked] { ++asked; return false; };
            const bool sync_ok =
                dxgi_sync_interval(0u, false, 120.0, 0.0f, true, on) == 0u &&   // VSync off stays off
                dxgi_sync_interval(2u, false, 120.0, 0.0f, true, on) == 2u &&   // 2 asked stays 2
                dxgi_sync_interval(1u, true, 120.0, 30.0f, true, on) == 1u &&   // a variable refresh: as asked
                dxgi_sync_interval(1u, false, 0.0, 30.0f, true, on) == 1u &&    // an unknown display rate
                asked == 0 &&                                                    // none of these asks
                dxgi_sync_interval(1u, false, 120.0, 0.0f, true, on) == 1u &&   // a game frame at 60 goes out at once
                dxgi_sync_interval(1u, false, 240.0, 0.0f, true, off) == 1u &&
                asked == 0 &&                                                    // a game frame never asks
                dxgi_sync_interval(1u, false, 120.0, 30.0f, true, on) == 4u &&  // 30 on 120: four refreshes each
                dxgi_sync_interval(1u, false, 144.0, 30.0f, true, on) == 1u &&  // 30 on 144: no whole multiple
                dxgi_sync_interval(1u, false, 60.0, 30.0f, true, on) == 2u &&   // 30 on 60: two
                dxgi_sync_interval(1u, false, 240.0, 120.0f, false, on) == 2u && // in-between pictures at half the rate
                dxgi_sync_interval(1u, false, 119.0, 59.94f, false, on) == 2u && // 119 for 119.88: within 1.5 %
                dxgi_sync_interval(1u, false, 120.0, 0.0f, false, off) == 1u &&  // no in-between pictures, 120: at once
                asked == 3;
            if (!sync_ok) {
                std::cerr << "the DXGI sync interval must keep whole refreshes only for a rate that is a part of the display's\n";
                passed = false;
            }
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
            interpolated_frames_per_game_frame(480.0) != 7u ||
            interpolated_frames_per_game_frame(500.0) != 8u ||
            interpolated_frames_per_game_frame(540.0) != 8u ||
            interpolated_frames_per_game_frame(1000.0) != 16u ||
            interpolated_frames_per_game_frame(1e12) != galaxy::render::kUnboundedGeneratedFrames ||
            interpolated_frames_per_game_frame(std::numeric_limits<double>::infinity()) !=
                galaxy::render::kUnboundedGeneratedFrames ||
            interpolated_frames_per_game_frame(std::numeric_limits<double>::quiet_NaN()) != 0u) {
            std::cerr << "interpolated frames must be ceil(rate / 60) - 1, unbounded at an unlimited rate\n";
            passed = false;
        }
    }

    {
        const auto root = galaxy::test_temp_path("galaxy_config_validation");
        const auto path = root / "config.ini";
        std::filesystem::create_directories(root);
        const ScopedSettingsFolder config_folder(root);
        for (const char* text : {"nan", "inf", "-1", "60junk", "0.00001", "1e100", "Unlimited", "unlimited60", "-inf"}) {
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
                -std::numeric_limits<float>::infinity(), -1.0f, 0.00001f}) {
            galaxy::render::RenderConfig direct{};
            direct.max_fps = invalid;
            direct.texture_lod_bias = 99u;
            direct.anisotropic_filtering = 0u;
            galaxy::render::set_render_config(direct);
            const auto value = galaxy::render::get_render_config();
            passed &= expect(value.max_fps == 60.0f && value.texture_lod_bias == 8u &&
                value.anisotropic_filtering == 1u, "direct config applies the same numeric boundary checks");
        }
        for (float rate : {1.0f, 30.0f, 59.940002f, 60.0f, 10000000.0f, galaxy::render::kUnlimitedFrameRate}) {
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
            // The bloom's strength, away from its default.
            galaxy::render::RenderConfig original{};
            original.bloom = 140u;
            galaxy::render::set_render_config(original);
            passed &= expect(galaxy::render::save_render_config_to_file(), "bloom fixture saves");
            galaxy::render::set_render_config(galaxy::render::RenderConfig{});
            galaxy::render::load_render_config_from_file();
            passed &= expect(galaxy::render::get_render_config().bloom == 140u, "the bloom's strength survives a save");
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
    galaxy::render::set_render_config(config);

    const galaxy::render::RenderConfig sanitized =
        galaxy::render::get_render_config();
    passed &= expect(
        sanitized.msaa_samples == 8,
        "set_render_config keeps MSAA 8x");

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
        passed &= expect(loaded.msaa_samples == 8 && loaded == load("msaa_samples=8\n"),
                         "a file's keys it no longer has (taa, ray tracing's) are ignored");

        const galaxy::render::RenderConfig quality =
            load("texture_lod_bias=99\nanisotropic_filtering=99\nenhanced_mipmaps=1\n");
        passed &= expect(
            quality.texture_lod_bias == 8,
            "texture LOD bias is clamped to the supported range");
        passed &= expect(
            quality.anisotropic_filtering == 16,
            "anisotropic filtering is clamped to the supported range");

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
    caps.streamline_bridge_built = false;
    caps.msaa = galaxy::render::make_render_feature_status(
        true,
        true,
        true,
        true,
        "scene target targets multisampled");
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
            readback.msaa.runtime_supported &&
            readback.msaa.implemented &&
            readback.msaa.renderer_ready &&
            readback.msaa.settings_exposed,
        "render feature caps preserve a feature's status");
    passed &= expect(
        std::strstr(readback.msaa.reason.data(), "multisampled") != nullptr,
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
    caps.streamline.reason.fill('x');
    caps.reflex.reason.fill('x');
    caps.dlss_super_resolution.reason.fill('x');
    caps.dlss_frame_generation.reason.fill('x');
    galaxy::render::set_render_feature_caps(caps);
    const auto terminated = galaxy::render::get_render_feature_caps();
    passed &= expect(terminated.adapter_description.back() == '\0' &&
        terminated.msaa.reason.back() == '\0' && terminated.taa.reason.back() == '\0' &&
        terminated.streamline.reason.back() == '\0' &&
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
                             preset.max_fps == 0.0f && preset.dynamic_resolution && preset.frame_generation == 0u &&
                             !preset.hdr &&
                             preset.sharpness == 0u && preset.anisotropic_filtering == 1u,
                         "the performance preset turns every optional pass off at 480 lines, at the display's rate with "
                         "dynamic resolution");
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
        passed &= expect(resolution_rectangle(1920u, 1080u, 16u) == std::pair{1920u, 1080u} &&
                             resolution_rectangle(1919u, 1081u, 16u) == std::pair{1919u, 1081u} &&
                             resolution_rectangle(1920u, 1080u, 12u) == std::pair{1440u, 810u} &&
                             resolution_rectangle(854u, 480u, 14u) == std::pair{747u, 420u} &&
                             resolution_rectangle(1920u, 1080u, 11u) == std::pair{1320u, 743u} &&
                             resolution_rectangle(1u, 1u, 8u) == std::pair{1u, 1u},
                         "a step's rectangle is the render size at 16/16 and n/16 of it rounded once otherwise");
        using galaxy::render::resolution_step_at_least;
        passed &= expect(resolution_step_at_least(16u) == 0u && resolution_step_at_least(12u) == 4u &&
                             resolution_step_at_least(1u) == 8u && resolution_step_at_least(17u) == 0u,
                         "the lowest step a floor allows");
        ResolutionGovernor governor;
        const double budget = 13.33;
        constexpr double at50 = 20.0;  // 50 pictures a second: exact sums, the picture counts decide
        bool held = true;
        for (unsigned f = 0; f < 200u; ++f) {
            held &= governor.update(10.0, budget, at50) == 16u;
        }
        passed &= expect(held, "a GPU inside 90 % of the budget keeps the whole render size");
        unsigned frames = 0u;
        while (governor.sixteenths() == 16u && frames < 100u) {
            (void)governor.update(20.0, budget, at50);
            ++frames;
        }
        // 20 ms at 256/256; 80 % of the budget is 10.66 ms: 12/16 would take 11.25 ms, 11/16 9.45 ms. So 11/16.
        passed &= expect(frames == ResolutionGovernor::kOverPictures && governor.sixteenths() == 11u,
                         "over the budget: down after three pictures and 50 ms, to the largest step that fits");
        // At 11/16 the scene takes 9.45 ms; 12/16 would take 11.25 ms, over 75 %: it stays.
        held = true;
        for (unsigned f = 0; f < 300u; ++f) {
            held &= governor.update(9.45, budget, at50) == 11u;
        }
        passed &= expect(held, "no step up while the next step would pass 75 % of the budget");
        // A lighter scene: 5 ms at 11/16 predicts 5.95 ms at 12/16: one step up after 60 pictures (1.2 s).
        frames = 0u;
        while (governor.sixteenths() == 11u && frames < 400u) {
            (void)governor.update(5.0, budget, at50);
            ++frames;
        }
        passed &= expect(governor.sixteenths() == 12u && frames == ResolutionGovernor::kRoomPictures,
                         "room for the next step: up one step after 60 pictures and 1 s");
        // The floor: a GPU far over the budget stops at 8/16.
        for (unsigned f = 0; f < 400u; ++f) {
            (void)governor.update(200.0, budget, at50);
        }
        passed &= expect(governor.sixteenths() == 8u, "never below half of each side");
        governor.reset();
        passed &= expect(governor.sixteenths() == 16u, "reset returns to the whole render size");
        held = true;
        for (unsigned f = 0; f < 50u; ++f) {
            held &= governor.update(0.0, budget, at50) == 16u;
        }
        passed &= expect(held, "no measurement moves nothing");

        // Thousands of pictures a second: every wait is a time too, so the step changes at the same pace.
        ResolutionGovernor fast;
        constexpr double at4000 = 0.25;
        unsigned pictures = 0u;
        while (fast.sixteenths() == 16u && pictures < 10000u) {
            (void)fast.update(20.0, budget, at4000);
            ++pictures;
        }
        // 800 settling pictures (0.2 s), then 200 over (50 ms).
        passed &= expect(pictures == 1000u && fast.sixteenths() == 11u,
                         "at 4,000 pictures a second the settle and the run over are 0.2 s and 50 ms");
        pictures = 0u;
        while (fast.sixteenths() == 11u && pictures < 10000u) {
            (void)fast.update(5.0, budget, at4000);
            ++pictures;
        }
        passed &= expect(pictures == 800u + 4000u && fast.sixteenths() == 12u,
                         "at 4,000 pictures a second a step up waits 0.2 s settling and 1 s of room");

        // An upscaler's floor: no step below it, and a step below it moves up at once.
        ResolutionGovernor floored;
        floored.set_floor(12u);
        for (unsigned f = 0; f < 400u; ++f) {
            (void)floored.update(200.0, budget, at50);
        }
        passed &= expect(floored.sixteenths() == 12u && floored.floor_sixteenths() == 12u, "never below the floor");
        floored.set_floor(14u);
        passed &= expect(floored.sixteenths() == 14u, "a higher floor lifts the step at once");
        floored.set_floor(8u);
        passed &= expect(floored.sixteenths() == 14u, "a lower floor leaves the step");
    }

    if (!passed) {
        return 1;
    }

    std::cout << "Render config feature gate tests passed\n";
    return 0;
}
