// Native sampler precision, descriptors, table identity and heap-page growth.
#include "galaxy/render/render_core.h"
#include <nebula_clip.h>
#include <cmath>
#include "render/core/pipelines.h"
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {
bool expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAILED: " << reason << '\n'; }
    return value;
}
}
int main() {
    using namespace galaxy::render;
    bool passed = true;
    struct PatternFile { NebulaClipHeader header; std::uint32_t indices[3]; } pattern{};
    pattern.header.values_at = offsetof(PatternFile, indices);
    pattern.indices[0] = 0x01000001u; pattern.indices[1] = 0x80000001u; pattern.indices[2] = NEBULA_NONE32;
    NebulaClipTrack track{}; track.count = 3u; track.stride = NEBULA_TRACK_STEP;
    passed &= expect(nebula_clip_pattern_value(&pattern.header, &track, -1.0f) == 0x01000001u &&
                     nebula_clip_pattern_value(&pattern.header, &track, 1.1f) == 0x80000001u &&
                     nebula_clip_pattern_value(&pattern.header, &track, 4.0f) == NEBULA_NONE32,
                     "pattern sampling retains full-width indices and clamps endpoints");
    track.stride = NEBULA_TRACK_STEP_NEAREST;
    passed &= expect(nebula_clip_pattern_value(&pattern.header, &track, 0.6f) == 0x80000001u,
                     "nearest-frame pattern sampling retains full-width indices");
    TextureSampler mode{};
    mode.wrap_s = TexWrap::Repeat; mode.wrap_t = TexWrap::Mirror;
    mode.mag_linear = mode.min_linear = true; mode.mip = 2;
    mode.min_lod = 0.001f; mode.max_lod = 23.75f; mode.lod_bias = -7.12345f;
    const auto key = make_sampler_key(mode, false, 32);
    RenderConfig cfg{}; cfg.anisotropic_filtering = 1; cfg.texture_lod_bias = 0;
    const auto desc = build_sampler_desc_from_key(key, cfg);
    passed &= expect(desc.MinLOD == mode.min_lod && desc.MaxLOD == mode.max_lod && desc.MipLODBias == mode.lod_bias,
                     "LOD values survive to the native descriptor without byte ranges or fixed-point rounding");
    passed &= expect(desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_WRAP && desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
                     "authored wrapping reaches the descriptor");
    mode.mip = 0; mode.max_lod = 0;
    auto generated = make_sampler_key(mode, true, 32);
    passed &= expect(generated.max_lod == 31.0f, "generated chains retain all 32 levels");
    cfg.anisotropic_filtering = 16;
    passed &= expect(build_sampler_desc_from_key(generated, cfg).Filter == D3D12_FILTER_ANISOTROPIC,
                     "linear generated levels use configured anisotropy");
    cfg.texture_lod_bias = 2u;
    const auto biased = build_sampler_desc_from_key(generated, cfg);
    passed &= expect(biased.MaxAnisotropy == 16u && std::fabs(biased.MipLODBias - (generated.lod_bias + 1.0f)) < 1e-6f,
                     "configured anisotropy and bias reach the descriptor");
    cfg.anisotropic_filtering = 1u;
    passed &= expect(build_sampler_desc_from_key(generated, cfg).MaxAnisotropy == 1u,
                     "disabling anisotropy restores one sample");
    auto nearest = mode;
    nearest.mag_linear = false;
    const auto nearest_key = make_sampler_key(nearest, true, 6u);
    passed &= expect(nearest_key.max_lod == 0.0f && ((nearest_key.filter >> 5u) & 7u) == 4u,
                     "nearest magnification keeps authored mip selection");
    const auto a = make_sampler_slot_key(key, cfg);
    auto biased_key = key;
    biased_key.lod_bias += 0.0001f;
    const auto b = make_sampler_slot_key(biased_key, cfg);
    auto maximum_cfg = cfg;
    maximum_cfg.anisotropic_filtering = 16u;
    maximum_cfg.texture_lod_bias = 8u;
    auto excessive_cfg = maximum_cfg;
    excessive_cfg.anisotropic_filtering = 100u;
    excessive_cfg.texture_lod_bias = 100u;
    passed &= expect(make_sampler_slot_key(key, maximum_cfg) == make_sampler_slot_key(key, excessive_cfg),
                     "cache equality follows device-clamped settings");
    passed &= expect(!(a == b) && !(make_sampler_slot_key(biased_key, maximum_cfg) == b),
                     "cache identity includes the sampler and live configuration");
    struct SameBucket { std::size_t operator()(const SamplerSlotKey&) const { return 0; } };
    std::unordered_map<SamplerSlotKey, unsigned, SameBucket> collisions;
    collisions.emplace(a, 1); collisions.emplace(b, 2);
    passed &= expect(collisions.size() == 2 && collisions.at(b) == 2,
                     "sub-fixed-point LOD changes remain distinct under hash collisions");
    SamplerSlotCache cache;
    const auto first = cache.get_or_allocate(a);
    const auto again = cache.get_or_allocate(a);
    passed &= expect(first.created && !again.created && first.index == again.index, "slots are reused by complete identity");
    passed &= expect(first.index == kSamplerReservedSlots, "the first slot follows the reserved ones");
    auto changed = a;
    unsigned previous = first.index;
    for (unsigned i = 0; i < 3000; ++i) {
        changed.sampler.lod_bias = static_cast<float>(i);
        const auto allocation = cache.get_or_allocate(changed);
        passed &= expect(allocation.index >= kSamplerReservedSlots, "no sampler takes the default slot");
        passed &= expect(allocation.created && allocation.index == previous + 1u, "each new sampler takes the next slot");
        previous = allocation.index;
    }
    passed &= expect(cache.used() == previous + 1u && cache.used() > kSamplerHeapSlots,
                     "the cache counts past the array: the renderer starts again before it is full");
    cache.reset();
    passed &= expect(cache.get_or_allocate(a).index == kSamplerReservedSlots, "reset reclaims slot space");
    {
        // A bound table's key: its length and settings count.
        const SamplerKey two[2] = {key, biased_key};
        const auto table = make_sampler_table_key(two, 2u, cfg);
        passed &= expect(!(table == make_sampler_table_key(two, 1u, cfg)) && !(table == make_sampler_table_key(two, 2u, maximum_cfg)) &&
                             table == make_sampler_table_key(two, 2u, cfg),
                         "a bound table's identity includes its length and live configuration");
    }
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    passed &= expect(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))), "descriptor test device");
    if (device) {
        DescriptorRing ring;
        const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        passed &= expect(ring.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16u, 2u, 32u, 8u), "small descriptor heap");
        const auto p0 = ring.allocate_persistent(16u);
        const auto p1 = ring.allocate_persistent(16u);
        passed &= expect(p0.index == 0u && p1.index == 16u, "persistent descriptors are named by their index in the persistent region");
        ring.begin_frame(0);
        const auto t0 = ring.allocate(16u);
        passed &= expect(t0.gpu.ptr == ring.gpu_at(32u).ptr, "a frame segment follows the persistent region");
        bool refused = false;
        try {
            static_cast<void>(ring.allocate_persistent(1u));
        } catch (const std::runtime_error&) {
            refused = true;
        }
        passed &= expect(refused, "a full persistent region refuses instead of opening another heap");
        refused = false;
        try {
            static_cast<void>(ring.allocate(1u));
        } catch (const std::runtime_error&) {
            refused = true;
        }
        passed &= expect(refused, "a full frame segment refuses instead of opening another heap");
        ring.begin_frame(1);
        const auto t2 = ring.allocate(16u);
        passed &= expect(t2.gpu.ptr != t0.gpu.ptr, "another frame slot never overwrites in-flight descriptors");
        ring.flush();
        const auto p1_before = ring.gpu_at(p1.index).ptr - ring.texture_gpu_start().ptr;
        ID3D12DescriptorHeap* const first_heap = ring.heap();
        auto replaced = ring.resize_textures(24u);
        passed &= expect(replaced.Get() == first_heap && ring.heap() != first_heap && ring.texture_slots() == 24u,
                         "a larger texture region replaces the heap and returns the old one");
        passed &= expect(ring.gpu_at(p1.index).ptr - ring.texture_gpu_start().ptr == p1_before + 16u * increment,
                         "a persistent index keeps its place in the ring part when the heap is replaced");
        passed &= expect(!ring.resize_textures(24u), "a region that is not larger changes nothing");
    }
    {
        core::Pipelines pipelines;
        const core::PipelineEntry missing{};
        (void)pipelines.get(0xFFFF0000u, missing, 0, 0);
        (void)pipelines.get((std::uint64_t{1} << 32u) | 0xFFFF0000u, missing, 0, 0);
        passed &= expect(pipelines.failures() == 2, "full-width pipeline IDs and inspector IDs reach separate cache cells");
        pipelines.shutdown();
    }
    if (device) {
        TextureBackend backend;
        passed &= expect(backend.initialize(device.Get(), 65536u), "texture view page setup");
        backend.ensure_view(70000u);
        passed &= expect(backend.srv_cpu(0).ptr != backend.srv_cpu(70000u).ptr, "texture views grow beyond 65536 without overwriting a live view");
        backend.shutdown();
        DeviceGate gate;
        gate.set_device(device.Get());
        core::Registries registry;
        registry.initialize(device.Get(), gate);
        constexpr std::uint32_t rows = 70000;
        std::vector<std::array<float, 4>> large_rows(rows * 2u);
        large_rows.back()[3] = 42.0f;
        const auto slot = registry.material_register(reinterpret_cast<const float (*)[4]>(large_rows.data()), rows, rows);
        const auto operations = registry.take_ops();
        passed &= expect(slot != NEBULA_NONE32 && operations.slots.size() == 1u && registry.memory_bytes() >= std::uint64_t{rows} * 32u,
                         "one material can exceed the initial page without overwriting its allocation");
        if (operations.slots.size() == 1u) {
            const auto& allocation = operations.slots.front();
            const std::uint64_t bytes = std::uint64_t{rows} * 16u;
            passed &= expect(allocation.vertex_rows != 0u && allocation.pixel_rows != 0u &&
                (allocation.vertex_rows + bytes <= allocation.pixel_rows || allocation.pixel_rows + bytes <= allocation.vertex_rows),
                "oversized vertex and pixel constant ranges do not overlap");
        }
        registry.shutdown();
        UploadRing ring;
        constexpr std::size_t initial = 1u << 20u, requested = 2u << 20u;
        passed &= expect(ring.initialize(device.Get(), "preflight-test", UploadRing::budget(initial, 2u), 2u, initial), "upload ring setup");
        ring.require_frame_bytes(requested);
        passed &= expect(ring.wants_growth(), "an oversized first frame requests growth before allocation");
        const auto grown = ring.grown_capacity(2u);
        passed &= expect(grown > initial && ring.initialize(device.Get(), "preflight-test", UploadRing::budget(grown, 2u), 2u, grown), "oversized first frame receives a larger ring");
        ring.begin_frame(0u);
        passed &= expect(ring.allocate(requested, 256u).cpu != nullptr && !ring.spilled(), "the first oversized frame fits its segment without a spill failure");
        ring.reset();
    }
    if (passed) { std::cout << "Sampler tests passed\n"; }
    return passed ? 0 : 1;
}
