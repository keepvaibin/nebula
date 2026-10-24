#pragma once

// NVIDIA Streamline (DLSS super resolution, DLSS Frame Generation with Reflex),
// loaded at run time from a vendor folder the player installs: Nebula ships
// none of NVIDIA's binaries. The interposer must carry Streamline's signature;
// Streamline runs with manual hooking (only the swap chain's presentation and
// the command queue pass through its proxies) and without over-the-air
// updates. Render thread only. Built without the SDK headers
// (GALAXY_STREAMLINE_SDK), every call reports Streamline as absent.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <memory>
#include <string>

namespace galaxy::gx {

class TemporalUpscaler;

namespace streamline {

// GALAXY_STREAMLINE_DIR, else %LOCALAPPDATA%\Nebula\vendor\streamline-2.14.1.
[[nodiscard]] std::wstring default_directory();
// Before the swap chain exists: loads and initializes Streamline from
// `directory`. False when it is not installed, not signed or refused (OS,
// driver); the reason is logged. With `frame_generation` it also loads DLSS
// Frame Generation and Reflex when their plugins are installed: the swap chain
// is then Streamline's, and frame generation switches on and off while it runs.
bool startup(const std::wstring& directory, bool frame_generation = false) noexcept;
[[nodiscard]] bool active() noexcept;
// After the device exists: hands it to Streamline. True when DLSS super
// resolution runs on the adapter.
bool attach_device(ID3D12Device* device, const LUID& adapter) noexcept;
[[nodiscard]] bool dlss_supported() noexcept;
// Streamline's device proxy for creating the command queue, or `device`.
[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Device> queue_device(ID3D12Device* device) noexcept;
// Streamline's factory proxy for creating the swap chain (so its presents
// run through Streamline), or `factory`.
[[nodiscard]] Microsoft::WRL::ComPtr<IDXGIFactory4> swap_chain_factory(IDXGIFactory4* factory) noexcept;
// Before the swap chain and device are released.
void shutdown() noexcept;

// DLSS Frame Generation (after attach_device): it runs on the adapter, and the
// most frames it generates after each rendered one.
[[nodiscard]] bool frame_generation_supported() noexcept;
[[nodiscard]] unsigned frame_generation_max() noexcept;
// It varies the frames it generates to reach a target rate (dynamic multi frame generation).
[[nodiscard]] bool frame_generation_dynamic() noexcept;

// The presented frame (render thread). Every Streamline call for one presented
// frame (DLSS constants and evaluation, frame generation inputs, latency
// markers) takes the current frame's token; marker(kPresentEnd) moves on to
// the next frame.
enum class Marker : unsigned {
    kSimulationStart,
    kSimulationEnd,
    kRenderSubmitStart,
    kRenderSubmitEnd,
    kPresentStart,
    kPresentEnd,
};
[[nodiscard]] unsigned frame_index() noexcept;
void marker(Marker marker) noexcept;
// Reflex: waits until the frame's best start (frame generation only).
void reflex_sleep() noexcept;

// This frame's frame generation inputs: the scene's depth and motion vectors
// at the render size, in the states they are in on `list` (Streamline copies
// them there), and the backbuffer rectangle the game picture covers.
struct FrameGenerationInputs {
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    unsigned render_width = 0;
    unsigned render_height = 0;
};
bool tag_frame_generation(const FrameGenerationInputs& in) noexcept;
void tag_backbuffer_region(ID3D12GraphicsCommandList* list, unsigned left, unsigned top, unsigned width,
                           unsigned height) noexcept;
// The frame without its 2D layer at the backbuffer's size, in the render-target state until the
// present (null: none this frame).
void tag_hudless(ID3D12GraphicsCommandList* list, ID3D12Resource* hudless, unsigned width,
                 unsigned height) noexcept;
// Before the present: frames to generate after it (0: none; also while the
// frame carries no tagged scene: menus, loading), or with `dynamic` as many as
// reach `target_fps` (0: the display's refresh rate). Applied when it changes.
void set_frame_generation(unsigned generated, bool dynamic = false, float target_fps = 0.0f) noexcept;
// Frames the display showed per present, as Streamline last reported (1
// without frame generation).
[[nodiscard]] unsigned frames_presented() noexcept;

}  // namespace streamline

// DLSS super resolution for one render/output size. quality: 0 DLAA, 1
// quality, 2 balanced, 3 performance, 4 ultra performance. Null when DLSS is
// not available.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(
    unsigned render_width, unsigned render_height,
    unsigned output_width, unsigned output_height, unsigned quality);
// The narrowest render width DLSS accepts for this quality and output (0: unknown).
[[nodiscard]] unsigned dlss_min_render_width(unsigned quality, unsigned output_width,
                                             unsigned output_height) noexcept;

}  // namespace galaxy::gx
