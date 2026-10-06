#include "galaxy/gx/render_config.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

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
        for (float rate : {0.0f, 1.0f, 59.940002f, 1000.0f}) {
            galaxy::gx::RenderConfig original{};
            original.max_fps = rate;
            galaxy::gx::set_render_config(original);
            passed &= expect(galaxy::gx::save_render_config_to_file(), "FPS precision fixture saves");
            galaxy::gx::set_render_config(galaxy::gx::RenderConfig{});
            galaxy::gx::load_render_config_from_file();
            passed &= expect(galaxy::gx::get_render_config().max_fps == rate,
                "finite caps and unlimited survive exact float round trips");
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
        for (const char* disabled : {"false", "off", "no", "0"}) {
            ScopedEnv experimental("GALAXY_EXPERIMENTAL_FRAME_RATE_DECOUPLE", disabled);
            ScopedEnv decouple("GALAXY_FRAME_RATE_DECOUPLE", "1");
            galaxy::gx::load_render_config_from_file();
            passed &= expect(!galaxy::gx::get_render_config().frame_rate_decouple,
                "all documented false spellings keep experimental decoupling disabled");
        }
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
        std::filesystem::remove_all(root);
    }

    const galaxy::gx::RenderConfig defaults{};
    passed &= expect(
        defaults.vsync_interval == 0 &&
            !defaults.wait_for_frame_latency &&
            defaults.swap_chain_frame_latency == 2 &&
            !defaults.flip_discard &&
            defaults.max_fps == 60.0f,
        "default Release config keeps a 60 Hz cap for non-realtime-VI launches");

    const galaxy::gx::GxTimingConfig timing_defaults{};
    passed &= expect(
        timing_defaults.render_queue_depth == 3 &&
            !timing_defaults.vi_render_fifo_wait &&
            timing_defaults.pe_events_gpu_fence &&
            timing_defaults.render_live_memory_wait &&
            !timing_defaults.render_memory_snapshot,
        "default GX timing config is the Release q3/0/1/1/no-snapshot profile");

    {
        ScopedEnv queue_depth("GALAXY_RENDER_QUEUE_DEPTH", "");
        ScopedEnv vi_wait("GALAXY_VI_RENDER_FIFO_WAIT", "");
        ScopedEnv pe_fence("GALAXY_PE_EVENTS_GPU_FENCE", "");
        ScopedEnv live_wait("GALAXY_RENDER_LIVE_MEMORY_WAIT", "");
        ScopedEnv snapshot("GALAXY_RENDER_MEMORY_SNAPSHOT", "");
        const galaxy::gx::GxTimingConfig resolved =
            galaxy::gx::resolve_gx_timing_config_from_environment();
        const galaxy::gx::GxTimingConfig& shared =
            galaxy::gx::get_gx_timing_config();
        passed &= expect(
            resolved.render_queue_depth == 3 &&
                !resolved.vi_render_fifo_wait &&
                resolved.pe_events_gpu_fence &&
                resolved.render_live_memory_wait &&
                !resolved.render_memory_snapshot,
            "no-env GX timing resolution matches the Release q3/0/1/1/no-snapshot profile");
        passed &= expect(
            shared.render_queue_depth == resolved.render_queue_depth &&
                shared.vi_render_fifo_wait == resolved.vi_render_fifo_wait &&
                shared.pe_events_gpu_fence == resolved.pe_events_gpu_fence &&
                shared.render_live_memory_wait ==
                    resolved.render_live_memory_wait &&
                shared.render_memory_snapshot ==
                    resolved.render_memory_snapshot,
            "runtime GX timing getter publishes the resolved no-env profile");
    }

    {
        ScopedEnv queue_depth("GALAXY_RENDER_QUEUE_DEPTH", "1");
        ScopedEnv vi_wait("GALAXY_VI_RENDER_FIFO_WAIT", "1");
        ScopedEnv pe_fence("GALAXY_PE_EVENTS_GPU_FENCE", "0");
        ScopedEnv live_wait("GALAXY_RENDER_LIVE_MEMORY_WAIT", "0");
        ScopedEnv snapshot("GALAXY_RENDER_MEMORY_SNAPSHOT", "1");
        const galaxy::gx::GxTimingConfig resolved =
            galaxy::gx::resolve_gx_timing_config_from_environment();
        passed &= expect(
            resolved.render_queue_depth == 1 &&
                resolved.vi_render_fifo_wait &&
                !resolved.pe_events_gpu_fence &&
                !resolved.render_live_memory_wait &&
                resolved.render_memory_snapshot,
            "explicit GX timing env values preserve the q1/1/0/0/snapshot bisect");
    }

    {
        ScopedEnv queue_depth("GALAXY_RENDER_QUEUE_DEPTH", "2");
        ScopedEnv vi_wait("GALAXY_VI_RENDER_FIFO_WAIT", "off");
        ScopedEnv pe_fence("GALAXY_PE_EVENTS_GPU_FENCE", "yes");
        ScopedEnv live_wait("GALAXY_RENDER_LIVE_MEMORY_WAIT", "TRUE");
        ScopedEnv snapshot("GALAXY_RENDER_MEMORY_SNAPSHOT", "off");
        const galaxy::gx::GxTimingConfig resolved =
            galaxy::gx::resolve_gx_timing_config_from_environment();
        passed &= expect(
            resolved.render_queue_depth == 2 &&
                !resolved.vi_render_fifo_wait &&
                resolved.pe_events_gpu_fence &&
                resolved.render_live_memory_wait &&
                !resolved.render_memory_snapshot,
            "GX timing env parser accepts q2 and unambiguous boolean spellings");
    }

    {
        ScopedEnv queue_depth("GALAXY_RENDER_QUEUE_DEPTH", "99");
        ScopedEnv live_wait("GALAXY_RENDER_LIVE_MEMORY_WAIT", "1");
        ScopedEnv snapshot("GALAXY_RENDER_MEMORY_SNAPSHOT", "1");
        const galaxy::gx::GxTimingConfig resolved =
            galaxy::gx::resolve_gx_timing_config_from_environment();
        passed &= expect(
            resolved.render_queue_depth == 3 &&
                !resolved.render_memory_snapshot,
            "GX timing resolution clamps queue depth and reports the effective live-memory mode");
    }

    galaxy::gx::RenderConfig config{};
    config.msaa_samples = 8;
    config.taa_enabled = true;
    config.ray_tracing_enabled = true;
    config.rt_shadow_quality = 3;
    config.rt_ao_enabled = true;
    config.rt_reflections = true;
    config.swap_chain_frame_latency = 99;
    config.efb_scale = 99;
    galaxy::gx::set_render_config(config);

    const galaxy::gx::RenderConfig sanitized =
        galaxy::gx::get_render_config();
    passed &= expect(
        sanitized.msaa_samples == 1,
        "set_render_config clamps unimplemented MSAA off");
    passed &= expect(
        !sanitized.taa_enabled,
        "set_render_config clamps unimplemented TAA off");
    passed &= expect(
        !sanitized.ray_tracing_enabled &&
            sanitized.rt_shadow_quality == 0 &&
            !sanitized.rt_ao_enabled &&
            !sanitized.rt_reflections,
        "set_render_config clamps unimplemented DXR effects off");
    passed &= expect(
        sanitized.swap_chain_frame_latency == 2,
        "set_render_config clamps swap-chain latency to the frame ring");
    passed &= expect(
        sanitized.efb_scale == galaxy::gx::kMaxEfbScale,
        "set_render_config clamps EFB scale to the implemented maximum");

    galaxy::gx::stage_next_start_efb_scale(0u);
    passed &= expect(
        galaxy::gx::get_next_start_efb_scale() == 1u &&
            galaxy::gx::get_render_config().efb_scale == galaxy::gx::kMaxEfbScale,
        "next-start EFB scale clamps low without changing the live scale");
    galaxy::gx::stage_next_start_efb_scale(99u);
    passed &= expect(
        galaxy::gx::get_next_start_efb_scale() == galaxy::gx::kMaxEfbScale &&
            galaxy::gx::get_render_config().efb_scale == galaxy::gx::kMaxEfbScale,
        "next-start EFB scale clamps high independently of the live scale");

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
            galaxy::gx::get_render_config().efb_scale == 2u &&
                galaxy::gx::get_next_start_efb_scale() == 2u,
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
        ScopedEnv rt_reflect("GALAXY_RT_REFLECTIONS", "1");
        galaxy::gx::load_render_config_from_file();
    }
    const galaxy::gx::RenderConfig loaded =
        galaxy::gx::get_render_config();
    passed &= expect(
        loaded.msaa_samples == 1 &&
            !loaded.taa_enabled &&
            !loaded.ray_tracing_enabled &&
            loaded.rt_shadow_quality == 0 &&
            !loaded.rt_ao_enabled &&
            !loaded.rt_reflections,
        "file/env render config cannot enable unimplemented graphics features");

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
        ScopedEnv experimental("GALAXY_EXPERIMENTAL_FRAME_RATE_DECOUPLE", "0");
        galaxy::gx::load_render_config_from_file();
    }
    passed &= expect(
        !galaxy::gx::get_render_config().frame_rate_decouple,
        "frame-rate decouple remains gated behind the experimental opt-in");

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
