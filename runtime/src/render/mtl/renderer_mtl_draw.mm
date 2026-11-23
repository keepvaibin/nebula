// SPDX-License-Identifier: GPL-3.0-only
// RendererMtl: the draws' textures and samplers, parallel recording, test captures, and the temporal
// scene. As vk/renderer_vk_frame.cpp's second half.
#include "renderer_mtl_internal.h"

#include "galaxy/platform_time.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render/idle_clock.h"
#include "render/core/frame_core.h"

#include <nebula_frame_list.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace galaxy::render {

using namespace mtl_detail;

namespace {

constexpr SamplerKey kDefaultSamplerKey{1u | (1u << 2u) | (1u << 4u) | (5u << 5u), 0.0f, 32.0f, 0.0f};

// The upscaler setting's values the Metal renderer runs as a temporal scene (render_config.cpp's names).
constexpr unsigned kUpscalerSgsr2 = 5u;
constexpr unsigned kUpscalerMetalFx = 6u;

}  // namespace

// ---- textures and samplers of the draws ----

void RendererMtl::attach_texture_uploads(TextureStore& cache) {
    mirrored_store_ = &cache;
    cache.backend().set_upload_list(frame_->upload_blit);
}

std::uint32_t RendererMtl::texture_slot(const TextureHandle& handle) const noexcept {
    if (handle.resource != nullptr) {
        return handle.srv_index;
    }
    // The array's last slot reads the null texture.
    const std::uint32_t slots = mirrored_store_ != nullptr ? mirrored_store_->backend().array_slots() : 0u;
    return slots != 0u ? slots - 1u : 0u;
}

std::uint32_t RendererMtl::sampler_generation() const noexcept {
    return sampler_generation_ + (mirrored_store_ != nullptr ? mirrored_store_->backend().array_generation() : 0u);
}

// The sampler a key describes (layout: make_sampler_key in render_core.cpp; the other renderers' sampler_for).
mtl::Object RendererMtl::sampler_for(const SamplerKey& key, const RenderConfig& cfg) {
    const SamplerCacheKey cache_key{key, sampler_config_key(cfg)};
    if (const auto it = samplers_.find(cache_key); it != samplers_.end()) {
        return it->second;
    }
    const auto wrap = [](std::uint32_t mode) {
        switch (mode) {
        case 0: return MTLSamplerAddressModeClampToEdge;
        case 2: return MTLSamplerAddressModeMirrorRepeat;
        default: return MTLSamplerAddressModeRepeat;
        }
    };
    const float cfg_bias = static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    const unsigned anisotropy = std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    const bool mag_linear = (key.filter & 16u) != 0u;
    const std::uint32_t minf = (key.filter >> 5u) & 7u;
    // The minified filter (texture_sampler.h min_filter_code): 4 linear, plus 1 the nearest level or 2 between levels.
    const bool min_linear = minf >= 4u;
    // 3: linear without anisotropy (make_sampler_key's arbitrary levels). Levels drawn by hand take neither anisotropy
    // nor the settings' bias (kSamplerAuthoredLevels).
    const bool authored = (key.filter & kSamplerAuthoredLevels) != 0u;
    const bool no_anisotropy = (minf & 3u) == 3u || authored;
    const unsigned mip_mode = (minf & 3u) == 3u ? 2u : minf & 3u;
    const bool use_anisotropic = anisotropy > 1u && mip_mode != 0u && min_linear && mag_linear && !no_anisotropy;
    @autoreleasepool {
        MTLSamplerDescriptor* const descriptor = [MTLSamplerDescriptor new];
        descriptor.magFilter = mag_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        descriptor.minFilter = min_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        descriptor.sAddressMode = wrap(key.filter & 3u);
        descriptor.tAddressMode = wrap((key.filter >> 2u) & 3u);
        descriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
        // An anisotropic filter blends between levels, as Direct3D's does.
        descriptor.mipFilter = mip_mode == 0u ? MTLSamplerMipFilterNotMipmapped
                               : mip_mode == 2u || use_anisotropic ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        descriptor.maxAnisotropy = use_anisotropic ? anisotropy : 1u;
        if (mip_mode != 0u) {
            descriptor.lodMinClamp = key.min_lod;
            descriptor.lodMaxClamp = key.max_lod > key.min_lod ? key.max_lod : key.min_lod;
        }
        // The level bias: Metal's sampler takes one (its descriptor's lodBias), named in the SDK from macOS 15 and
        // iOS 18 and present before them.
        const float bias = std::clamp(key.lod_bias + (authored ? 0.0f : cfg_bias), -16.0f, 16.0f);
        if (bias != 0.0f && [descriptor respondsToSelector:NSSelectorFromString(@"setLodBias:")]) {
            [descriptor setValue:@(bias) forKey:@"lodBias"];
        }
        descriptor.supportArgumentBuffers = bindless_ ? YES : NO;
        id<MTLSamplerState> const made = [metal_device() newSamplerStateWithDescriptor:descriptor];
        if (made == nil) {
            throw std::runtime_error("[samplers] a sampler could not be made");
        }
        const mtl::Object sampler = own(made);
        samplers_.emplace(cache_key, sampler);
        return sampler;
    }
}

void RendererMtl::write_sampler(unsigned slot, mtl::Object sampler) {
    if (@available(macOS 13.0, iOS 16.0, *)) {
        const MTLResourceID resource = [(__bridge id<MTLSamplerState>)sampler gpuResourceID];
        std::memcpy(sampler_array_->mapped + std::size_t{slot} * 8u, &resource, 8u);
    }
}

// Every slot of the sampler array holds `sampler`: a slot no draw named yet reads the default sampler.
void RendererMtl::fill_sampler_array(mtl::Object sampler) {
    for (std::uint32_t slot = 0; slot < sampler_array_slots_; ++slot) {
        write_sampler(slot, sampler);
    }
}

std::uint32_t RendererMtl::sampler_slot(const SamplerKey& key) {
    const RenderConfig cfg = get_render_config();
    const auto allocation = sampler_slots_.get_or_allocate(make_sampler_slot_key(key, cfg));
    if (allocation.index >= sampler_array_slots_) {
        throw std::runtime_error("[samplers] more new samplers in one frame than the sampler array holds");
    }
    if (allocation.created) {
        write_sampler(allocation.index, sampler_for(key, cfg));
    }
    return allocation.index;
}

bool RendererMtl::make_texture_list_chunk(std::uint32_t pairs, std::uint32_t*& mapped, gpu::Address& address) {
    std::shared_ptr<mtl::Buffer> buffer =
        mtl::Context::get().create_buffer(std::uint64_t{pairs} * 8u, mtl::Storage::Shared, "texture lists");
    if (!buffer) {
        return false;
    }
    mapped = reinterpret_cast<std::uint32_t*>(buffer->mapped);
    address = buffer->address;
    texture_list_chunks_.push_back(std::move(buffer));
    return true;
}

gpu::TextureArrays RendererMtl::draw_texture_arrays(gpu::Address lists) {
    gpu::TextureArrays arrays;
    arrays.textures = gpu::Table{reinterpret_cast<std::uint64_t>(
        mirrored_store_ != nullptr ? mirrored_store_->backend().texture_array() : nullptr)};
    arrays.samplers = gpu::Table{reinterpret_cast<std::uint64_t>(sampler_array_ ? sampler_array_->buffer : nullptr)};
    arrays.lists = lists;
    return arrays;
}

void RendererMtl::bind_draw_textures(gpu::Address lists) {
    const gpu::TextureArrays arrays = draw_texture_arrays(lists);
    encoder_.set_texture_arrays(arrays.textures, arrays.samplers, arrays.lists);
}

// Without arrays: a draw's table, as many maps as mtl::kBoundTableSlots, the rest the null texture.
void RendererMtl::write_table(mtl::BoundTextures& table, const TextureHandle* handles, unsigned count,
                              const TextureStore& cache) {
    const TextureBackend& backend = cache.backend();
    for (unsigned m = 0; m < mtl::kBoundTableSlots; ++m) {
        const bool bound = m < count && handles[m].resource != nullptr;
        table.textures[m] = bound ? backend.texture_at(handles[m].srv_index) : backend.null_texture();
    }
}

gpu::Table RendererMtl::texture_table_for(const TextureHandle* handles, unsigned count, const TextureStore& cache) {
    Frame& frame = *frame_;
    if (frame.tables_used == frame.tables.size()) {
        frame.tables.emplace_back();
    }
    mtl::BoundTextures& table = frame.tables[frame.tables_used++];
    write_table(table, handles, count, cache);
    return gpu::Table{reinterpret_cast<std::uint64_t>(&table)};
}

gpu::Table RendererMtl::persistent_texture_table(const TextureHandle* handles, unsigned count, const TextureStore& cache,
                                                 gpu::Table old) {
    const std::uint64_t completed = completed_fence_value();
    std::erase_if(retired_tables_, [&](const RetiredTable& retired) {
        if (retired.fence > completed) {
            return false;
        }
        free_tables_.push_back(retired.table);
        return true;
    });
    if (old.ptr != 0u) {
        retire_texture_table(old);
    }
    mtl::BoundTextures* table = nullptr;
    if (!free_tables_.empty()) {
        table = free_tables_.back();
        free_tables_.pop_back();
    } else {
        persistent_tables_.push_back(std::make_unique<mtl::BoundTextures>());
        table = persistent_tables_.back().get();
    }
    write_table(*table, handles, count, cache);
    return gpu::Table{reinterpret_cast<std::uint64_t>(table)};
}

void RendererMtl::retire_texture_table(gpu::Table table) {
    retired_tables_.push_back({reinterpret_cast<mtl::BoundTextures*>(table.ptr), last_submitted_ + 1u});
}

// A bound table's samplers (without arrays); the maps past `count` read the default sampler.
gpu::Table RendererMtl::sampler_table_for(const SamplerKey* keys, unsigned count) {
    if (count == 0u) {
        return default_sampler_table_;
    }
    const RenderConfig cfg = get_render_config();
    SamplerTableKey table_key = make_sampler_table_key(keys, count, cfg);
    if (const auto it = sampler_tables_.find(table_key); it != sampler_tables_.end()) {
        return gpu::Table{reinterpret_cast<std::uint64_t>(it->second.get())};
    }
    auto table = std::make_unique<mtl::BoundSamplers>();
    for (unsigned i = 0; i < mtl::kBoundTableSlots; ++i) {
        table->samplers[i] = sampler_for(i < count ? keys[i] : kDefaultSamplerKey, cfg);
    }
    const gpu::Table made{reinterpret_cast<std::uint64_t>(table.get())};
    sampler_tables_.emplace(std::move(table_key), std::move(table));
    return made;
}

// Between frames: the samplers made again when the settings change, which also makes the walker ask for its lists or
// tables again. With arrays the sampler array starts again when it is nearly full too, slot 0 the default sampler
// (wrap, linear, mip point, every level) and the slots written once the GPU is past the frames that read them; else
// the default table of draws without samplers of their own is made again.
void RendererMtl::reset_sampler_frame() {
    const RenderConfig cfg = get_render_config();
    const std::uint32_t config = sampler_config_key(cfg);
    if (bindless_) {
        const bool full = sampler_slots_.used() + sampler_frame_room(sampler_array_slots_) > sampler_array_slots_;
        if (sampler_slots_ready_ && config == sampler_config_ && !full) {
            return;
        }
        if (sampler_slots_ready_ && full) {
            std::fprintf(stderr, "[samplers] %u of %u slots made: the array starts again once the GPU is past them\n",
                         sampler_slots_.used(), sampler_array_slots_);
        }
        if (sampler_slots_ready_ && last_submitted_ != 0u) {
            wait_for_fence_value(last_submitted_);
        }
        if (sampler_slots_ready_ && config != sampler_config_) {
            // The samplers of the settings before: a device counts every distinct sampler an array may hold.
            for (auto& [key, sampler] : samplers_) {
                mtl::release(sampler);
            }
            samplers_.clear();
        }
        sampler_slots_.reset();
        sampler_slots_ready_ = true;
        sampler_config_ = config;
        ++sampler_generation_;
        fill_sampler_array(sampler_for(kDefaultSamplerKey, cfg));
        return;
    }
    if (default_sampler_table_.ptr != 0u && config == sampler_config_) {
        return;
    }
    std::array<SamplerKey, mtl::kBoundTableSlots> keys{};
    keys.fill(kDefaultSamplerKey);
    default_sampler_table_ = sampler_table_for(keys.data(), mtl::kBoundTableSlots);
    sampler_config_ = config;
    ++sampler_generation_;
}

gpu::Address RendererMtl::zero_stream(std::uint32_t bytes) {
    if (bytes > zero_stream_bytes_) {
        // Frames in flight read the old one: it goes with them.
        if (zero_vertices_) {
            frames_[frame_slot_].retired_buffers.push_back(std::move(zero_vertices_));
        }
        zero_stream_bytes_ = std::max(bytes, 1u << 16u);
        UploadBuffer memory = gpu::create_upload_memory(device(), zero_stream_bytes_, "zero stream");
        if (!memory) {
            throw std::runtime_error("the zero stream could not be made");
        }
        std::memset(memory.cpu, 0, zero_stream_bytes_);
        zero_vertices_ = std::move(memory.buffer);
        zero_stream_address_ = memory.gpu;
    }
    return zero_stream_address_;
}

// ---- parallel recording ----

// A chunk of a recorded pass replayed inline, as the walker would have drawn it.
void RendererMtl::replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count) {
    set_draw_aux(aux);
    gpu::replay_ops(encoder_, ops, count, [this](const gpu::TextureArrays& arrays) {
        encoder_.set_texture_arrays(arrays.textures, arrays.samplers, arrays.lists);
    });
}

bool RendererMtl::parallel_record_begin(const std::uint8_t* chunk_aux, unsigned chunk_count) {
    constexpr unsigned kMaxRecordPasses = 64u;
    if (chunk_count == 0u || record_passes_ >= kMaxRecordPasses) {
        return false;
    }
    // A tile-based GPU stores the targets at a pass's end and loads them at the next one's start: there every scene pass
    // is a parallel one from now on, and chunks of the pass's own targets run inside it.
    parallel_scene_pass_ = mtl::Context::get().caps().apple_gpu;
    if (parallel_scene_pass_ && parallel_record_in_pass(chunk_aux, chunk_count)) {
        return true;
    }
    // The pass the walk had open ran its draws before; the chunks run in passes of their own.
    end_scene_pass("parallel recording");
    record_aux_.assign(chunk_aux, chunk_aux + chunk_count);
    record_encoders_.assign(chunk_count, nullptr);
    record_runs_.clear();
    record_width_ = scene_.color->width;
    record_height_ = scene_.color->height;
    @autoreleasepool {
        for (std::size_t first = 0; first < record_aux_.size();) {
            const unsigned aux = effective_draw_aux(record_aux_[first]);
            std::size_t next = first;
            while (next < record_aux_.size() && effective_draw_aux(record_aux_[next]) == aux) {
                ++next;
            }
            // A run of chunks with the same targets: one parallel pass, in a command buffer of its own (a command
            // buffer has one encoder open at a time).
            next_command_buffer(GpuPass::Scene);
            scene_pass_aux_ = aux;
            MTLRenderPassDescriptor* const descriptor =
                (__bridge_transfer MTLRenderPassDescriptor*)scene_pass_descriptor(aux, nullptr, false, 0u);
            id<MTLParallelRenderCommandEncoder> const parallel =
                [commands_of(commands_) parallelRenderCommandEncoderWithDescriptor:descriptor];
            if (parallel == nil) {
                throw std::runtime_error("a parallel pass could not begin");
            }
            parallel.label = @"parallel recording";
            for (std::size_t chunk = first; chunk < next; ++chunk) {
                // The chunks run in the order their encoders were made.
                record_encoders_[chunk] = own([parallel renderCommandEncoder]);
            }
            record_runs_.push_back(RecordRun{commands_, own(parallel)});
            ++gpu_window_.passes;
            ++record_passes_;
            first = next;
        }
    }
    // What the frame records after the chunks.
    next_command_buffer(current_pass_);
    return true;
}

bool RendererMtl::parallel_record_in_pass(const std::uint8_t* chunk_aux, unsigned chunk_count) {
    const unsigned aux = effective_draw_aux(chunk_aux[0]);
    for (unsigned chunk = 1; chunk < chunk_count; ++chunk) {
        if (effective_draw_aux(chunk_aux[chunk]) != aux) {
            return false;
        }
    }
    // A pass opened before parallel_scene_pass_ is a plain one: it ends here. begin_scene_pass_with keeps an open pass
    // of these targets.
    if (scene_pass_open_ && scene_parallel_ == nullptr) {
        end_scene_pass("parallel recording");
    }
    begin_scene_pass_with(aux, nullptr, false, "parallel recording");
    if (scene_parallel_ == nullptr) {
        return false;
    }
    record_aux_.assign(chunk_aux, chunk_aux + chunk_count);
    record_encoders_.assign(chunk_count, nullptr);
    record_runs_.clear();
    record_width_ = scene_.color->width;
    record_height_ = scene_.color->height;
    // The encoders run in the order they are made: the draws before, the chunks, then pass_ again for those after.
    if (mirrored_store_ != nullptr) {
        mirrored_store_->backend().set_open_pass(nullptr);
    }
    [pass_of(pass_) endEncoding];
    drop(pass_);
    @autoreleasepool {
        id<MTLParallelRenderCommandEncoder> const parallel = (__bridge id<MTLParallelRenderCommandEncoder>)scene_parallel_;
        for (mtl::Object& encoder : record_encoders_) {
            encoder = own([parallel renderCommandEncoder]);
        }
        pass_ = own([parallel renderCommandEncoder]);
    }
    if (pass_ == nullptr) {
        throw std::runtime_error("the scene pass could not go on after its chunks");
    }
    attach_scene_encoder();
    return true;
}

void RendererMtl::parallel_record_chunk(unsigned chunk, const gpu::DrawOp* ops, std::size_t count) {
    @autoreleasepool {
        const mtl::Object pass = record_encoders_[chunk];
        if (bindless_ && mirrored_store_ != nullptr) {
            const mtl::Object targets[] = {scene_.color->texture,
                                           scene_.depth->texture,
                                           scene_.ms_color ? scene_.ms_color->texture : nullptr,
                                           scene_.ms_depth ? scene_.ms_depth->texture : nullptr,
                                           motion_.motion ? motion_.motion->texture : nullptr,
                                           motion_.reactive ? motion_.reactive->texture : nullptr,
                                           motion_.layer_color ? motion_.layer_color->texture : nullptr,
                                           motion_.layer_trans ? motion_.layer_trans->texture : nullptr};
            mirrored_store_->backend().use_textures(pass, targets, std::size(targets));
        }
        gpu::Encoder encoder;
        encoder.configure(encoding_layout_);
        encoder.attach(pass, record_width_, record_height_);
        gpu::replay_ops(encoder, ops, count, [&encoder](const gpu::TextureArrays& arrays) {
            encoder.set_texture_arrays(arrays.textures, arrays.samplers, arrays.lists);
        });
        [pass_of(pass) endEncoding];
        mtl::release(pass);
        record_encoders_[chunk] = nullptr;
    }
}

void RendererMtl::parallel_record_end() {
    // A chunk that was never replayed still ends its encoder: a parallel pass ends after all of its own.
    for (mtl::Object& pass : record_encoders_) {
        if (pass != nullptr) {
            [pass_of(pass) endEncoding];
            drop(pass);
        }
    }
    if (!record_runs_.empty()) {
        scene_pass_closed_by_ = "parallel recording";
    }
    for (RecordRun& run : record_runs_) {
        [(__bridge id<MTLParallelRenderCommandEncoder>)run.parallel endEncoding];
        drop(run.parallel);
    }
    record_runs_.clear();
    reset_draw_bindings();
}

// ---- test captures ----

void RendererMtl::finish_pictures() {
    wait_for_gpu_idle();
    debug_log_backbuffer_readback();
    wait_picture_files();
}

void RendererMtl::set_debug_capture_paths(std::string backbuffer_ppm_path) {
    backbuffer_capture_path_ = std::move(backbuffer_ppm_path);
}

namespace {

// `width` x `height` pixels of `texture` (four bytes each) into `buffer`, by a blit of `commands`.
void copy_texture_out(id<MTLCommandBuffer> commands, id<MTLTexture> texture, id<MTLBuffer> buffer, unsigned width,
                      unsigned height) {
    id<MTLBlitCommandEncoder> const blit = [commands blitCommandEncoder];
    blit.label = @"capture";
    [blit copyFromTexture:texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(width, height, 1)
                        toBuffer:buffer
               destinationOffset:0
          destinationBytesPerRow:NSUInteger{width} * 4u
        destinationBytesPerImage:NSUInteger{width} * height * 4u];
    [blit endEncoding];
}

}  // namespace

std::shared_ptr<mtl::Buffer> RendererMtl::capture_buffer(std::uint64_t bytes, const char* name) {
    std::shared_ptr<mtl::Buffer> readback =
        captures_.spare([bytes](const std::shared_ptr<mtl::Buffer>& kept) { return kept->size >= bytes; });
    if (!readback) {
        readback = mtl::Context::get().create_buffer(bytes, mtl::Storage::Shared, name);
    }
    if (!readback) {
        std::cerr << "[capture] no readback for the " << name << " (" << bytes << " bytes): the picture is not written\n";
    }
    return readback;
}

bool RendererMtl::debug_copy_selected_display_to_readback(const DisplayTexture& source) {
    if (backbuffer_capture_path_.empty() || !source.texture || source.width == 0u || source.height == 0u) {
        return false;
    }
    const mtl::Texture& texture = *source.texture;
    const bool ten_bit = texture.format == mtl::Format::Rgb10a2;
    // The picture's rows (a handed-over scene target is taller); the formats the display pictures have.
    if ((!ten_bit && texture.format != mtl::Format::Rgba8) || texture.width < source.width || texture.height < source.height) {
        return false;
    }
    std::shared_ptr<mtl::Buffer> readback =
        capture_buffer(static_cast<std::uint64_t>(source.width) * source.height * 4u, "display picture capture");
    if (!readback) {
        return false;
    }
    @autoreleasepool {
        copy_texture_out(commands_of(commands_), texture_of(texture), buffer_of(readback), source.width, source.height);
    }
    captures_.recorded({std::move(readback), 0u, backbuffer_capture_path_ + ".display.ppm", source.width, source.height,
                        source.width * 4u, ten_bit ? Captures::Form::TenBit : Captures::Form::Rgba8});
    return true;
}

void RendererMtl::debug_log_backbuffer_readback() {
    captures_.finished(completed_fence_value(), [](Captures::Capture& capture) {
        write_capture_rows(std::move(capture.path), reinterpret_cast<const std::uint8_t*>(capture.readback->mapped),
                           capture.width, capture.height, capture.pitch, capture.form);
    });
}

// ---- the temporal scene ----

bool RendererMtl::temporal_mode() const {
    const unsigned upscaler = get_render_config().upscaler;
    return (upscaler == kUpscalerSgsr2 && sgsr2_available()) ||
           (upscaler == kUpscalerMetalFx && mtl::Context::get().caps().metalfx_temporal);
}

bool RendererMtl::motion_pass_only() const {
    return get_render_config().upscaler == kUpscalerSgsr2;
}

void RendererMtl::prepare_temporal_upscaler() {
    // The scaler's largest render size: the scene targets' (dynamic resolution's steps are within it).
    const FrameMap render_map = scene_extent_;
    const FrameMap output_map = upscale_output_;
    if (!temporal_mode() || output_map.empty() || render_map.width > output_map.width) {
        if (temporal_) {
            wait_for_gpu_idle();
            temporal_.reset();
        }
        temporal_render_w_ = temporal_render_h_ = 0u;
        temporal_output_w_ = temporal_output_h_ = 0u;
        temporal_history_valid_ = false;
        return;
    }
    const unsigned rw = render_map.width;
    const unsigned rh = render_map.height;
    const unsigned ow = output_map.width;
    const unsigned oh = output_map.height;
    const RenderConfig cfg = get_render_config();
    const unsigned kind = cfg.upscaler;
    const unsigned quality = std::min(cfg.temporal_quality, 5u);
    // A failed creation is not retried until its sizes or mode change.
    if (rw == temporal_render_w_ && rh == temporal_render_h_ && ow == temporal_output_w_ && oh == temporal_output_h_ &&
        kind == temporal_kind_) {
        return;
    }
    if (temporal_) {
        wait_for_gpu_idle();
        temporal_.reset();
    }
    temporal_history_valid_ = false;
    temporal_render_w_ = rw;
    temporal_render_h_ = rh;
    temporal_output_w_ = ow;
    temporal_output_h_ = oh;
    temporal_kind_ = kind;
    temporal_quality_ = quality;
    temporal_ = kind == kUpscalerSgsr2
        ? mtl::create_sgsr2_upscaler(rw, rh, ow, oh, draw_layout_.scene_color)
        : mtl::create_metalfx_upscaler(rw, rh, ow, oh, draw_layout_.scene_color, draw_layout_.scene_color);
    if (temporal_) {
        std::cout << "[Renderer] " << temporal_->name() << " " << rw << "x" << rh << " -> " << ow << "x" << oh << "\n";
    }
}

bool RendererMtl::scene_temporal_begin(float& jitter_x, float& jitter_y) {
    jitter_x = 0.0f;
    jitter_y = 0.0f;
    scene_temporal_ = false;
    scene_motion_ = false;
    const unsigned rw = scene_map_.width;
    const unsigned ow = temporal_output_w_;
    // The upscaler is replaced only by begin_frame (prepare_temporal_upscaler).
    if (!temporal_ || !temporal_mode() || output_scene_active_ || target_pass_active_ ||
        frame_extent_.width != temporal_render_w_ || frame_extent_.height != temporal_render_h_ || rw > temporal_render_w_ ||
        scene_map_.height > temporal_render_h_) {
        temporal_history_valid_ = false;
        return false;
    }
    const unsigned phases = std::max(temporal_->jitter_phase_count(rw, ow), 1u);
    temporal_->jitter_offset(temporal_frame_ % phases, phases, jitter_x, jitter_y);
    scene_jitter_index_ = static_cast<unsigned>(temporal_frame_ % phases);
    scene_jitter_phases_ = phases;
    ++temporal_frame_;
    scene_jitter_[0] = jitter_x;
    scene_jitter_[1] = jitter_y;
    scene_temporal_ = true;
    scene_motion_ = true;
    return true;
}

// Once a frame, after the slot's retired textures are freed: the temporal, output and wide targets are handed back
// after kIdleSteps game steps unused (idle_clock.h), behind the fence of the slot's frame.
void RendererMtl::trim_idle_effect_targets() {
    if (capture_target_ && idle_since(capture_used_step_)) {
        retire(std::move(capture_target_));
    }
    if (motion_.motion && !layer_window_ && idle_since(motion_used_step_)) {
        retire(std::move(motion_.motion));
        retire(std::move(motion_.reactive));
        retire(std::move(motion_.layer_color));
        retire(std::move(motion_.layer_trans));
        reactive_scene_ = false;
        motion_.map = FrameMap{};
    }
    // The output set and the wide set are the live scene target while a swap holds them; between scenes they are idle
    // sets of their own.
    if (alt_scene_.color && !output_scene_active_ && !scene_.wide && idle_since(output_scene_used_step_)) {
        destroy_scene_set(alt_scene_);
        alt_scene_map_ = alt_scene_extent_ = FrameMap{};
    }
    if (wide_scene_.color && !scene_.wide && !output_scene_active_ && !target_pass_active_ &&
        idle_since(wide_scene_used_step_)) {
        destroy_scene_set(wide_scene_);
    }
}

bool RendererMtl::ensure_motion_targets() {
    motion_used_step_ = idle_clock();
    if (motion_.motion && motion_.map == frame_extent_) {
        return true;
    }
    retire(std::move(motion_.motion));
    retire(std::move(motion_.reactive));
    retire(std::move(motion_.layer_color));
    retire(std::move(motion_.layer_trans));
    layer_window_ = false;
    reactive_scene_ = false;
    motion_.map = FrameMap{};
    mtl::Context& context = mtl::Context::get();
    const std::uint32_t width = scene_.color->width;
    const std::uint32_t height = scene_.color->height;
    const std::uint32_t use = mtl::kUseTarget | mtl::kUseSample;
    gpu::TextureRef motion = context.create_texture(width, height, 1u, draw_layout_.motion, use, mtl::Storage::Private, "motion vectors");
    gpu::TextureRef reactive = context.create_texture(width, height, 1u, draw_layout_.reactive, use, mtl::Storage::Private, "reactive mask");
    if (!motion || !reactive) {
        std::cerr << "[Renderer] motion targets " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    // The effects layer's two targets, at the scene target's size; without them the scene runs as before
    // (begin_layer_window does nothing).
    gpu::TextureRef layer_color, layer_trans;
    if (draw_layout_.layer_targets) {
        layer_color = context.create_texture(width, height, 1u, draw_layout_.layer, use, mtl::Storage::Private, "effects layer colour");
        layer_trans = context.create_texture(width, height, 1u, draw_layout_.layer, use, mtl::Storage::Private,
                                             "effects layer transmittance");
        if (!layer_color || !layer_trans) {
            std::cerr << "[Renderer] effects layer targets " << width << 'x' << height << " allocation failed\n";
            layer_color.reset();
            layer_trans.reset();
        }
    }
    motion_.motion = std::move(motion);
    motion_.reactive = std::move(reactive);
    motion_.layer_color = std::move(layer_color);
    motion_.layer_trans = std::move(layer_trans);
    motion_.map = frame_extent_;
    // The layer targets start as an empty layer (nothing added, everything let through), as clear_layer_targets leaves
    // them, so a read before the first layer window sees that; the others start at zero.
    end_scene_pass("ensure_motion_targets");
    {
        const mtl::Texture* const targets[2] = {motion_.motion.get(), motion_.reactive.get()};
        const float zero[2][4] = {};
        clear_textures(targets, zero, 2u);
    }
    if (motion_.layer_color) {
        clear_layer_targets();
    }
    return true;
}

bool RendererMtl::begin_motion_scene() {
    const bool cleared = std::exchange(motion_cleared_, false);
    reactive_scene_ = false;
    layer_window_ = false;
    if (!scene_motion_ || !ensure_motion_targets()) {
        return false;
    }
    if (!cleared) {
        clear_motion_targets();
    }
    reactive_scene_ = !motion_pass_only();
    return true;
}

// The layer opens when a scene writes the reactive mask and both targets exist: it is cleared (nothing added,
// everything let through) and the scene's blended draws that never read the scene write it too until the next scene.
void RendererMtl::begin_layer_window() {
    if (!reactive_scene_ || layer_window_ || !motion_.layer_color || !motion_.layer_trans || !draw_layout_.layer_targets) {
        return;
    }
    clear_layer_targets();
    layer_window_ = true;
}

void RendererMtl::clear_layer_targets() {
    end_scene_pass("clear_layer_targets");
    const mtl::Texture* const targets[2] = {motion_.layer_color.get(), motion_.layer_trans.get()};
    const float values[2][4] = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    clear_textures(targets, values, 2u);
}

bool RendererMtl::begin_cleared_motion_scene(std::uint32_t clear_rgba) {
    // scene_temporal_begin's conditions, which the scene's begin tests next.
    if (scene_pass_open_ || target_pass_active_ || output_scene_active_ || !temporal_ || !temporal_mode() ||
        frame_extent_.width != temporal_render_w_ || frame_extent_.height != temporal_render_h_ ||
        scene_map_.width > temporal_render_w_ || scene_map_.height > temporal_render_h_ || scene_.samples != 1u ||
        !ensure_motion_targets()) {
        return false;
    }
    if (!motion_pass_only()) {
        clear_motion_targets(false);  // the reactive mask, while no pass is open
    }
    const SceneClearValues clear = make_scene_clear_values(clear_rgba, 0u);
    begin_scene_pass_with(1u, &clear, false, "begin_cleared_motion_scene");
    motion_cleared_ = true;
    return true;
}

void RendererMtl::clear_motion_targets(bool motion) {
    end_scene_pass();
    // Nothing reads the reactive mask when the scene is one pass (motion_pass_only), and nothing writes it.
    const bool reactive = !motion_pass_only();
    const mtl::Texture* targets[2]{};
    const float zero[2][4] = {};
    unsigned count = 0u;
    if (motion) {
        targets[count++] = motion_.motion.get();
    }
    if (reactive) {
        targets[count++] = motion_.reactive.get();
    }
    if (count != 0u) {
        clear_textures(targets, zero, count);
    }
}

bool RendererMtl::ensure_output_set(const FrameMap& output, bool storage) {
    output_scene_used_step_ = idle_clock();
    if (alt_scene_.color && alt_scene_map_ == output && (alt_scene_storage_ || !storage)) {
        return true;
    }
    if (alt_scene_.color) {
        destroy_scene_set(alt_scene_);
    }
    scene_set_storage_ = storage;
    const bool made = create_scene_set(alt_scene_, output);
    scene_set_storage_ = false;
    if (!made) {
        alt_scene_map_ = alt_scene_extent_ = FrameMap{};
        return false;
    }
    alt_scene_map_ = alt_scene_extent_ = output;
    alt_scene_storage_ = storage;
    return true;
}

bool RendererMtl::record_temporal_upscale(const FrameMap& output) {
    end_scene_pass();
    const GpuPassScope scope{*this, GpuPass::Upscale};
    // The scene's rectangle within the scaler's render size (dynamic resolution's step: the upscale names the size it
    // reads).
    if (!temporal_ || !motion_.motion || !scene_.color || !alt_scene_.color || frame_extent_.width != temporal_render_w_ ||
        frame_extent_.height != temporal_render_h_ || scene_map_.width > temporal_render_w_ ||
        scene_map_.height > temporal_render_h_ || scene_map_.empty() || output.width != temporal_output_w_ ||
        output.height != temporal_output_h_ || scene_.wide) {
        temporal_history_valid_ = false;
        return false;
    }
    mtl::TemporalUpscaleInputs in{};
    in.commands = commands_;
    in.color = scene_.color.get();
    in.depth = scene_.depth.get();
    in.motion = motion_.motion.get();
    in.reactive = motion_.reactive.get();
    in.output = alt_scene_.color.get();
    in.camera_near = scene_lens_[0];
    in.camera_far = scene_lens_[1];
    in.fov_y = scene_lens_[2];
    in.view = scene_view_;
    in.view_known = scene_view_known_;
    in.render_width = scene_map_.width;
    in.render_height = scene_map_.height;
    in.output_width = output.width;
    in.output_height = output.height;
    in.jitter_x = scene_jitter_[0];
    in.jitter_y = scene_jitter_[1];
    // The time since the last upscale, as measured.
    const std::int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    in.frame_ms = temporal_last_upscale_us_ != 0 ? static_cast<float>(now_us - temporal_last_upscale_us_) * 0.001f : 0.0f;
    temporal_last_upscale_us_ = now_us;
    in.reset = !temporal_history_valid_;
    const bool recorded = temporal_->evaluate(in);
    reset_draw_bindings();
    temporal_history_valid_ = recorded;
    return recorded;
}

bool RendererMtl::begin_output_scene(bool resolve_msaa_only) {
    if (resolve_msaa_only) {
        const FrameMap output = output_frame_for_window();
        if (output_scene_active_ || scene_temporal_ || scene_.samples <= 1u || output != scene_map_) {
            return false;
        }
        return enter_output_scene(output);
    }
    const bool temporal = scene_temporal_;
    scene_temporal_ = false;
    if (output_scene_active_) {
        return false;
    }
    if (temporal) {
        const FrameMap upscaled_map(temporal_output_w_, temporal_output_h_);
        if (!upscaled_map.empty() && upscaled_map.width >= scene_map_.width &&
            ensure_output_set(upscaled_map, temporal_ && temporal_->writes_output()) && record_temporal_upscale(upscaled_map)) {
            swap_scene_sets();
            bind_scene();
            clear_scene(0u, false, false, true);
            return true;
        }
        temporal_history_valid_ = false;
    }
    const FrameMap output = temporal && !upscale_output_.empty() ? upscale_output_ : output_frame_for_window();
    // Nebula's screens with small text are drawn at the window's resolution whatever the HUD's. Dynamic resolution's
    // rectangle: the rest of the frame (post AA's result, the 2D layer, the display picture) at the output's size,
    // whatever the step (the HUD never follows it).
    const bool rectangle = scene_map_ != frame_extent_;
    const bool full = temporal || rectangle || render_hud_at_output(get_render_config()) || sharp_2d_requested();
    if (!full || output.empty() || (output.width < scene_map_.width && !rectangle) ||
        (!temporal && !rectangle && output.width == scene_map_.width)) {
        return false;
    }
    return enter_output_scene(output);
}

// The output scene target takes the scene target's place, its pass opening cleared (colour, depth to the far plane,
// stencil) with the whole scene target resampled into it as the first draw, with the display copy's identity settings
// (no filter, gamma 1.0, alpha kept; record_conversion_blit's shaders and constants). The resampled picture stays in
// tile memory for the 2D layer, where a copy pass of its own stored it and the scene pass loaded it.
bool RendererMtl::enter_output_scene(const FrameMap& output) {
    if (!ensure_output_set(output, false)) {
        return false;
    }
    const FramePassGeometry sampling = frame_pass_geometry(output.width, output.height, scene_map_);
    const bool display_filter = sampling.step_x == 1.0f && sampling.step_y == 1.0f;
    // The drawn part (dynamic resolution's rectangle) bounds every sample.
    const FrameMap part = scene_part();
    const SceneConversionShaderConstants constants =
        make_scene_conversion_constants(kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
                                        static_cast<float>(part.width), static_cast<float>(part.height));
    swap_scene_sets();
    bind_scene();
    const SceneClearValues clear = make_scene_clear_values(0u, 0u);
    begin_scene_pass(&clear, "enter_output_scene");
    utility_draw(pass_,
                 scene_pass_pipeline(display_filter ? mtl::UtilityShader::DisplayFilterPs : mtl::UtilityShader::SceneConversionPs,
                                     static_cast<std::uint8_t>(kWriteAll)),
                 DepthUse::None, pass_width_, pass_height_,
                 gpu::rect(0, 0, static_cast<std::int32_t>(pass_width_), static_cast<std::int32_t>(pass_height_)), &constants,
                 sizeof(constants), alt_scene_.color->texture);
    reset_draw_bindings();
    bind_scene();
    return true;
}

void RendererMtl::end_output_scene() {
    if (!output_scene_active_) {
        return;
    }
    swap_scene_sets();
    bind_scene();
}

bool RendererMtl::interpolation_mode() const {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
    if (temporal_mode() && temporal_interp_hold_.held()) {
        return false;
    }
    return interpolated_frames_per_game_frame(rate) != 0u;
}

void RendererMtl::suspend_temporal_interpolation(double ms_per_frame) {
    if (temporal_interp_hold_.held()) {
        return;
    }
    temporal_interp_hold_.hold(galaxy::qpc_to_ns(galaxy::qpc_now()));
    temporal_interp_key_ = temporal_interp_key();
    std::cerr << "[interp] temporal upscaling of generated frames off: " << ms_per_frame
              << " ms GPU per presented frame; tried again in " << temporal_interp_hold_.wait_ns() / 1000000000 << " s\n";
}

RendererMtl::TemporalInterpKey RendererMtl::temporal_interp_key() const {
    const RenderConfig cfg = get_render_config();
    TemporalInterpKey key{};
    key.upscaler = cfg.upscaler;
    key.quality = cfg.temporal_quality;
    key.scene_map = scene_extent_;  // the render scene target's size (a dynamic resolution step is not a new key)
    key.output_map = upscale_output_;
    key.max_fps = presented_max_fps(cfg);
    key.vsync = cfg.vsync_interval;
    key.refresh_hz = display_refresh_hz_;
    return key;
}

}  // namespace galaxy::render
