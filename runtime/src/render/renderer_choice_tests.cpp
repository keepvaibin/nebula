// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/renderer_choice.h"

#include <iostream>
#include <stdexcept>

namespace {
using galaxy::render::CardRenderers;
using galaxy::render::RendererKind;
using galaxy::render::RendererRefusal;

void check(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}

CardRenderers card(const char* identity, RendererRefusal d3d12, RendererRefusal d3d11, RendererRefusal vulkan) {
    CardRenderers c;
    c.identity = identity;
    c.name = std::string("Card ") + identity;
    c.set(RendererKind::kD3D12, d3d12);
    c.set(RendererKind::kD3D11, d3d11);
    c.set(RendererKind::kVulkan, vulkan);
    return c;
}

void test_keys() {
    using galaxy::render::renderer_from_key;
    using galaxy::render::renderer_key;
    for (const RendererKind kind : {RendererKind::kAuto, RendererKind::kD3D12, RendererKind::kD3D11, RendererKind::kVulkan,
                                    RendererKind::kMetal}) {
        check(renderer_from_key(renderer_key(kind)) == kind, "every key reads back");
    }
    check(renderer_from_key("Vulkan") == RendererKind::kVulkan, "keys in any case");
    check(!renderer_from_key("opengl") && !renderer_from_key(""), "other text is no renderer");
    for (std::size_t i = 0u; i < galaxy::render::kRendererRefusalCount; ++i) {
        const auto refusal = static_cast<RendererRefusal>(i);
        check(galaxy::render::refusal_from_key(galaxy::render::refusal_key(refusal)) == refusal, "every refusal reads back");
    }
}

void test_rules() {
    using namespace galaxy::render;
    check(d3d12_refusal({}) == RendererRefusal::kNoDevice, "d3d12: no device");
    check(d3d12_refusal({true, 1u, 0x66u}) == RendererRefusal::kBindingTier, "d3d12: tier 1");
    check(d3d12_refusal({true, 3u, 0x51u}) == RendererRefusal::kShaderModel, "d3d12: shader model 5.1");
    check(d3d12_refusal({true, 2u, 0x60u}) == RendererRefusal::kNone, "d3d12: tier 2 and 6.0 run");

    check(d3d11_refusal({}) == RendererRefusal::kNoDevice, "d3d11: no device");
    check(d3d11_refusal({true, 0xa100u, true, true}) == RendererRefusal::kFeatureLevel, "d3d11: 10_1");
    check(d3d11_refusal({true, 0xb000u, false, true}) == RendererRefusal::kNoD3D11_1, "d3d11: no 11.1 runtime");
    check(d3d11_refusal({true, 0xb100u, true, false}) == RendererRefusal::kConstantBufferOffsets, "d3d11: no offsets");
    check(d3d11_refusal({true, 0xb000u, true, true}) == RendererRefusal::kNone, "d3d11: 11_0 runs");

    constexpr std::uint32_t v10 = 1u << 22u;
    constexpr std::uint32_t v11 = (1u << 22u) | (1u << 12u);
    check(vulkan_refusal({false, v11, true, true, true}) == RendererRefusal::kNoSwapchain, "vulkan: swapchain first");
    check(vulkan_refusal({true, v10, true, false, true}) == RendererRefusal::kVulkanVersion, "vulkan: 1.0 without maintenance1");
    check(vulkan_refusal({true, v10, false, true, true}) == RendererRefusal::kVulkanVersion, "vulkan: 1.0 without properties2");
    check(vulkan_refusal({true, v10, true, true, true}) == RendererRefusal::kNone, "vulkan: 1.0 with both extensions");
    check(vulkan_refusal({true, v11, false, false, false}) == RendererRefusal::kNoPresentQueue, "vulkan: no presenting queue");
    check(vulkan_refusal({true, v11, false, false, true}) == RendererRefusal::kNone, "vulkan: 1.1 runs");
}

void test_choice() {
    using namespace galaxy::render;
    const CardRenderers modern = card("10de:2b85:00000000", RendererRefusal::kNone, RendererRefusal::kNone, RendererRefusal::kNone);
    const CardRenderers kepler = card("10de:1180:00000000", RendererRefusal::kShaderModel, RendererRefusal::kNone, RendererRefusal::kNone);
    const CardRenderers old = card("8086:0166:00000000", RendererRefusal::kNoDevice, RendererRefusal::kNone, RendererRefusal::kNoDevice);
    const CardRenderers none = card("1234:0001:00000000", RendererRefusal::kNoDevice, RendererRefusal::kFeatureLevel,
                                    RendererRefusal::kNoDriver);

    check(preferred_renderer(modern) == RendererKind::kD3D12, "Direct3D 12 first");
    check(preferred_renderer(kepler) == RendererKind::kVulkan, "then Vulkan");
    check(preferred_renderer(old) == RendererKind::kD3D11, "then Direct3D 11");
    check(preferred_renderer(none) == RendererKind::kAuto, "a card that runs none");

    check(resolve_renderer(RendererKind::kD3D11, modern) == RendererKind::kD3D11, "a choice the card runs stands");
    check(resolve_renderer(RendererKind::kD3D12, kepler) == RendererKind::kVulkan, "a choice it does not run gives way");
    check(resolve_renderer(RendererKind::kAuto, old) == RendererKind::kD3D11, "auto is the preferred one");

    check(fallback_renderer(RendererKind::kD3D12, modern) == RendererKind::kVulkan, "Direct3D 12 falls to Vulkan");
    check(fallback_renderer(RendererKind::kD3D12, old) == RendererKind::kD3D11, "skipping what the card does not run");
    check(fallback_renderer(RendererKind::kVulkan, modern) == RendererKind::kD3D11, "Vulkan falls to Direct3D 11");
    check(fallback_renderer(RendererKind::kD3D11, modern) == RendererKind::kAuto, "Direct3D 11 is the end");
    check(fallback_renderer(RendererKind::kVulkan, kepler) == RendererKind::kD3D11, "never back up the order");

    const std::vector<CardRenderers> laptop{kepler, old};
    const CardRenderers automatic = renderers_for(laptop, "");
    check(!automatic.runs(RendererKind::kD3D12) && automatic.refusal(RendererKind::kD3D12) == RendererRefusal::kShaderModel,
          "automatic: the first card's reason where none runs it");
    check(automatic.runs(RendererKind::kVulkan) && automatic.runs(RendererKind::kD3D11), "automatic: any card that runs it");
    check(renderers_for(laptop, old.identity).identity == old.identity, "a chosen card's own");
    check(!renderers_for(laptop, old.identity).runs(RendererKind::kVulkan), "a chosen card's own refusals");
    check(renderers_for(laptop, "ffff:ffff:00000000").runs(RendererKind::kVulkan), "a card not present is automatic");
    check(!renderers_for({}, "").runs(RendererKind::kD3D12) &&
              renderers_for({}, "").refusal(RendererKind::kD3D12) == RendererRefusal::kUnknown,
          "no cards: nothing known");
}

void test_check_text() {
    using namespace galaxy::render;
    CardRenderers named = card("10de:2b85:89ef1043", RendererRefusal::kNone, RendererRefusal::kNoD3D11_1, RendererRefusal::kNoPresentQueue);
    named.name = "NVIDIA GeForce RTX 5090 (two  spaces)";
    CardRenderers unnamed = card("", RendererRefusal::kUnknown, RendererRefusal::kNotInstalled, RendererRefusal::kVulkanVersion);
    unnamed.name.clear();
    const std::vector<CardRenderers> parsed = parse_renderer_check(format_renderer_check({named, unnamed}));
    check(parsed.size() == 2u, "both cards read back");
    check(parsed[0].identity == named.identity && parsed[0].name == named.name && parsed[0].refusals == named.refusals,
          "a card reads back whole");
    check(parsed[1].identity.empty() && parsed[1].name.empty() && parsed[1].refusals == unnamed.refusals,
          "a card without identity or name");
    check(parse_renderer_check("card 10de:2b85:89ef1043 none none none X\n").empty(), "nothing before the header");
    check(parse_renderer_check(std::string(kRendererCheckHeader) + "\r\ncard a none none none X\r\n").size() == 1u,
          "CRLF lines");
    check(parse_renderer_check(std::string(kRendererCheckHeader) +
                               "\ncard a none none\ncard b none none maybe X\nchip c none none none X\ncard d none none none X")
                  .size() == 1u,
          "malformed lines left out");
}
}  // namespace

int main() {
    try {
        test_keys();
        test_rules();
        test_choice();
        test_check_text();
    } catch (const std::exception& failure) {
        std::cerr << "renderer choice test failed: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "renderer choice tests passed\n";
    return 0;
}
