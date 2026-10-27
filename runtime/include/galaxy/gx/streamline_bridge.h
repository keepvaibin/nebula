#pragma once

// NVIDIA Streamline (DLSS super resolution), loaded at run time from a vendor
// folder the user installs: Nebula ships none of NVIDIA's binaries. The
// interposer must carry Streamline's signature; Streamline runs with manual
// hooking (only the swap chain's presentation and the command queue pass
// through its proxies) and without over-the-air updates. Render thread only.
// Built without the SDK headers (GALAXY_STREAMLINE_SDK), every call reports
// Streamline as absent.

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
// driver); the reason is logged.
bool startup(const std::wstring& directory) noexcept;
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

}  // namespace streamline

// DLSS super resolution for one render/output size. quality: 0 DLAA, 1
// quality, 2 balanced, 3 performance, 4 ultra performance. Null when DLSS is
// not available.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_dlss_upscaler(
    unsigned render_width, unsigned render_height,
    unsigned output_width, unsigned output_height, unsigned quality);

}  // namespace galaxy::gx
