#include "galaxy/gx/gx_backend.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool expect_near(float actual, float expected, const char* message) {
    return expect(std::fabs(actual - expected) <= 0.0001f, message);
}

std::uint32_t field(std::uint32_t value, unsigned shift, std::uint32_t mask) {
    return (value >> shift) & mask;
}

}  // namespace

int main() {
    bool passed = true;

    galaxy::gx::TexMode authored{};
    authored.wrap_s = galaxy::gx::TexWrap::Repeat;
    authored.wrap_t = galaxy::gx::TexWrap::Mirror;
    authored.mag_filter = galaxy::gx::TexMagFilter::Linear;
    authored.min_filter = galaxy::gx::TexMinFilter::LinearMipNear;
    authored.lod_bias_x32 = -32;
    authored.min_lod_x16 = 2;
    authored.max_lod_x16 = 96;

    const std::uint32_t authored_key =
        galaxy::gx::pack_sampler_key(authored, false, 1);
    passed &= expect(
        field(authored_key, 0, 0x3u) ==
            static_cast<std::uint32_t>(galaxy::gx::TexWrap::Repeat),
        "sampler key packs wrap_s");
    passed &= expect(
        field(authored_key, 2, 0x3u) ==
            static_cast<std::uint32_t>(galaxy::gx::TexWrap::Mirror),
        "sampler key packs wrap_t");
    passed &= expect(field(authored_key, 4, 0x1u) == 1u,
        "sampler key packs mag filter");
    passed &= expect(
        field(authored_key, 5, 0x7u) ==
            static_cast<std::uint32_t>(
                galaxy::gx::TexMinFilter::LinearMipNear),
        "sampler key preserves authored min filter");
    passed &= expect(field(authored_key, 8, 0xFFu) == 0xE0u,
        "sampler key sign-packs negative LOD bias");
    passed &= expect(field(authored_key, 16, 0xFFu) == 2u,
        "sampler key packs min LOD");
    passed &= expect(field(authored_key, 24, 0xFFu) == 96u,
        "sampler key packs max LOD");

    galaxy::gx::TexMode generated{};
    generated.wrap_s = galaxy::gx::TexWrap::Clamp;
    generated.wrap_t = galaxy::gx::TexWrap::Repeat;
    generated.mag_filter = galaxy::gx::TexMagFilter::Linear;
    generated.min_filter = galaxy::gx::TexMinFilter::Linear;
    generated.lod_bias_x32 = 12;
    generated.min_lod_x16 = 0;
    generated.max_lod_x16 = 0;

    const std::uint32_t generated_key =
        galaxy::gx::pack_sampler_key(generated, true, 6);
    passed &= expect(
        field(generated_key, 5, 0x7u) ==
            static_cast<std::uint32_t>(
                galaxy::gx::TexMinFilter::LinearMipLinear),
        "generated mip chains upgrade linear minification to trilinear");
    passed &= expect(field(generated_key, 24, 0xFFu) == 80u,
        "generated mip chains expand max LOD to the generated level count");

    galaxy::gx::RenderConfig aniso_config{};
    aniso_config.anisotropic_filtering = 16;
    aniso_config.texture_lod_bias = 2;
    const D3D12_SAMPLER_DESC aniso_desc =
        galaxy::gx::build_sampler_desc_from_key(
            generated_key,
            aniso_config);
    passed &= expect(
        aniso_desc.Filter == D3D12_FILTER_ANISOTROPIC,
        "linear mipmapped sampler uses D3D12 anisotropic filtering");
    passed &= expect(
        aniso_desc.MaxAnisotropy == 16u,
        "anisotropic sampler descriptor uses configured anisotropy");
    passed &= expect(
        aniso_desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_CLAMP &&
            aniso_desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        "sampler descriptor maps GX wrap modes to D3D12 address modes");
    passed &= expect_near(
        aniso_desc.MipLODBias,
        1.375f,
        "sampler descriptor combines GX LOD bias with native LOD bias");
    passed &= expect_near(
        aniso_desc.MinLOD,
        0.0f,
        "sampler descriptor maps GX minimum LOD");
    passed &= expect_near(
        aniso_desc.MaxLOD,
        5.0f,
        "sampler descriptor maps generated mip maximum LOD");

    galaxy::gx::RenderConfig no_aniso_config = aniso_config;
    no_aniso_config.anisotropic_filtering = 1;
    const D3D12_SAMPLER_DESC no_aniso_desc =
        galaxy::gx::build_sampler_desc_from_key(
            generated_key,
            no_aniso_config);
    passed &= expect(
        no_aniso_desc.Filter != D3D12_FILTER_ANISOTROPIC,
        "anisotropic filtering setting of 1 disables D3D12 anisotropy");
    passed &= expect(
        no_aniso_desc.MaxAnisotropy == 1u,
        "non-anisotropic sampler descriptor keeps MaxAnisotropy at 1");

    const std::uint32_t clamped_generated_key =
        galaxy::gx::pack_sampler_key(generated, true, 32);
    passed &= expect(field(clamped_generated_key, 24, 0xFFu) == 0xFFu,
        "generated mip max LOD clamps to the packed byte range");

    galaxy::gx::TexMode nearest = generated;
    nearest.mag_filter = galaxy::gx::TexMagFilter::Near;
    const std::uint32_t nearest_key =
        galaxy::gx::pack_sampler_key(nearest, true, 6);
    passed &= expect(
        field(nearest_key, 5, 0x7u) ==
            static_cast<std::uint32_t>(galaxy::gx::TexMinFilter::Linear),
        "generated mip rewrite requires linear magnification");
    passed &= expect(field(nearest_key, 24, 0xFFu) == 0u,
        "non-rewritten sampler preserves TexMode max LOD");

    // One bucket for every key: equality must compare all eight slots and the host
    // configuration, not just a fingerprint.
    struct SameBucket {
        std::size_t operator()(const galaxy::gx::SamplerTableKey&) const { return 0u; }
    };
    std::uint32_t sampler_keys[8]{};
    auto table_a = galaxy::gx::make_sampler_table_key(sampler_keys, no_aniso_config);
    sampler_keys[7] = authored_key;
    const auto table_b = galaxy::gx::make_sampler_table_key(sampler_keys, no_aniso_config);
    const auto table_c = galaxy::gx::make_sampler_table_key(sampler_keys, aniso_config);
    auto clamped_config = aniso_config;
    clamped_config.anisotropic_filtering = 100u;
    clamped_config.texture_lod_bias = 100u;
    auto maximum_config = aniso_config;
    maximum_config.texture_lod_bias = 8u;
    passed &= expect(galaxy::gx::make_sampler_table_key(sampler_keys, clamped_config) ==
        galaxy::gx::make_sampler_table_key(sampler_keys, maximum_config),
        "sampler settings identity uses descriptor-clamped anisotropy and bias");
    auto changed_bias_config = aniso_config;
    changed_bias_config.texture_lod_bias = 3u;
    passed &= expect(!(galaxy::gx::make_sampler_table_key(sampler_keys, changed_bias_config) == table_c),
        "sampler identity changes when live descriptor bias changes");
    std::unordered_map<galaxy::gx::SamplerTableKey, unsigned, SameBucket> collision_map;
    collision_map.emplace(table_a, 1u);
    collision_map.emplace(table_b, 2u);
    collision_map.emplace(table_c, 3u);
    passed &= expect(collision_map.size() == 3u && collision_map.at(table_b) == 2u,
        "sampler full equality separates colliding keys and configuration");

    // A cache hit must refer to descriptors written in that physical heap,
    // including after the active slot wraps around. The renderer owns one
    // cache per heap; a shared cache would suppress the second heap's writes.
    std::array<galaxy::gx::SamplerTableCache, galaxy::gx::kFramesInFlight> heap_caches;
    std::array<std::unordered_map<unsigned, galaxy::gx::SamplerTableKey>,
        galaxy::gx::kFramesInFlight> written_tables;
    for (unsigned frame = 0u; frame < 8u; ++frame) {
        const unsigned slot = frame % galaxy::gx::kFramesInFlight;
        heap_caches[slot].reset(table_c.config);
        const auto allocation = heap_caches[slot].get_or_allocate(table_c);
        passed &= expect(allocation.created == (frame < galaxy::gx::kFramesInFlight),
            "each physical heap writes its cold table, then retains warm tables");
        if (allocation.created) written_tables[slot][allocation.index] = table_c;
        const auto resident = written_tables[slot].find(allocation.index);
        passed &= expect(resident != written_tables[slot].end() && resident->second == table_c,
            "every returned sampler index names the expected table in its heap");
    }
    // Only the fenced active heap may invalidate its contents for changed
    // settings. Another slot's descriptors/cache remain valid until its turn.
    heap_caches[0].reset(table_b.config);
    written_tables[0].clear();
    const auto changed = heap_caches[0].get_or_allocate(table_b);
    passed &= expect(changed.created && changed.index == 8u,
        "changed configuration rebuilds the active heap from the first dynamic slot");
    const auto untouched = heap_caches[1].get_or_allocate(table_c);
    passed &= expect(!untouched.created && written_tables[1].at(untouched.index) == table_c,
        "configuration changes in one heap do not invalidate another heap");
    heap_caches[0].reset(table_b.config);
    passed &= expect(!heap_caches[0].get_or_allocate(table_b).created,
        "unchanged configuration retains the rebuilt heap's table");
    heap_caches[0].reset();
    passed &= expect(heap_caches[0].get_or_allocate(table_b).created,
        "teardown clears retained heap identity even for unchanged settings");

    // Production reset(config) keeps descriptors, but historical tables must
    // not consume capacity needed by a legal new frame's working set.
    galaxy::gx::SamplerTableCache lifetime_cache;
    bool lifetime_fit = true;
    for (unsigned frame = 0u; frame < 8u; ++frame) {
        lifetime_cache.reset(table_a.config);
        try {
            for (unsigned i = 0u; i < 128u; ++i) {
                auto key = table_a;
                key.samplers[0] = frame * 128u + i;
                (void)lifetime_cache.get_or_allocate(key);
            }
        } catch (const std::runtime_error&) { lifetime_fit = false; break; }
    }
    passed &= expect(lifetime_fit,
        "disjoint legal working sets fit across same-config fenced frames");

    galaxy::gx::SamplerTableCache pinned_cache;
    pinned_cache.reset(table_a.config);
    for (unsigned i = 0u; i < 255u; ++i) {
        auto key = table_a;
        key.samplers[0] = i;
        (void)pinned_cache.get_or_allocate(key);
    }
    pinned_cache.reset(table_a.config);
    const auto pinned = pinned_cache.get_or_allocate(table_a);
    passed &= expect(!pinned.created, "a current-frame hit pins its retained descriptor range");
    std::array<unsigned, 254> new_indices{};
    for (unsigned i = 0u; i < new_indices.size(); ++i) {
        auto key = table_a;
        key.samplers[0] = 1000u + i;
        const auto allocation = pinned_cache.get_or_allocate(key);
        new_indices[i] = allocation.index;
        passed &= expect(allocation.created && allocation.index != pinned.index,
            "historical replacement rewrites a range without evicting a current-frame pin");
    }
    auto excess = table_a;
    excess.samplers[0] = 9999u;
    bool pinned_exhaustion = false;
    try { (void)pinned_cache.get_or_allocate(excess); }
    catch (const std::runtime_error&) { pinned_exhaustion = true; }
    passed &= expect(pinned_exhaustion,
        "256th current-frame table fails instead of overwriting recorded descriptors");
    const auto after_failure = pinned_cache.get_or_allocate(table_a);
    passed &= expect(!after_failure.created && after_failure.index == pinned.index,
        "capacity failure preserves the retained current-frame table");
    for (unsigned i = 0u; i < new_indices.size(); ++i) {
        auto key = table_a;
        key.samplers[0] = 1000u + i;
        const auto allocation = pinned_cache.get_or_allocate(key);
        passed &= expect(!allocation.created && allocation.index == new_indices[i],
            "all published current-frame indices remain unchanged after replacement and failure");
    }
    pinned_cache.reset(table_a.config);
    passed &= expect(!pinned_cache.get_or_allocate(table_a).created,
        "an unchanged table survives the next fenced frame");
    passed &= expect(pinned_cache.get_or_allocate(excess).created,
        "the next fenced frame can replace a previously pinned historical table");

    galaxy::gx::SamplerTableCache table_cache;
    for (unsigned frame = 0u; frame < 4u; ++frame) {
        // Exercise the production same-config fenced reset, not teardown.
        table_cache.reset(table_a.config);
        for (unsigned i = 0u; i < 255u; ++i) {
            auto key = table_a;
            key.samplers[0] = frame * 255u + i;
            const auto allocation = table_cache.get_or_allocate(key);
            passed &= expect(allocation.created && allocation.index == 8u + i * 8u,
                "frame sampler allocator fills all legal table slots");
            const auto duplicate = table_cache.get_or_allocate(key);
            passed &= expect(!duplicate.created && duplicate.index == allocation.index,
                "duplicate table reuses its unchanged frame slot");
        }
        auto extra = table_a;
        extra.samplers[0] = 10000u;
        bool rejected = false;
        try { (void)table_cache.get_or_allocate(extra); }
        catch (const std::runtime_error&) { rejected = true; }
        passed &= expect(rejected, "256th distinct table in one frame is explicit exhaustion");
    }

    passed &= expect(galaxy::gx::texture_guest_byte_size(
        galaxy::gx::TexFormat::RGB565, 8u, 8u, 3u) == 192u,
        "full authored mip footprint includes the two minimum blocks");
    passed &= expect(galaxy::gx::texture_guest_byte_size(
        galaxy::gx::TexFormat::I4, 9u, 9u) == 128u,
        "odd I4 extent rounds both dimensions to complete 8x8 blocks");
    passed &= expect(galaxy::gx::texture_guest_byte_size(
        galaxy::gx::TexFormat::RGBA8, 8u, 8u) == 256u,
        "generated mip guest source remains base-only");
    for (const auto extent : {0u, 1025u, UINT32_MAX}) {
        bool rejected = false;
        try { (void)galaxy::gx::texture_guest_byte_size(
            galaxy::gx::TexFormat::RGBA8, extent, 8u); }
        catch (const std::runtime_error&) { rejected = true; }
        passed &= expect(rejected, "invalid public dimensions rejected before size arithmetic");
    }
    bool bad_levels = false;
    try { (void)galaxy::gx::texture_guest_byte_size(
        galaxy::gx::TexFormat::RGBA8, 8u, 8u, 33u); }
    catch (const std::runtime_error&) { bad_levels = true; }
    passed &= expect(bad_levels, "invalid mip count rejected before shifts");
    galaxy::gx::EfbCopyParams copy{};
    copy.src_width = copy.src_height = 8u;
    copy.target_format = 6u; // Two 64-byte RGBA blocks per block row.
    copy.dest_stride = 512u;
    passed &= expect(galaxy::gx::efb_copy_guest_byte_size(copy) == 640u,
        "strided copy span reaches last block row without trailing padding");
    copy.dest_stride = 0u;
    passed &= expect(galaxy::gx::efb_copy_guest_byte_size(copy) == 128u,
        "zero stride overlapping rows have one-row bounding footprint");
    copy.dest_stride = UINT32_MAX;
    bool overflow = false;
    try { (void)galaxy::gx::efb_copy_guest_byte_size(copy); }
    catch (const std::runtime_error&) { overflow = true; }
    passed &= expect(overflow, "public copy stride overflow is explicit");

    if (!passed) {
        return 1;
    }

    std::cout << "Sampler key tests passed\n";
    return 0;
}
