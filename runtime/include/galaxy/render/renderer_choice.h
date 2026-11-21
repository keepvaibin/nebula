// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The renderer a Windows start runs: Direct3D 12 (Nebula.exe), Vulkan (NebulaVk.exe) or Direct3D 11 (NebulaD11.exe).
// What each renderer needs of a graphics card is written once here, as rules over what the card reports; each
// renderer applies them when it chooses its device, and the renderer check (renderer_check.h) applies them to every
// card for the System page's RENDERER row.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::render {

// config.ini's renderer key and --renderer: 0 automatic, then each renderer. Metal is the Apple builds' only renderer
// and never a choice.
enum class RendererKind : std::uint8_t { kAuto = 0u, kD3D12 = 1u, kD3D11 = 2u, kVulkan = 3u, kMetal = 4u };

// The renderers a Windows start can choose, in the order the automatic choice takes them: the newest API first.
inline constexpr std::array<RendererKind, 3> kRendererPreference{RendererKind::kD3D12, RendererKind::kVulkan,
                                                                 RendererKind::kD3D11};

// "auto", "d3d12", "d3d11", "vulkan", "metal".
[[nodiscard]] const char* renderer_key(RendererKind kind) noexcept;
// A key renderer_key writes, in any case; nothing for another text.
[[nodiscard]] std::optional<RendererKind> renderer_from_key(std::string_view key) noexcept;
// The renderer this program runs (its build's).
[[nodiscard]] RendererKind running_renderer() noexcept;

// Why a card does not run a renderer.
enum class RendererRefusal : std::uint8_t {
    kNone = 0u,              // it runs
    kUnknown,                // not checked: the check did not finish
    kNotInstalled,           // the renderer's program is not in Nebula's folder
    kNoDriver,               // no driver of the API on this system (vulkan-1.dll missing)
    kNoDevice,               // the card's driver creates no device of the API
    kFeatureLevel,           // Direct3D: below feature level 11_0
    kBindingTier,            // Direct3D 12: resource binding tier 1 (tier 2 needed)
    kShaderModel,            // Direct3D 12: no shader model 6.0
    kNoD3D11_1,              // Direct3D 11: no Direct3D 11.1 runtime
    kConstantBufferOffsets,  // Direct3D 11: no constant buffer offsets or partial updates
    kVulkanVersion,          // Vulkan 1.0 without VK_KHR_maintenance1 and VK_KHR_get_physical_device_properties2
    kNoSwapchain,            // Vulkan: no VK_KHR_swapchain
    kNoPresentQueue,         // Vulkan: no graphics queue that presents to a window
};
inline constexpr std::size_t kRendererRefusalCount = 13u;

// A refusal's word in the renderer check's output ("none", "no-device"...); its reason for a log ("no shader model 6.0").
[[nodiscard]] const char* refusal_key(RendererRefusal refusal) noexcept;
[[nodiscard]] std::optional<RendererRefusal> refusal_from_key(std::string_view key) noexcept;
[[nodiscard]] const char* refusal_reason(RendererRefusal refusal) noexcept;

// What a card reports, as each renderer reads it.
struct D3D12Facts {
    bool device = false;            // D3D12CreateDevice at feature level 11_0 made one
    unsigned binding_tier = 0u;     // D3D12_FEATURE_DATA_D3D12_OPTIONS::ResourceBindingTier
    unsigned shader_model = 0u;     // D3D12_FEATURE_DATA_SHADER_MODEL::HighestShaderModel (0x60: 6.0)
};
struct D3D11Facts {
    bool device = false;            // D3D11CreateDevice made one at a level of 10_0 to 11_1
    unsigned feature_level = 0u;    // its D3D_FEATURE_LEVEL (0xb000: 11_0)
    bool device1 = false;           // ID3D11Device1 and ID3D11DeviceContext1
    bool constant_buffer_offsets = false;  // ConstantBufferOffsetting and ConstantBufferPartialUpdate
};
struct VulkanFacts {
    bool swapchain = false;                 // VK_KHR_swapchain
    std::uint32_t version = 0u;             // the lower of the instance's and the device's API version
    bool properties2 = false;               // the instance has VK_KHR_get_physical_device_properties2
    bool maintenance1 = false;              // the device has VK_KHR_maintenance1
    bool presenting_queue = false;          // a graphics queue family that presents to the window
};
[[nodiscard]] RendererRefusal d3d12_refusal(const D3D12Facts& facts) noexcept;
[[nodiscard]] RendererRefusal d3d11_refusal(const D3D11Facts& facts) noexcept;
[[nodiscard]] RendererRefusal vulkan_refusal(const VulkanFacts& facts) noexcept;

// A card and what keeps each renderer of kRendererPreference off it.
struct CardRenderers {
    std::string identity;  // gpu_identity
    std::string name;      // UTF-8
    std::array<RendererRefusal, 3> refusals{RendererRefusal::kUnknown, RendererRefusal::kUnknown, RendererRefusal::kUnknown};

    [[nodiscard]] RendererRefusal refusal(RendererKind kind) const noexcept;
    void set(RendererKind kind, RendererRefusal refusal) noexcept;
    [[nodiscard]] bool runs(RendererKind kind) const noexcept { return refusal(kind) == RendererRefusal::kNone; }
};

// The first renderer of kRendererPreference the card runs; kAuto when it runs none.
[[nodiscard]] RendererKind preferred_renderer(const CardRenderers& card) noexcept;
// The renderer a start takes: `chosen` where the card runs it, else preferred_renderer.
[[nodiscard]] RendererKind resolve_renderer(RendererKind chosen, const CardRenderers& card) noexcept;
// The renderers of the automatic GPU choice (`identity` empty) or of the card `identity` names: for the automatic
// choice, a renderer runs where any card runs it (that renderer's own GPU choice takes such a card), else the first
// card's refusal stands. A card that is not among `cards` is an automatic choice too (the renderers take another).
[[nodiscard]] CardRenderers renderers_for(const std::vector<CardRenderers>& cards, std::string_view identity);
// The next renderer after `failed` in kRendererPreference that `card` runs; kAuto when none. A start whose renderer
// found no card it runs starts again on it; the chain only moves down the order, so it ends.
[[nodiscard]] RendererKind fallback_renderer(RendererKind failed, const CardRenderers& card) noexcept;

// The renderer check's output (renderer_check.h): a header line, then a line per card
// "card <identity> <d3d12> <d3d11> <vulkan> <name>". Parsing takes the cards of well-formed lines.
inline constexpr std::string_view kRendererCheckHeader = "nebula-renderers 1";
[[nodiscard]] std::string format_renderer_check(const std::vector<CardRenderers>& cards);
[[nodiscard]] std::vector<CardRenderers> parse_renderer_check(std::string_view text);

// A renderer that found no card it runs says so as it fails its start (render_api.h startup_found_no_device).
void note_no_device() noexcept;
[[nodiscard]] bool found_no_device() noexcept;

// Whether a renderer takes an adapter (launch_args.h software_device and start_check). `software_adapter`: Microsoft's software adapter
// (DXGI_ADAPTER_FLAG_SOFTWARE) or, on Vulkan, a device of the processor type. With --software-device (`software_only`) only a software
// adapter is taken; without it a hardware adapter always, and a software one only in the start check.
[[nodiscard]] bool adapter_allowed(bool software_adapter, bool software_only, bool start_check) noexcept;
// The place of the first adapter adapter_allowed lets through in a renderer's list (one flag each: whether it is a software adapter);
// none for a list with none.
[[nodiscard]] std::optional<std::size_t> first_allowed_adapter(const std::vector<bool>& software_adapters, bool software_only,
                                                               bool start_check) noexcept;
// The line a renderer ends its start with when --software-device found no software device for it.
[[nodiscard]] std::string software_device_missing_line(RendererKind kind);

}  // namespace galaxy::render
