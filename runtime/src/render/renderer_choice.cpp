// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/renderer_choice.h"

#include <atomic>
#include <cctype>

namespace galaxy::render {
namespace {

constexpr std::array<const char*, 5> kRendererKeys{"auto", "d3d12", "d3d11", "vulkan", "metal"};

struct RefusalText {
    const char* key;
    const char* reason;
};
constexpr std::array<RefusalText, kRendererRefusalCount> kRefusals{{
    {"none", "runs"},
    {"unknown", "not checked"},
    {"not-installed", "its program is not installed"},
    {"no-driver", "no driver of the API"},
    {"no-device", "the driver creates no device"},
    {"feature-level", "feature level below 11_0"},
    {"binding-tier", "resource binding tier 1, tier 2 needed"},
    {"shader-model", "no shader model 6.0"},
    {"no-d3d11-1", "no Direct3D 11.1 runtime"},
    {"constant-buffer-offsets", "no constant buffer offsets"},
    {"vulkan-version", "Vulkan 1.0 without VK_KHR_maintenance1 or VK_KHR_get_physical_device_properties2"},
    {"no-swapchain", "no VK_KHR_swapchain"},
    {"no-present-queue", "no graphics queue that presents to a window"},
}};

bool same_letters(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0u; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// Index of a choosable renderer in CardRenderers::refusals; kRendererPreference.size() for kAuto and kMetal.
constexpr std::size_t slot(RendererKind kind) noexcept {
    switch (kind) {
    case RendererKind::kD3D12: return 0u;
    case RendererKind::kD3D11: return 1u;
    case RendererKind::kVulkan: return 2u;
    default: return 3u;
    }
}

std::atomic<bool> s_found_no_device{false};

}  // namespace

const char* renderer_key(RendererKind kind) noexcept {
    const auto at = static_cast<std::size_t>(kind);
    return at < kRendererKeys.size() ? kRendererKeys[at] : kRendererKeys[0];
}

std::optional<RendererKind> renderer_from_key(std::string_view key) noexcept {
    for (std::size_t i = 0u; i < kRendererKeys.size(); ++i) {
        if (same_letters(key, kRendererKeys[i])) return static_cast<RendererKind>(i);
    }
    return std::nullopt;
}

RendererKind running_renderer() noexcept {
#if defined(NEBULA_GPU_VULKAN)
    return RendererKind::kVulkan;
#elif defined(NEBULA_GPU_METAL)
    return RendererKind::kMetal;
#elif defined(NEBULA_GPU_D3D11)
    return RendererKind::kD3D11;
#else
    return RendererKind::kD3D12;
#endif
}

const char* refusal_key(RendererRefusal refusal) noexcept {
    const auto at = static_cast<std::size_t>(refusal);
    return at < kRefusals.size() ? kRefusals[at].key : kRefusals[1].key;
}

std::optional<RendererRefusal> refusal_from_key(std::string_view key) noexcept {
    for (std::size_t i = 0u; i < kRefusals.size(); ++i) {
        if (key == kRefusals[i].key) return static_cast<RendererRefusal>(i);
    }
    return std::nullopt;
}

const char* refusal_reason(RendererRefusal refusal) noexcept {
    const auto at = static_cast<std::size_t>(refusal);
    return at < kRefusals.size() ? kRefusals[at].reason : kRefusals[1].reason;
}

// Every texture and sampler in one unbounded table each, indexed by heap slot (create_draw_root_signature; tier 1 gives
// a stage 128 views and 16 samplers), and the programs' DXIL (shader model 6.0).
RendererRefusal d3d12_refusal(const D3D12Facts& facts) noexcept {
    if (!facts.device) return RendererRefusal::kNoDevice;
    if (facts.binding_tier < 2u) return RendererRefusal::kBindingTier;
    if (facts.shader_model < 0x60u) return RendererRefusal::kShaderModel;
    return RendererRefusal::kNone;
}

// Feature level 11_0 (the programs' shader model 5.0), and constant buffers bound at an offset and written in part: the
// draws' rows live in shared buffers.
RendererRefusal d3d11_refusal(const D3D11Facts& facts) noexcept {
    if (!facts.device) return RendererRefusal::kNoDevice;
    if (facts.feature_level < 0xb000u) return RendererRefusal::kFeatureLevel;
    if (!facts.device1) return RendererRefusal::kNoD3D11_1;
    if (!facts.constant_buffer_offsets) return RendererRefusal::kConstantBufferOffsets;
    return RendererRefusal::kNone;
}

// Swapchains; Vulkan 1.1, or 1.0 with what Nebula takes from 1.1 as the extensions it came from (the physical-device
// queries and VK_KHR_maintenance1); a graphics queue that presents.
RendererRefusal vulkan_refusal(const VulkanFacts& facts) noexcept {
    constexpr std::uint32_t kVulkan11 = (1u << 22u) | (1u << 12u);  // VK_MAKE_API_VERSION(0, 1, 1, 0)
    if (!facts.swapchain) return RendererRefusal::kNoSwapchain;
    if (facts.version < kVulkan11 && !(facts.properties2 && facts.maintenance1)) return RendererRefusal::kVulkanVersion;
    if (!facts.presenting_queue) return RendererRefusal::kNoPresentQueue;
    return RendererRefusal::kNone;
}

RendererRefusal CardRenderers::refusal(RendererKind kind) const noexcept {
    const std::size_t at = slot(kind);
    return at < refusals.size() ? refusals[at] : RendererRefusal::kNoDevice;
}

void CardRenderers::set(RendererKind kind, RendererRefusal refusal) noexcept {
    if (const std::size_t at = slot(kind); at < refusals.size()) refusals[at] = refusal;
}

RendererKind preferred_renderer(const CardRenderers& card) noexcept {
    for (const RendererKind kind : kRendererPreference) {
        if (card.runs(kind)) return kind;
    }
    return RendererKind::kAuto;
}

RendererKind resolve_renderer(RendererKind chosen, const CardRenderers& card) noexcept {
    return slot(chosen) < card.refusals.size() && card.runs(chosen) ? chosen : preferred_renderer(card);
}

CardRenderers renderers_for(const std::vector<CardRenderers>& cards, std::string_view identity) {
    if (!identity.empty()) {
        for (const CardRenderers& card : cards) {
            if (card.identity == identity) return card;
        }
    }
    CardRenderers any;
    if (cards.empty()) return any;
    any.refusals = cards.front().refusals;
    for (const CardRenderers& card : cards) {
        for (const RendererKind kind : kRendererPreference) {
            if (card.runs(kind)) any.set(kind, RendererRefusal::kNone);
        }
    }
    return any;
}

RendererKind fallback_renderer(RendererKind failed, const CardRenderers& card) noexcept {
    bool below = false;
    for (const RendererKind kind : kRendererPreference) {
        if (below && card.runs(kind)) return kind;
        below = below || kind == failed;
    }
    return RendererKind::kAuto;
}

std::string format_renderer_check(const std::vector<CardRenderers>& cards) {
    std::string text(kRendererCheckHeader);
    text += '\n';
    for (const CardRenderers& card : cards) {
        text += "card ";
        text += card.identity.empty() ? std::string("-") : card.identity;
        for (const RendererRefusal refusal : card.refusals) {
            text += ' ';
            text += refusal_key(refusal);
        }
        text += ' ';
        for (const char c : card.name) text += c == '\n' || c == '\r' ? ' ' : c;
        text += '\n';
    }
    return text;
}

std::vector<CardRenderers> parse_renderer_check(std::string_view text) {
    std::vector<CardRenderers> cards;
    bool header = false;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0u, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1u);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1u);
        if (!header) {
            header = line == kRendererCheckHeader;
            continue;
        }
        // card <identity> <d3d12> <d3d11> <vulkan> <name>
        std::array<std::string_view, 5> words;
        std::string_view rest = line;
        bool whole = true;
        for (std::string_view& word : words) {
            const std::size_t space = rest.find(' ');
            if (space == std::string_view::npos || space == 0u) {
                whole = false;
                break;
            }
            word = rest.substr(0u, space);
            rest = rest.substr(space + 1u);
        }
        if (!whole || words[0] != "card") continue;
        CardRenderers card;
        card.identity = words[1] == "-" ? std::string() : std::string(words[1]);
        card.name = std::string(rest);
        for (std::size_t i = 0u; i < card.refusals.size() && whole; ++i) {
            const std::optional<RendererRefusal> refusal = refusal_from_key(words[2u + i]);
            whole = refusal.has_value();
            if (whole) card.refusals[i] = *refusal;
        }
        if (whole) cards.push_back(std::move(card));
    }
    return cards;
}

void note_no_device() noexcept {
    s_found_no_device.store(true, std::memory_order_release);
}

bool found_no_device() noexcept {
    return s_found_no_device.load(std::memory_order_acquire);
}

bool adapter_allowed(bool software_adapter, bool software_only, bool start_check) noexcept {
    if (software_only) return software_adapter;
    return !software_adapter || start_check;
}

std::optional<std::size_t> first_allowed_adapter(const std::vector<bool>& software_adapters, bool software_only,
                                                 bool start_check) noexcept {
    for (std::size_t i = 0u; i < software_adapters.size(); ++i) {
        if (adapter_allowed(software_adapters[i], software_only, start_check)) return i;
    }
    return std::nullopt;
}

std::string software_device_missing_line(RendererKind kind) {
    return std::string("[gpu] --software-device: no software device for ") + renderer_key(kind);
}

}  // namespace galaxy::render
