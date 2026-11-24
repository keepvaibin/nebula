// Native sampler precision, descriptors, table identity and heap-page growth.
#include "galaxy/render/render_core.h"
#include <nebula_clip.h>
#include <cmath>
#include "render/core/pipelines.h"
#include "render/core/present_gate.h"
#include "render/core/sampler_renewal.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
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
    const auto key = make_sampler_key(mode, 32);
    RenderConfig cfg{}; cfg.anisotropic_filtering = 1; cfg.texture_lod_bias = 0;
    const auto desc = build_sampler_desc_from_key(key, cfg);
    passed &= expect(desc.MinLOD == mode.min_lod && desc.MaxLOD == mode.max_lod && desc.MipLODBias == mode.lod_bias,
                     "LOD values survive to the native descriptor without byte ranges or fixed-point rounding");
    passed &= expect(desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_WRAP && desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
                     "authored wrapping reaches the descriptor");
    cfg.anisotropic_filtering = 16;
    passed &= expect(build_sampler_desc_from_key(key, cfg).Filter == D3D12_FILTER_ANISOTROPIC,
                     "linear filtering between authored levels uses configured anisotropy");
    cfg.texture_lod_bias = 2u;
    const auto biased = build_sampler_desc_from_key(key, cfg);
    passed &= expect(biased.MaxAnisotropy == 16u && std::fabs(biased.MipLODBias - (key.lod_bias + 1.0f)) < 1e-6f,
                     "configured anisotropy and bias reach the descriptor");
    cfg.anisotropic_filtering = 1u;
    passed &= expect(build_sampler_desc_from_key(key, cfg).MaxAnisotropy == 1u,
                     "disabling anisotropy restores one sample");
    {
        // Levels drawn by hand: the level the game showed, whatever anisotropy and LOD bias the settings ask for.
        const auto drawn = make_sampler_key(mode, 32, 0.5f, false, true);
        RenderConfig sharp{}; sharp.anisotropic_filtering = 16u; sharp.texture_lod_bias = 2u;
        const auto drawn_desc = build_sampler_desc_from_key(drawn, sharp);
        passed &= expect(drawn_desc.Filter == D3D12_FILTER_MIN_MAG_MIP_LINEAR && drawn_desc.MaxAnisotropy == 1u &&
                             std::fabs(drawn_desc.MipLODBias - (mode.lod_bias - 0.5f)) < 1e-5f,
                         "levels drawn by hand take no anisotropy or settings bias, and their replacement's shift");
        // P-048: the frame's part of the bias is the programs', by kind: the key is the same at every frame height,
        // window and upscaler, so they make no new samplers.
        passed &= expect(sampler_frame_bias(mode, false, true) == SamplerFrameBias::lines &&
                             sampler_frame_bias(mode, true, false) == SamplerFrameBias::lines &&
                             sampler_frame_bias(mode, false, false) == SamplerFrameBias::scene,
                         "levels drawn by hand follow the frame's lines, the game's levels the scene's bias");
        auto flat = mode;
        flat.mip = 0;
        passed &= expect(sampler_frame_bias(flat, true, true) == SamplerFrameBias::none,
                         "a sampler without levels takes no frame bias");
        passed &= expect(make_sampler_key(mode, 32).lod_bias == mode.lod_bias,
                         "the game's levels keep the game's bias in the key, the scene's is the programs'");
    }
    auto unmipped = mode;
    unmipped.mip = 0; unmipped.max_lod = 0; unmipped.mag_linear = false;
    const auto unmipped_key = make_sampler_key(unmipped, 6u);
    passed &= expect(unmipped_key.max_lod == 0.0f && ((unmipped_key.filter >> 5u) & 7u) == 4u,
                     "a sampler without mip filtering keeps to the first level, whatever levels the texture has");
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
        // P-056, P-066, P-061: the samplers start again between frames beside the ones in flight, and nothing waits. A
        // fake renderer records frame n in slot n % 2 and releases the slot first, as begin_frame does after its fence;
        // its arrays and samplers are owners that count their destruction, and a GPU wait would count in `waits`.
        struct Fake {
            int waits = 0;
            int arrays_made = 0;
            int arrays_destroyed = 0;
            int in_use = -1;
            std::size_t samplers = 0u;
            std::size_t samplers_destroyed = 0u;
            [[nodiscard]] std::size_t samplers_made() const noexcept { return samplers; }
            void make_samplers(core::RetireRing& ring, unsigned slot, std::uint32_t, bool all) {
                if (in_use >= 0) {
                    ring.retire(slot, [this] { ++arrays_destroyed; });
                }
                if (all) {
                    ring.retire(slot, [this, n = samplers] { samplers_destroyed += n; });
                    samplers = 0u;
                }
                in_use = arrays_made++;
            }
        } fake;
        core::RetireRing ring(2u);
        SamplerSlotCache slots;
        std::uint32_t config = 0u;
        bool ready = false;
        std::uint32_t generation = 0u;
        constexpr unsigned room = 512u;
        constexpr std::size_t crowd = 64u;
        unsigned frame = 0u;
        bool restarted = false;
        const auto next_frame = [&](std::uint32_t settings) {
            const unsigned slot = frame++ % 2u;
            ring.release(slot);
            restarted = false;
            return core::renew_samplers(fake, ring, slot, {&slots, config, ready, generation}, settings, kSamplerHeapSlots,
                                        room, crowd, &restarted);
        };
        passed &= expect(next_frame(5u) && !restarted && fake.in_use == 0 && generation == 1u && config == 5u,
                         "the first frame makes the first array");
        passed &= expect(!next_frame(5u) && fake.arrays_made == 1, "the same settings with room keep the array");
        auto filled = a;
        for (unsigned i = 0; slots.used() + room <= kSamplerHeapSlots; ++i) {
            filled.sampler.lod_bias = 1000.0f + static_cast<float>(i);
            (void)slots.get_or_allocate(filled);
        }
        passed &= expect(next_frame(5u) && restarted && fake.in_use == 1 && slots.used() == kSamplerReservedSlots &&
                             generation == 2u,
                         "a nearly full array is replaced by a new one, and the slots start again");
        passed &= expect(fake.arrays_destroyed == 0, "the old array lives on in the frame that replaced it");
        passed &= expect(!next_frame(5u) && fake.arrays_destroyed == 0, "the other slot's release keeps it");
        passed &= expect(!next_frame(5u) && fake.arrays_destroyed == 1, "its own slot's next release destroys it, once");
        passed &= expect(next_frame(9u) && config == 9u && fake.in_use == 2 && fake.arrays_destroyed == 1,
                         "new settings make a new array beside the old one");
        fake.samplers = crowd;
        passed &= expect(next_frame(9u) && fake.samplers == 0u && fake.samplers_destroyed == 0u && fake.arrays_made == 4,
                         "as many samplers as the device should keep start again beside the old ones");
        (void)next_frame(9u);
        (void)next_frame(9u);
        passed &= expect(fake.samplers_destroyed == crowd && fake.arrays_destroyed == 3 && ring.count() == 0u,
                         "the samplers given up are destroyed after their slot's fence");
        passed &= expect(fake.waits == 0, "nothing waits for the GPU");
    }
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
        // A full persistent region doubles: the earlier descriptors keep their indices and the frame segments move up.
        const auto heap_before = ring.heap();
        const auto p2 = ring.allocate_persistent(1u);
        passed &= expect(p2.index == 32u && ring.persistent_capacity() >= 33u && ring.heap() != heap_before,
                         "a full persistent region grows into a new heap");
        passed &= expect(p0.index == 0u && ring.persistent_cpu(p0.index).ptr != 0u && ring.persistent_cpu(p1.index).ptr != 0u,
                         "the earlier persistent descriptors keep their indices");
        passed &= expect(ring.take_replaced().size() == 1u, "the replaced heap is handed back");
        passed &= expect(ring.take_replaced().empty(), "a heap handed back is not kept (P-029)");
        // A full frame segment doubles into a new heap: this frame's tables so far move with it, at the same place in
        // the segment.
        const auto heap_full = ring.heap();
        const auto t1 = ring.allocate(1u);
        passed &= expect(ring.heap() != heap_full && t1.gpu.ptr == ring.gpu_at(ring.persistent_capacity() + 16u).ptr,
                         "a full frame segment grows into a new heap and keeps the frame's tables");
        passed &= expect(ring.take_replaced().size() == 1u, "the heap the segment outgrew is handed back");
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
        // A draw's pipeline that is not made is never built on the draw's thread (Pipelines::missing; an empty entry's build
        // would be refused and counted in failures): null, counted once, and queued for the workers. No worker runs here.
        // Before play (no picture with a scene) the draw waits for its worker instead, and is let go when the workers stop.
        const core::PipelineEntry missing{};
        const std::uint64_t narrow = 0xFFFF0000u;
        const std::uint64_t wide = (std::uint64_t{1} << 32u) | 0xFFFF0000u;
        {
            core::Pipelines starting;
            std::atomic<bool> returned{false};
            std::thread draw([&] {
                (void)starting.get(narrow, missing, 0, 0);
                returned.store(true);
            });
            for (int i = 0; i < 2000 && starting.misses() == 0u; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const bool waited = starting.misses() == 1u && starting.pending() == 1u && !returned.load();
            starting.shutdown();
            draw.join();
            passed &= expect(waited && returned.load() && starting.failures() == 0u,
                             "before play a draw waits for its missing pipeline's worker, and is let go when the workers stop");
        }
        core::Pipelines pipelines;
        pipelines.scene_drawn();
        pipelines.picture_shown();
        const bool left_out = pipelines.get(narrow, missing, 0, 0) == nullptr && pipelines.get(wide, missing, 0, 0) == nullptr;
        passed &= expect(left_out && pipelines.misses() == 2u && pipelines.pending() == 2u && pipelines.failures() == 0u,
                         "a missing pipeline is left out, counted and queued, never built by the draw");
        passed &= expect(pipelines.misses() == 2u, "pipeline IDs past 32 bits reach separate cache cells");
        const bool missed_again = pipelines.get(narrow, missing, 0, 0) == nullptr && pipelines.get(narrow, missing, 8, 0) == nullptr;
        passed &= expect(missed_again && pipelines.misses() == 3u && pipelines.pending() == 3u && pipelines.failures() == 0u,
                         "a form missed again is neither counted nor queued again; another target kind of the id is its own form");
        // With fixed steps a draw waits for its pipeline's worker; when the workers stop it is let go with none.
        pipelines.set_wait_for_missing(true);
        pipelines.shutdown();
        const bool stopped = pipelines.get(narrow, missing, 0, 0) == nullptr && pipelines.get(narrow, missing, 0, 0) == nullptr;
        passed &= expect(stopped && pipelines.failures() == 0u, "a draw that waits for its pipeline is let go when the workers stop");
        passed &= expect(pipelines.later() == 0u, "no form is left for a load where none was prepared");
        pipelines.shutdown();
    }
    {
        // The present gate (Direct3D 12): a Present waits for a creation in flight and runs alone; in play (no load waiting
        // for its pipelines) each such wait is counted and logged, during a load it is not.
        core::PresentGate gate;
        const auto held_present = [&gate](bool in_play) {
            gate.set_in_play(in_play);
            gate.begin_creation();
            std::atomic<bool> presented{false};
            std::thread present([&] {
                gate.begin_present();
                presented.store(true);
                gate.end_present();
            });
            bool held = false;
            for (int i = 0; i < 2000 && !held; ++i) {
                held = gate.present_held();
                if (!held) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const bool waited = held && !presented.load();
            gate.end_creation();
            present.join();
            return waited && presented.load();
        };
        passed &= expect(held_present(false) && gate.play_waits() == 0u, "a Present held during a load is not play's");
        passed &= expect(held_present(true) && gate.play_waits() == 1u, "a Present held in play is counted");
        gate.begin_present();
        gate.end_present();
        passed &= expect(gate.play_waits() == 1u, "a Present with no creation in flight does not wait");
    }
    {
        // texture_table: one id per list content, from the lookup or this thread's remembered answer alike.
        NebulaFrameTexture la[2]{}, lb[2]{}, wide[9]{};
        la[0].texture = 5u; la[1].texture = 6u; la[1].lod_bias = -0.5f;
        lb[0] = la[0]; lb[1] = la[1]; lb[1].wrap_s = 1u;
        for (std::uint32_t i = 0; i < 9u; ++i) wide[i].texture = 10u + i;
        core::Registries regs, other;
        const std::uint32_t ta = regs.texture_table(la, 2u);
        const std::uint32_t tb = regs.texture_table(lb, 2u);
        passed &= expect(ta != 0u && tb != 0u && ta != tb, "lists differing in one sampler field are two tables");
        passed &= expect(regs.texture_table(la, 2u) == ta && regs.texture_table(lb, 2u) == tb && regs.texture_table(la, 2u) == ta,
                         "a list asked again is the same table");
        passed &= expect(regs.texture_table(la, 1u) != ta && regs.texture_table(la, 1u) == regs.texture_table(la, 1u),
                         "a shorter list is its own table");
        const std::uint32_t tw = regs.texture_table(wide, 9u);
        passed &= expect(regs.texture_table(wide, 9u) == tw, "a list longer than the remembered ones is found by the lookup");
        passed &= expect(regs.texture_table(nullptr, 0u) == regs.texture_table(nullptr, 0u), "the empty list is one table");
        const std::uint32_t sb = other.texture_table(lb, 2u);
        passed &= expect(sb == 1u && other.texture_table(la, 2u) == 2u && regs.texture_table(lb, 2u) == tb,
                         "each registries object keeps its own ids on one thread");
        std::vector<gpu::Table> retired;
        const std::uint32_t freed[] = {6u};
        regs.forget_textures(freed, retired);
        const std::uint32_t anew = regs.texture_table(la, 2u);
        passed &= expect(anew != ta && anew != tb && regs.texture_table(la, 2u) == anew,
                         "a list dropped with its texture is registered anew, not answered from memory");
        const auto ops = regs.take_ops();
        passed &= expect(ops.tables.size() == 6u, "every new table reaches the render thread once");
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
        {
            // A ring without chunks, past its segment and spill: try_allocate answers empty and takes nothing; allocate throws.
            const std::size_t cursor = ring.cursor();
            const UploadRing::Allocation refused = ring.try_allocate(ring.total_bytes() + 1u, 256u);
            passed &= expect(refused.cpu == nullptr && refused.gpu == 0u && refused.resource == nullptr && ring.cursor() == cursor &&
                                 !ring.spilled(),
                             "try_allocate past the segment and the spill is empty and consumes nothing");
            passed &= expect(ring.try_allocate(16u, 3u).cpu == nullptr, "try_allocate with a bad alignment is empty");
            bool threw = false;
            try {
                (void)ring.allocate(ring.total_bytes() + 1u, 256u);
            } catch (const std::runtime_error&) {
                threw = true;
            }
            passed &= expect(threw && ring.cursor() == cursor, "allocate past the segment and the spill still throws");
            const UploadRing::Allocation fits = ring.try_allocate(256u, 256u);
            passed &= expect(fits.cpu != nullptr && ring.cursor() == fits.offset + 256u, "try_allocate places what fits as allocate does");
        }
        ring.reset();
    }
    if (passed) { std::cout << "Sampler tests passed\n"; }
    return passed ? 0 : 1;
}
