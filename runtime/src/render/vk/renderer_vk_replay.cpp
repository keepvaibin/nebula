// SPDX-License-Identifier: GPL-3.0-only
// Interpolated sub-frames on Vulkan (renderer_vk.h). Each game frame's draws, clears and copies are logged; a
// sub-frame evaluates the moving constants at its t on the GPU (interp_eval.h), replays the log with them into a
// private scene target (the chunks of the next game frame keep the live one meanwhile) and presents its display copy. The design
// is RendererD3D12's (renderer_d3d12.cpp, begin_replay_record to present_replay_subframe): the replay is recorded
// once per sequence and submitted again for each of its sub-frames.
#include "galaxy/render/vk/renderer_vk.h"

#include "renderer_vk_internal.h"

#include "galaxy/render/frame_pass.h"
#include "galaxy/render/interp_eval.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/texture_store.h"

#include <span>
#include <tuple>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

namespace galaxy::render {

using namespace vk_detail;

namespace {

// InterpEvalFrame blocks at uniform-offset alignment: two per sub-frame slot (the copies' dispatch, the ops').
constexpr VkDeviceSize kEvalFrameBlock = 256u;

}  // namespace

bool RendererVk::interpolation_mode() const {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
    // With a temporal upscaler every presented frame is upscaled, its motion from the frame presented before, unless
    // the GPU could not afford it.
    if (temporal_mode() && temporal_interp_suspended_) {
        return false;
    }
    return interpolated_frames_per_game_frame(rate) != 0u;
}

RendererVk::TemporalInterpKey RendererVk::temporal_interp_key() const {
    const RenderConfig cfg = get_render_config();
    TemporalInterpKey key{};
    key.upscaler = cfg.upscaler;
    key.quality = cfg.temporal_quality;
    key.scene_map = output_scene_active_ ? alt_scene_map_ : scene_map_;
    key.output_map = upscale_output_;
    key.max_fps = presented_max_fps(cfg);
    key.vsync = cfg.vsync_interval;
    key.refresh_hz = display_refresh_hz_;
    return key;
}

void RendererVk::suspend_temporal_interpolation(double ms_per_frame) {
    if (temporal_interp_suspended_) {
        return;
    }
    temporal_interp_suspended_ = true;
    temporal_interp_key_ = temporal_interp_key();
    std::cerr << "[interp] temporal upscaling of the replays off: " << ms_per_frame << " ms GPU per presented frame\n";
}

// Interp: the upload rings, texture tables and retired textures are segmented per game frame, so a logged frame's
// data lives until its segment comes round again; Normal: per frame slot. A change, or a ring whose frames took over
// half their budget, waits for an idle GPU.
void RendererVk::update_ring_mode() {
    if (temporal_interp_suspended_ && temporal_interp_key() != temporal_interp_key_) {
        // The settings changed: decided again.
        temporal_interp_suspended_ = false;
    }
    const RingMode mode = interpolation_mode() ? RingMode::Interp
        : temporal_mode() ? RingMode::Temporal : RingMode::Normal;
    const bool grow = !ring_growth_failed_ &&
        (vertex_ring_.wants_growth() || index_ring_.wants_growth() || constant_ring_.wants_growth() ||
         matrix_ring_.wants_growth());
    if (mode == ring_mode_ && !grow) {
        return;
    }
    drain_gpu();
    const bool interp = mode == RingMode::Interp;
    const unsigned segments = interp ? kInterpSegments : kFramesInFlight;
    const VkDevice device = vk::Context::get().device();
    // Temporal: one matrix segment more holds the previous scene's palettes.
    const unsigned matrix_segments = mode == RingMode::Temporal ? kFramesInFlight + 1u : segments;
    const auto place = [&](UploadRing& ring, const char* name, unsigned segments) {
        const std::size_t had = ring.capacity();
        std::size_t capacity = ring_growth_failed_ ? had : ring.grown_capacity(segments);
        if (capacity == 0u) {
            ring_growth_failed_ = true;
            capacity = had;
        }
        if (capacity == had) {
            return ring.resegment(UploadRing::budget(capacity, segments), segments);
        }
        std::cerr << "[gpu-rings] grow " << name << " frame-high-water-kb=" << (ring.high_water() >> 10u)
                  << " mb=" << (had >> 20u) << "->" << (capacity >> 20u) << '\n';
        ring.reset();
        if (ring.initialize(device, name, UploadRing::budget(capacity, segments), segments, capacity)) {
            return true;
        }
        ring_growth_failed_ = true;
        return ring.initialize(device, name, UploadRing::budget(had, segments), segments, had);
    };
    if (!place(vertex_ring_, "vertex", segments) || !place(index_ring_, "index", segments) ||
        !place(constant_ring_, "constant", segments) || !place(matrix_ring_, "matrix", matrix_segments)) {
        throw std::runtime_error("upload ring resize failed");
    }
    matrix_segments_ = mode == RingMode::Temporal ? matrix_segments : kFramesInFlight;
    matrix_segment_ = 0u;
    matrix_segment_used_ = false;
    matrix_segment_started_ = false;
    // Rebuilt rings may be new buffers: no cached set may name the old ones.
    constants_sets_.clear();
    reset_draw_bindings();
    for (Segment& segment : segments_) {
        segment.retired.clear();
        segment.snapshots.clear();
        segment.fence = 0u;
    }
    feedback_.clear();
    for (ReplayList& list : replay_lists_) {
        list.sequence = 0u;
    }
    recording_log_valid_ = false;
    ready_valid_ = false;
    run_valid_ = false;
    prev_valid_ = false;
    sequence_.ready = false;
    start_clear_valid_ = false;
    ring_segment_ = 0u;
    ring_advance_pending_ = interp;
    ring_mode_ = mode;
}

// A game frame's first renderer frame in Interp: the next segment, once the GPU is past every submission that read it.
void RendererVk::advance_ring_segment() {
    ring_advance_pending_ = false;
    ring_segment_ = (ring_segment_ + 1u) % kInterpSegments;
    if (ready_valid_ && ready_log_.segment == ring_segment_) {
        ready_valid_ = false;
    }
    if (run_valid_ && run_log_.segment == ring_segment_) {
        run_valid_ = false;
        sequence_.ready = false;
    }
    for (ReplayList& list : replay_lists_) {
        if (list.segment == ring_segment_) {
            list.sequence = 0u;
        }
    }
    Segment& segment = segments_[ring_segment_];
    if (segment.fence != 0u) {
        wait_for_fence_value(segment.fence);
    }
    segment.retired.clear();
    const VkDevice device = vk::Context::get().device();
    if (segment.descriptors == VK_NULL_HANDLE) {
        segment.descriptors = create_pool(device, kFrameSets, {.images = kFrameSets * 8u});
    } else {
        vk::check(vkResetDescriptorPool(device, segment.descriptors, 0), "vkResetDescriptorPool");
    }
    for (VkDescriptorPool pool : segment.overflow) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    segment.overflow.clear();
    vertex_ring_.begin_frame(ring_segment_);
    index_ring_.begin_frame(ring_segment_);
    constant_ring_.begin_frame(ring_segment_);
    matrix_ring_.begin_frame(ring_segment_);
    begin_replay_record();
    take_log_snapshots();
}

// A table being logged: its copy textures this log has not written yet are read as an earlier frame left them.
void RendererVk::note_feedback_reads(const std::array<VkImageView, kMaxTextureMaps>& views) {
    for (const VkImageView view : views) {
        if (view == VK_NULL_HANDLE ||
            std::find(recording_log_.written.begin(), recording_log_.written.end(), view) !=
                recording_log_.written.end()) {
            continue;
        }
        if (const auto known = std::find_if(feedback_.begin(), feedback_.end(),
                                            [view](const Feedback& entry) { return entry.view == view; });
            known != feedback_.end()) {
            known->serial = replay_serial_ + 1u;
            continue;
        }
        for (const auto& [key, dest] : frame_pass_targets_) {
            if (dest.texture && dest.texture->view == view) {
                feedback_.push_back({view, dest.texture, replay_serial_ + 1u});
                break;
            }
        }
    }
}

// The log's start, before its first pass: the copy textures recent logs read before rewriting them, copied as they
// are now (their earlier frame's content), from the segment's snapshot pool.
void RendererVk::take_log_snapshots() {
    // A texture no log read so in the last kInterpSegments frames, or no longer a copy destination, goes.
    std::erase_if(feedback_, [this](const Feedback& entry) {
        return entry.serial + kInterpSegments < replay_serial_ + 1u || entry.texture.use_count() <= 1;
    });
    Segment& segment = segments_[ring_segment_];
    std::vector<gpu::TextureRef> pool = std::move(segment.snapshots);
    segment.snapshots.clear();
    const VkCommandBuffer commands = commands_;
    for (const Feedback& entry : feedback_) {
        vk::Texture& source = *entry.texture;
        gpu::TextureRef snapshot;
        for (gpu::TextureRef& candidate : pool) {
            if (candidate && candidate->width == source.width && candidate->height == source.height) {
                snapshot = std::move(candidate);
                break;
            }
        }
        if (!snapshot) {
            snapshot = vk::Context::get().create_texture(source.width, source.height, 1u, source.format,
                                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                                         false, "sub-frame copy snapshot");
            if (!snapshot) {
                ++redirect_stats_.fallback_logs;
                continue;
            }
        }
        image_barrier(commands, source.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        image_barrier(commands, snapshot->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {source.width, source.height, 1};
        vkCmdCopyImage(commands, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, snapshot->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(commands, source.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        image_barrier(commands, snapshot->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        snapshot->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        recording_log_.snapshots.emplace_back(entry.view, snapshot);
        segment.snapshots.push_back(std::move(snapshot));
        ++redirect_stats_.snapshots;
    }
    // Unused pool textures retire with the segment.
    for (gpu::TextureRef& unused : pool) {
        if (unused) {
            segment.retired.push_back(std::move(unused));
        }
    }
}

// Texture tables: the frame slot's pool, or in Interp the game frame's segment (its replays bind them).
VkDescriptorSet RendererVk::allocate_table_set(VkDescriptorSetLayout layout) {
    if (ring_mode_ != RingMode::Interp) {
        return allocate_frame_set(layout);
    }
    Segment& segment = segments_[ring_segment_];
    const VkDevice device = vk::Context::get().device();
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = segment.overflow.empty() ? segment.descriptors : segment.overflow.back();
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device, &info, &set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        segment.overflow.push_back(create_pool(device, kFrameSets, {.images = kFrameSets * 8u}));
        info.descriptorPool = segment.overflow.back();
        result = vkAllocateDescriptorSets(device, &info, &set);
    }
    vk::check(result, "vkAllocateDescriptorSets");
    return set;
}

void RendererVk::ReplayLog::clear() {
    ops.clear();
    draws.clear();
    clears.clear();
    copies.clear();
    blooms.clear();
    tables.clear();
    movie = false;
    snapshots.clear();
    written.clear();
}

void RendererVk::begin_replay_record() {
    recording_log_.clear();
    recording_log_.segment = ring_segment_;
    replay_scene_count_ = 0u;
    replay_screen_frame_ = false;
    recording_log_valid_ = true;
    if (start_clear_valid_) {
        start_clear_valid_ = false;
        recording_log_.ops.push_back({ReplayKind::Clear, 0u});
        recording_log_.clears.push_back(start_clear_);
    }
}

void RendererVk::close_replay_log(std::uint32_t picture_slot) {
    recording_log_valid_ = false;
    // One 3D scene, or none in a frame recorded as its screen layer alone (the pause menu): a frame without one
    // otherwise (menus between scenes), or with two, is shown as it is.
    const bool valid = replay_scene_count_ == 1u || (replay_scene_count_ == 0u && replay_screen_frame_);
    replay_reject_ = valid ? 0u : replay_scene_count_ == 0u ? 1u : 2u;
    ++replay_serial_;
    replay_picture_slot_ = picture_slot;
    if (valid) {
        recording_log_.serial = replay_serial_;
        std::swap(recording_log_, ready_log_);
        ready_valid_ = true;
    }
    ring_advance_pending_ = true;
}

bool RendererVk::start_replay_playback() {
    if (!ready_valid_) {
        return false;
    }
    std::swap(ready_log_, run_log_);
    run_valid_ = true;
    ready_valid_ = false;
    return true;
}

bool RendererVk::ensure_interp_eval() {
    if (eval_pipeline_ != VK_NULL_HANDLE) {
        return true;
    }
    if (eval_failed_) {
        return false;
    }
    eval_failed_ = true;
    const VkDevice device = vk::Context::get().device();
    VkDescriptorSetLayoutBinding bindings[5]{};
    for (std::uint32_t i = 0; i < 5u; ++i) {
        bindings[i].binding = i < 4u ? i : 5u;
        bindings[i].descriptorType = i == 4u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 5;
    layout_info.pBindings = bindings;
    vk::check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &eval_set_layout_), "eval set layout");
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &eval_set_layout_;
    vk::check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &eval_layout_), "eval layout");
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4u * 3u},
                                          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3u}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 3;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = sizes;
    vk::check(vkCreateDescriptorPool(device, &pool_info, nullptr, &eval_pool_), "eval descriptor pool");
    for (const auto& [shader, entry, pipeline] :
         {std::tuple{vk::UtilitySpirv::InterpCopyCs, "copy_main", &eval_copy_pipeline_},
          std::tuple{vk::UtilitySpirv::InterpEvalCs, "eval_main", &eval_pipeline_}}) {
        const std::span<const std::uint32_t> spirv = vk::utility_spirv(shader);
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = spirv.size_bytes();
        module_info.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        vk::check(vkCreateShaderModule(device, &module_info, nullptr, &module), "eval shader module");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = entry;
        info.layout = eval_layout_;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, pipeline);
        vkDestroyShaderModule(device, module, nullptr);
        vk::check(result, "eval pipeline");
    }
    vk::Context& context = vk::Context::get();
    eval_frames_ = context.create_buffer(kEvalFrameBlock * 2u * subframes_.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         "interpolation frames");
    if (!eval_frames_) {
        return false;
    }
    eval_failed_ = false;
    return true;
}

// The constants every sub-frame's draws read: storage written by the evaluation, uniform (b0, b1) and storage (the
// palette) read by the replay. Grows by doubling; the old one lives until the sub-frames reading it complete.
bool RendererVk::ensure_interp_output(std::uint64_t bytes) {
    bytes = std::max<std::uint64_t>(bytes, 256u);
    if (interp_output_ && interp_output_->size >= bytes) {
        return true;
    }
    std::uint64_t size = std::max<std::uint64_t>(interp_output_ ? interp_output_->size * 2u : 0u, 4ull << 20u);
    while (size < bytes) {
        size *= 2u;
    }
    if (interp_output_) {
        interp_retired_.emplace_back(std::move(interp_output_), last_submitted_);
    }
    interp_output_ = vk::Context::get().create_buffer(
        size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "interpolated constants");
    interp_output_address_ = interp_output_ ? interp_output_->address : 0u;
    return static_cast<bool>(interp_output_);
}

// The evaluation's set for a program buffer, rewritten when a buffer it names changed.
VkDescriptorSet RendererVk::eval_set(ProgramBuffer& program) {
    const auto ring_buffer = [](UploadRing& ring) { return reinterpret_cast<VkBuffer>(ring.resource()); };
    const std::array<VkBuffer, 4> buffers = {program.buffer->buffer, ring_buffer(constant_ring_),
                                             ring_buffer(matrix_ring_), interp_output_->buffer};
    if (program.set != VK_NULL_HANDLE && program.bound == buffers) {
        return program.set;
    }
    const VkDevice device = vk::Context::get().device();
    if (program.set == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = eval_pool_;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &eval_set_layout_;
        vk::check(vkAllocateDescriptorSets(device, &info, &program.set), "eval set");
    }
    // A set a submitted, unfinished sub-frame may still use: idle first (the buffers change rarely).
    if (program.set != VK_NULL_HANDLE && program.bound[0] != VK_NULL_HANDLE) {
        drain_gpu();
    }
    VkDescriptorBufferInfo infos[5]{};
    for (unsigned i = 0; i < 4u; ++i) {
        infos[i] = {buffers[i], 0, VK_WHOLE_SIZE};
    }
    infos[4] = {eval_frames_->buffer, 0, sizeof(InterpEvalFrame)};
    VkWriteDescriptorSet writes[5]{};
    for (unsigned i = 0; i < 5u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = program.set;
        writes[i].dstBinding = i < 4u ? i : 5u;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 4u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
    program.bound = buffers;
    return program.set;
}

// The sequence's base copies (its first sub-frame, and the frame at t = 1) and its ops at t, into the output the
// replay reads. The sub-frame's slot picks its evaluation constants.
void RendererVk::record_interp_eval(VkCommandBuffer commands, const SubframeTime& time, const Affine34& camera_t,
                                    const Affine34& camera_prev) {
    const unsigned slot = static_cast<unsigned>(frame_ - subframes_.data());
    const bool last = time.t >= 1.0f;
    InterpEvalFrame frame{};
    std::memcpy(frame.camera, camera_t.data(), sizeof(frame.camera));
    std::memcpy(frame.camera_prev, camera_prev.data(), sizeof(frame.camera_prev));
    frame.mode = last ? 1u : 0u;
    frame.t = time.t;
    frame.t_prev = time.t_prev;
    frame.jitter[0] = time.jitter_x;
    frame.jitter[1] = time.jitter_y;
    frame.payload = sequence_.payload_offset;
    frame.program_last = sequence_.program_bytes - 16u;
    frame.output_last = static_cast<std::uint32_t>(std::min<std::uint64_t>(interp_output_->size - 4u, 0xFFFFFFF0u));
    frame.constants_last = static_cast<std::uint32_t>(std::min<std::uint64_t>(constant_ring_.total_bytes() - 4u, 0xFFFFFFF0u));
    frame.matrices_last = static_cast<std::uint32_t>(std::min<std::uint64_t>(matrix_ring_.total_bytes() - 4u, 0xFFFFFFF0u));
    ProgramBuffer& program = program_buffers_[program_index_];
    const VkDescriptorSet set = eval_set(program);
    const auto copies_offset = static_cast<std::uint32_t>(kEvalFrameBlock * 2u * slot);
    const auto ops_offset = static_cast<std::uint32_t>(kEvalFrameBlock * (2u * slot + 1u));

    // The output is rewritten after the previous sub-frame's draws read it (queue order).
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);
    if ((sequence_.first || last) && sequence_.copies != 0u) {
        frame.count = sequence_.copies;
        frame.table = sequence_.copies_offset;
        std::memcpy(eval_frames_->mapped + copies_offset, &frame, sizeof(frame));
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, eval_layout_, 0, 1, &set, 1,
                                &copies_offset);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, eval_copy_pipeline_);
        vkCmdDispatch(commands, sequence_.copies, 1u, 1u);
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);
    }
    frame.count = sequence_.ops;
    frame.table = sequence_.ops_offset;
    std::memcpy(eval_frames_->mapped + ops_offset, &frame, sizeof(frame));
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, eval_layout_, 0, 1, &set, 1, &ops_offset);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, eval_pipeline_);
    vkCmdDispatch(commands, std::max(1u, (sequence_.ops + 63u) / 64u), 1u, 1u);
    eval_readback_recorded_ = false;
    if (eval_readback_requested_ && eval_readback_fence_ == 0u) {
        eval_readback_requested_ = false;
        if (!eval_readback_ || eval_readback_->size < interp_output_->size) {
            eval_readback_ = vk::Context::get().create_readback_buffer(interp_output_->size, "interpolation readback");
        }
        if (eval_readback_) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);
            const VkBufferCopy region{0, 0, interp_output_->size};
            vkCmdCopyBuffer(commands, interp_output_->buffer, eval_readback_->buffer, 1, &region);
            eval_readback_t_ = time.t;
            eval_readback_recorded_ = true;
        }
    }
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier,
                         0, nullptr, 0, nullptr);
}

// A sequence: the base copies of every moving block from the rings, then the program (copies, ops, payload) into one of
// the three program buffers.
bool RendererVk::begin_replay_sequence(const ReplaySequence& sequence) {
    sequence_.ready = false;
    if (!run_valid_ || sequence.program == nullptr) {
        replay_failure_ = run_valid_ ? 2u : 1u;
        return false;
    }
    const ReplayCopy* display = nullptr;
    for (const ReplayCopy& copy : run_log_.copies) {
        if (copy.display) display = &copy;
    }
    if (display == nullptr) {
        replay_failure_ = 3u;
        return false;
    }
    if (!ensure_interp_eval()) {
        replay_failure_ = 7u;
        return false;
    }
    if (!ensure_interp_output(sequence.layout.bytes())) {
        replay_failure_ = 8u;
        return false;
    }
    const std::uint64_t completed = completed_fence_value();
    std::erase_if(interp_retired_, [completed](const auto& retired) { return retired.second <= completed; });
    const auto record_start = std::chrono::steady_clock::now();

    const interp_gpu::Layout& layout = sequence.layout;
    const gpu::Address constants_base = constant_ring_.gpu_base();
    const std::uint64_t constants_bytes = constant_ring_.total_bytes();
    const std::uint64_t matrices_bytes = matrix_ring_.total_bytes();
    interp_copies_.clear();
    interp_copied_.assign(static_cast<std::size_t>(layout.vs_count), 0u);
    const auto copy_block = [&](gpu::Address address, std::uint32_t bytes, std::uint32_t dst) {
        if (address < constants_base || address + bytes > constants_base + constants_bytes) {
            return false;
        }
        interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Constants),
                                  static_cast<std::uint32_t>(address - constants_base), dst, bytes / 4u});
        return true;
    };
    for (const DrawCall& call : run_log_.draws) {
        if (call.replay_record >= 0 && static_cast<std::size_t>(call.replay_record) < sequence.record_vs_slot.size()) {
            const std::int32_t slot = sequence.record_vs_slot[static_cast<std::size_t>(call.replay_record)];
            if (slot >= 0 && interp_copied_[static_cast<std::size_t>(slot)] == 0u) {
                interp_copied_[static_cast<std::size_t>(slot)] = 1u;
                if (!copy_block(call.vs_constants, interp_gpu::kVsBytes,
                                layout.vs_offset(static_cast<std::uint32_t>(slot)))) {
                    replay_failure_ = 9u;
                    return false;
                }
            }
        }
    }
    // Each palette slot takes its source draw's blocks (draw_matrices.h) at their rows; a motion vector's slot its
    // position and texture blocks.
    static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 5> kBlocks{{
        {draw_rows::kPosition, draw_rows::kPositionRows}, {draw_rows::kTexture, draw_rows::kTextureRows},
        {draw_rows::kNormal, draw_rows::kNormalRows}, {draw_rows::kPost, draw_rows::kPostRows},
        {draw_rows::kLight, draw_rows::kLightRows}}};
    for (std::uint32_t q = 0; q < layout.palette_count && q < sequence.palette_source.size(); ++q) {
        const std::array<std::uint32_t, 5>& source = sequence.palette_source[q];
        for (std::size_t k = 0; k < kBlocks.size(); ++k) {
            if ((static_cast<std::uint64_t>(source[k]) + kBlocks[k].second) * 16u > matrices_bytes) {
                replay_failure_ = 9u;
                return false;
            }
        }
        const auto slot = [&](std::uint32_t offset, std::uint32_t rows, std::size_t blocks) {
            const std::uint32_t first = offset + rows * 4u;  // its rows, after the unused float4
            for (std::size_t k = 0; k < blocks; ++k) {
                interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices), source[k] * 16u,
                                          first + kBlocks[k].first * 16u, kBlocks[k].second * 4u});
            }
        };
        slot(layout.palette_offset(q), interp_gpu::kPaletteRows, kBlocks.size());
        if (q < layout.prev_palette_count) {
            slot(layout.prev_palette_offset(q), interp_gpu::kPrevPaletteRows, 2u);
        }
    }

    // The program (16 bytes of tail room for Load4), in a buffer no sub-frame in flight reads.
    const interp_gpu::Program& program = *sequence.program;
    interp_copies_.insert(interp_copies_.end(), sequence.model_copies.begin(), sequence.model_copies.end());
    const std::size_t copies_bytes = interp_copies_.size() * sizeof(interp_gpu::Copy);
    const std::size_t ops_bytes = program.ops.size() * sizeof(interp_gpu::Op);
    const std::size_t payload_bytes = program.payload.size() * sizeof(std::uint32_t);
    const std::size_t table_bytes = copies_bytes + ops_bytes + payload_bytes + 16u;
    program_index_ = (program_index_ + 1u) % static_cast<unsigned>(program_buffers_.size());
    ProgramBuffer& buffer = program_buffers_[program_index_];
    if (buffer.fence != 0u) {
        wait_for_fence_value(buffer.fence);
    }
    if (!buffer.buffer || buffer.buffer->size < table_bytes) {
        std::uint64_t size = buffer.buffer ? buffer.buffer->size * 2u : 1ull << 20u;
        while (size < table_bytes) {
            size *= 2u;
        }
        buffer.buffer = vk::Context::get().create_buffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                         "interpolation program");
        if (!buffer.buffer) {
            replay_failure_ = 8u;
            return false;
        }
    }
    std::memcpy(buffer.buffer->mapped, interp_copies_.data(), copies_bytes);
    std::memcpy(buffer.buffer->mapped + copies_bytes, program.ops.data(), ops_bytes);
    std::memcpy(buffer.buffer->mapped + copies_bytes + ops_bytes, program.payload.data(), payload_bytes);
    std::memset(buffer.buffer->mapped + copies_bytes + ops_bytes + payload_bytes, 0, 16u);

    sequence_ = SequenceState{};
    sequence_.ready = true;
    sequence_.first = true;
    sequence_.serial = ++sequence_serial_;
    sequence_.overrides = sequence;
    sequence_.program_bytes = static_cast<std::uint32_t>(table_bytes);
    sequence_.copies = static_cast<std::uint32_t>(interp_copies_.size());
    sequence_.ops = static_cast<std::uint32_t>(program.ops.size());
    sequence_.copies_offset = 0u;
    sequence_.ops_offset = static_cast<std::uint32_t>(copies_bytes);
    sequence_.payload_offset = static_cast<std::uint32_t>(copies_bytes + ops_bytes);
    sequence_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++sequence_record_samples_;
    return true;
}

std::uint64_t RendererVk::patched_table(ReplayList& list, std::uint64_t table) {
    if (const auto found = patched_.find(table); found != patched_.end()) {
        return found->second;
    }
    std::uint64_t result = table;
    if (const auto views = run_log_.tables.find(table); views != run_log_.tables.end()) {
        std::array<VkImageView, kMaxTextureMaps> replaced = views->second;
        bool changed = false;
        for (VkImageView& view : replaced) {
            if (const auto stand_in = redirect_.find(view); stand_in != redirect_.end()) {
                view = stand_in->second;
                changed = true;
            }
        }
        if (changed) {
            const VkDevice device = vk::Context::get().device();
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            info.descriptorPool = list.descriptors;
            info.descriptorSetCount = 1;
            info.pSetLayouts = &draw_layout_.sets[1];
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(device, &info, &set) == VK_SUCCESS) {
                std::array<VkDescriptorImageInfo, kMaxTextureMaps> images{};
                for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
                    images[m] = {VK_NULL_HANDLE, replaced[m], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                }
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = set;
                write.dstBinding = 0;
                write.descriptorCount = kMaxTextureMaps;
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                write.pImageInfo = images.data();
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                result = reinterpret_cast<std::uint64_t>(set);
            }
        }
    }
    patched_.emplace(table, result);
    return result;
}

// A logged copy again, from the private scene target into `target` (a stand-in or the sub-frame's display picture).
void RendererVk::replay_copy(const ReplayCopy& copy, vk::Texture& target) {
    if (copy.display) {
        record_conversion_blit(target, copy.width, copy.height, kCopyLayoutDisplay, copy.geometry[0], copy.geometry[1], copy.geometry[2], copy.geometry[3]);
        return;
    }
    record_conversion_blit(target, copy.width, copy.height, copy.params.channels, copy.geometry[0], copy.geometry[1], copy.geometry[2], copy.geometry[3]);
}

// The playing log into `list`'s command buffer, with the sequence's constants: draws read the evaluated blocks, copies
// write stand-ins (later draws of the replay read them), the display copy writes the sub-frame's display picture.
bool RendererVk::record_replay_list(ReplayList& list) {
    const ReplaySequence& sequence = sequence_.overrides;
    const interp_gpu::Layout& layout = sequence.layout;
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    if (list.pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.queueFamilyIndex = context.queue_family();
        vk::check(vkCreateCommandPool(device, &pool_info, nullptr, &list.pool), "replay command pool");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = list.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 2;
        VkCommandBuffer buffers[2]{};
        vk::check(vkAllocateCommandBuffers(device, &alloc, buffers), "replay command buffer");
        list.commands = buffers[0];
        list.commands_after = buffers[1];
        // Patched texture tables and the replay's constants sets (constants_set).
        list.descriptors =
            create_pool(device, kFrameSets,
                        constants_sets(64u, {.images = kFrameSets * 8u, .storage = vk_detail::kPassStorageBuffers,
                                             .storage_images = vk_detail::kPassStorageImages}));
    }
    // The scene scene target's scale (the output scene target's place while the live frame's 2D layer is in it).
    const FrameMap scene_map = output_scene_active_ ? alt_scene_map_ : scene_map_;
    const unsigned scene_samples = output_scene_active_ ? alt_scene_.samples : scene_.samples;
    if (replay_scene_.color && (replay_scene_map_ != scene_map || replay_scene_.samples != scene_samples)) {
        destroy_scene_set(replay_scene_);
    }
    if (!replay_scene_.color) {
        if (!create_scene_set(replay_scene_, scene_map, scene_samples)) {
            replay_failure_ = 5u;
            return false;
        }
        replay_scene_map_ = scene_map;
    }
    const ReplayCopy* display = nullptr;
    for (const ReplayCopy& copy : run_log_.copies) {
        if (copy.display) display = &copy;
    }
    if (display == nullptr) {
        replay_failure_ = 3u;
        return false;
    }
    if (!subframe_display_.texture || subframe_display_.width != display->width || subframe_display_.height != display->height) {
        if (subframe_display_.texture) {
            retire(std::move(subframe_display_.texture));
        }
        subframe_display_.texture = context.create_texture(
            display->width, display->height, 1u, vk::kSceneColorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            true, "sub-frame display picture");
        if (!subframe_display_.texture) {
            replay_failure_ = 4u;
            return false;
        }
        subframe_display_.width = static_cast<std::uint16_t>(display->width);
        subframe_display_.height = static_cast<std::uint16_t>(display->height);
    }
    subframe_display_.prerecorded_movie = run_log_.movie;

    const auto record_start = std::chrono::steady_clock::now();
    vk::check(vkResetCommandPool(device, list.pool, 0), "vkResetCommandPool");
    vk::check(vkResetDescriptorPool(device, list.descriptors, 0), "vkResetDescriptorPool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk::check(vkBeginCommandBuffer(list.commands, &begin), "vkBeginCommandBuffer");

    const VkCommandBuffer saved_commands = commands_;
    const bool saved_pass = scene_pass_open_;
    // The live frame's scene target back in place when its 2D layer is in the output scene target (the log starts at the scene's).
    const bool live_output = output_scene_active_;
    if (live_output) {
        swap_scene_sets();
    }
    commands_ = list.commands;
    scene_pass_open_ = false;
    std::swap(scene_, replay_scene_);
    std::swap(alt_scene_, replay_alt_scene_);
    std::swap(alt_scene_map_, replay_alt_scale_);
    std::swap(alt_scene_storage_, replay_alt_storage_);
    replaying_ = true;
    reset_draw_bindings();
    list.split = false;
    list.upscale = {};
    // Sub-frames write their motion vectors and reactive mask to their own targets.
    const bool temporal = temporal_interp_ && temporal_ != nullptr;
    if (temporal) {
        std::swap(motion_, replay_motion_);
    }
    const VkImage motion_before = motion_.motion ? motion_.motion->image : VK_NULL_HANDLE;
    const bool motion_ready = temporal && ensure_motion_targets();
    if (motion_ready) {
        clear_motion_targets();
        if (motion_.motion->image != motion_before) {
            // Lists of this sequence recorded with the old targets are recorded again.
            for (ReplayList& other : replay_lists_) {
                other.sequence = 0u;
            }
        }
    }
    const FrameMap output_map = temporal ? FrameMap(temporal_output_w_, temporal_output_h_) : output_frame_for_window();
    redirect_.clear();
    patched_.clear();
    // Copy textures the frame read before rewriting them: its snapshots of them.
    for (const auto& [view, snapshot] : run_log_.snapshots) {
        redirect_[view] = snapshot->view;
    }
    replay_list_ = &list;
    replay_constant_sets_.clear();
    list.held.clear();
    bind_scene();
    const gpu::Address output = interp_output_address_;
    for (const ReplayOp& op : run_log_.ops) {
        switch (op.kind) {
        case ReplayKind::Draw: {
            DrawCall call = run_log_.draws[op.index];
            if (call.replay_record >= 0 &&
                static_cast<std::size_t>(call.replay_record) < sequence.record_vs_slot.size()) {
                const auto record = static_cast<std::size_t>(call.replay_record);
                if (const std::int32_t slot = sequence.record_vs_slot[record]; slot >= 0) {
                    call.vs_constants = output + layout.vs_offset(static_cast<std::uint32_t>(slot));
                }
                if (record < sequence.record_palette_slot.size() && sequence.record_palette_slot[record] >= 0) {
                    call.matrix_palette =
                        output + layout.palette_offset(static_cast<std::uint32_t>(sequence.record_palette_slot[record]));
                    call.matrix_rows = output + layout.palette_base();
                }
            }
            if (!redirect_.empty() && call.texture_table.ptr != 0u) {
                call.texture_table.ptr = patched_table(list, call.texture_table.ptr);
            }
            draw(call);
            break;
        }
        case ReplayKind::BindScene:
            bind_scene();
            break;
        case ReplayKind::Clear: {
            const ReplayClear& c = run_log_.clears[op.index];
            clear_scene(c.rgba, c.color, c.alpha, c.depth, c.x, c.y, c.w, c.h, c.format);
            break;
        }
        case ReplayKind::SceneAa:
            record_scene_aa();
            break;
        case ReplayKind::SceneAo:
            record_scene_ao();
            break;
        case ReplayKind::WideBegin:
            record_wide_begin();
            break;
        case ReplayKind::WideEnd:
            record_wide_end();
            break;
        case ReplayKind::Bloom:
            record_game_bloom(run_log_.blooms[op.index]);
            break;
        case ReplayKind::OutputSceneBegin:
            if (!output_scene_active_) {
                if (motion_ready && !list.split && ensure_output_set(output_map, true)) {
                    // The output switch is the temporal upscale, recorded per presented frame (record_subframe_upscale);
                    // the rest of the log goes to the second command buffer.
                    end_scene_pass();
                    list.split = true;
                    list.upscale = {scene_.color, scene_.depth, motion_.motion, motion_.reactive, alt_scene_.color,
                                    scene_map_.width, scene_map_.height, output_map.width, output_map.height};
                    vk::check(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");
                    vk::check(vkBeginCommandBuffer(list.commands_after, &begin), "vkBeginCommandBuffer");
                    commands_ = list.commands_after;
                    swap_scene_sets();
                    bind_scene();
                    clear_scene(0u, false, false, true);
                    break;
                }
                (void)enter_output_scene(output_map);
            }
            break;
        case ReplayKind::SwapSceneSets:
            if (alt_scene_.color) {
                swap_scene_sets();
                bind_scene();
            }
            break;
        case ReplayKind::Copy: {
            const ReplayCopy& copy = run_log_.copies[op.index];
            if (copy.display) {
                replay_copy(copy, *subframe_display_.texture);
                break;
            }
            // One stand-in per destination: the game frame's copy texture keeps what the game drew into it.
            StandIn* stand_in = nullptr;
            for (StandIn& candidate : list.stand_ins) {
                if (candidate.dest_key == copy.dest_key && candidate.texture &&
                    candidate.texture->width == copy.width && candidate.texture->height == copy.height) {
                    stand_in = &candidate;
                }
            }
            if (stand_in == nullptr) {
                gpu::TextureRef texture = context.create_texture(
                    copy.width, copy.height, 1u, vk::kSceneColorFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    true, "sub-frame copy");
                if (!texture) {
                    ++redirect_stats_.fallback_logs;
                    break;
                }
                list.stand_ins.push_back({copy.dest_key, std::move(texture)});
                stand_in = &list.stand_ins.back();
            }
            replay_copy(copy, *stand_in->texture);
            if (copy.dest_view != VK_NULL_HANDLE) {
                redirect_[copy.dest_view] = stand_in->texture->view;
                patched_.clear();
            }
            ++redirect_stats_.redirected;
            break;
        }
        }
    }
    end_scene_pass();
    if (output_scene_active_) {
        swap_scene_sets();
    }
    if (temporal) {
        std::swap(motion_, replay_motion_);
    }
    std::swap(alt_scene_storage_, replay_alt_storage_);
    std::swap(alt_scene_, replay_alt_scene_);
    std::swap(alt_scene_map_, replay_alt_scale_);
    std::swap(scene_, replay_scene_);
    if (live_output) {
        swap_scene_sets();
    }
    replaying_ = false;
    replay_list_ = nullptr;
    vk::check(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");
    commands_ = saved_commands;
    scene_pass_open_ = saved_pass;
    reset_draw_bindings();
    list.sequence = sequence_.serial;
    list.segment = run_log_.segment;
    // Stand-ins unused by this log (a copy changed size) go.
    std::erase_if(list.stand_ins, [this](const StandIn& stand_in) {
        return std::none_of(run_log_.copies.begin(), run_log_.copies.end(), [&](const ReplayCopy& copy) {
            return !copy.display && copy.dest_key == stand_in.dest_key;
        });
    });
    sequence_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++replay_records_;
    return true;
}

// This presented frame's upscale of the replay's scene into its output target, the frame's jitter phase on the replay's
// picture; when it cannot run, the resample in its place.
void RendererVk::record_subframe_upscale(const ReplayList& list, const SubframeTime& time) {
    const ReplayList::Upscale& up = list.upscale;
    const GpuPassScope pass{*this, GpuPass::Upscale};
    dump_temporal_inputs(*up.color, *up.motion, temporal_frame_);
    bool upscaled = false;
    if (temporal_ && up.render_width == temporal_render_w_ && up.render_height == temporal_render_h_ &&
        up.output_width == temporal_output_w_ && up.output_height == temporal_output_h_) {
        vk::TemporalUpscaleInputs in = temporal_inputs(*up.color, *up.depth, *up.motion, *up.reactive, *up.output);
        in.commands = commands_;
        in.render_width = up.render_width;
        in.render_height = up.render_height;
        in.output_width = up.output_width;
        in.output_height = up.output_height;
        in.jitter_x = time.jitter_x;
        in.jitter_y = time.jitter_y;
        const RenderConfig cfg = get_render_config();
        in.frame_ms = static_cast<float>(1000.0 / present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_));
        in.reset = time.reset || !temporal_history_valid_;
        upscaled = temporal_->evaluate(in);
        reset_draw_bindings();
    }
    temporal_history_valid_ = upscaled;
    if (upscaled) {
        return;
    }
    const LogPause pause(*this);
    const bool live_output = output_scene_active_;
    if (live_output) {
        swap_scene_sets();
    }
    std::swap(scene_, replay_scene_);
    std::swap(alt_scene_, replay_alt_scene_);
    std::swap(alt_scene_map_, replay_alt_scale_);
    const FramePassGeometry sampling = frame_pass_geometry(up.output_width, up.output_height, scene_map_);
    record_conversion_blit(*alt_scene_.color, up.output_width, up.output_height, kCopyLayoutDisplay, sampling.src_x, sampling.src_y,
                           sampling.step_x, sampling.step_y);
    end_scene_pass();
    image_barrier(commands_, alt_scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    alt_scene_.color->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    std::swap(alt_scene_, replay_alt_scene_);
    std::swap(alt_scene_map_, replay_alt_scale_);
    std::swap(scene_, replay_scene_);
    if (live_output) {
        swap_scene_sets();
    }
    reset_draw_bindings();
}

bool RendererVk::present_replay_subframe(const SubframeTime& given, const Affine34& camera_t,
                                         const Affine34& camera_prev) {
    if (!sequence_.ready || !run_valid_) {
        replay_failure_ = sequence_.ready ? 2u : 10u;
        return false;
    }
    SubframeTime time = given;
    if (temporal_interp_ && temporal_ && temporal_render_w_ != 0u) {
        // Each presented frame takes the next jitter phase.
        const unsigned phases = std::max(temporal_->jitter_phase_count(temporal_render_w_, temporal_output_w_), 1u);
        temporal_->jitter_offset(temporal_frame_ % phases, phases, time.jitter_x, time.jitter_y);
        ++temporal_frame_;
    }
    // A list of this sequence whose last submission completed; else the one completed longest ago, recorded now.
    const std::uint64_t completed = completed_fence_value();
    ReplayList* list = nullptr;
    for (ReplayList& entry : replay_lists_) {
        if (entry.sequence == sequence_.serial && entry.fence <= completed &&
            true) {
            list = &entry;
            break;
        }
    }
    if (list == nullptr) {
        ReplayList* oldest = &replay_lists_[0];
        for (ReplayList& entry : replay_lists_) {
            if (entry.fence < oldest->fence) oldest = &entry;
        }
        if (oldest->fence > completed) {
            wait_for_fence_value(oldest->fence);
        }
        if (!record_replay_list(*oldest)) {
            return false;
        }
        list = oldest;
    }

    const auto record_start = std::chrono::steady_clock::now();
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    Frame& slot = subframes_[subframe_next_];
    subframe_next_ = (subframe_next_ + 1u) % static_cast<unsigned>(subframes_.size());
    if (slot.pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = context.queue_family();
        vk::check(vkCreateCommandPool(device, &pool_info, nullptr, &slot.pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = slot.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 3;
        VkCommandBuffer buffers[3]{};
        vk::check(vkAllocateCommandBuffers(device, &alloc, buffers), "vkAllocateCommandBuffers");
        slot.uploads = buffers[0];   // the evaluation
        slot.commands = buffers[1];  // the present
        slot.upscale = buffers[2];
        slot.descriptors = create_pool(device, 16u, {.images = 16u, .samplers = kFrameUtilitySamplers});
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vk::check(vkCreateSemaphore(device, &semaphore_info, nullptr, &slot.acquired), "vkCreateSemaphore");
    }
    if (slot.fence != 0u) {
        wait_for_fence_value(slot.fence);
    }
    const auto index = static_cast<std::uint32_t>(&slot - subframes_.data());
    if (subframe_timestamps_ == VK_NULL_HANDLE && timestamps_ != VK_NULL_HANDLE) {
        VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_info.queryCount = static_cast<std::uint32_t>(subframes_.size()) * 2u;
        vk::check(vkCreateQueryPool(device, &query_info, nullptr, &subframe_timestamps_), "vkCreateQueryPool");
    }
    if (subframe_timestamp_pending_[index]) {
        subframe_timestamp_pending_[index] = false;
        std::uint64_t ticks[2]{};
        if (vkGetQueryPoolResults(device, subframe_timestamps_, index * 2u, 2u, sizeof(ticks), ticks,
                                  sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ticks[1] >= ticks[0]) {
            const double ms = static_cast<double>(ticks[1] - ticks[0]) * timestamp_period_ns_ / 1'000'000.0;
            subframe_gpu_ms_ += ms;
            ++subframe_gpu_samples_;
            subframe_gpu_ema_ms_ = subframe_gpu_ema_ms_ == 0.0 ? ms : subframe_gpu_ema_ms_ + (ms - subframe_gpu_ema_ms_) * 0.0625;
        }
    }
    slot.retired.clear();
    vk::check(vkResetCommandPool(device, slot.pool, 0), "vkResetCommandPool");
    vk::check(vkResetDescriptorPool(device, slot.descriptors, 0), "vkResetDescriptorPool");
    for (VkDescriptorPool pool : slot.overflow_descriptors) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    slot.overflow_descriptors.clear();

    Frame* const saved_frame = frame_;
    const VkCommandBuffer saved_commands = commands_;
    const bool saved_pass = scene_pass_open_;
    const std::uint32_t saved_image = swap_image_;
    frame_ = &slot;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(slot.uploads, &begin), "vkBeginCommandBuffer");
    if (subframe_timestamps_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(slot.uploads, subframe_timestamps_, index * 2u, 2u);
        vkCmdWriteTimestamp(slot.uploads, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, subframe_timestamps_, index * 2u);
    }
    record_interp_eval(slot.uploads, time, camera_t, camera_prev);
    vk::check(vkEndCommandBuffer(slot.uploads), "vkEndCommandBuffer");
    if (list->split) {
        vk::check(vkBeginCommandBuffer(slot.upscale, &begin), "vkBeginCommandBuffer");
        commands_ = slot.upscale;
        scene_pass_open_ = false;
        record_subframe_upscale(*list, time);
        vk::check(vkEndCommandBuffer(slot.upscale), "vkEndCommandBuffer");
    }
    vk::check(vkBeginCommandBuffer(slot.commands, &begin), "vkBeginCommandBuffer");
    commands_ = slot.commands;
    scene_pass_open_ = false;
    swap_image_ = UINT32_MAX;
    (void)present(&subframe_display_, subframe_capture_);
    if (subframe_timestamps_ != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(slot.commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, subframe_timestamps_, index * 2u + 1u);
        subframe_timestamp_pending_[index] = true;
    }
    vk::check(vkEndCommandBuffer(slot.commands), "vkEndCommandBuffer");
    const VkCommandBuffer split_buffers[] = {slot.uploads, list->commands, slot.upscale, list->commands_after, slot.commands};
    const VkCommandBuffer whole_buffers[] = {slot.uploads, list->commands, slot.commands};
    const std::span<const VkCommandBuffer> buffers = list->split ? std::span<const VkCommandBuffer>(split_buffers)
                                                                 : std::span<const VkCommandBuffer>(whole_buffers);
    const bool presenting = swap_image_ != UINT32_MAX;
    const std::uint64_t fence = presenting
        ? context.submit_frame(buffers, slot.acquired, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               swapchain_.rendered[swap_image_])
        : context.submit_frame(buffers);
    last_submitted_ = fence;
    slot.fence = fence;
    list->fence = fence;
    if (eval_readback_recorded_) {
        eval_readback_fence_ = fence;
        eval_readback_recorded_ = false;
    }
    program_buffers_[program_index_].fence = fence;
    // The replay reads its log's segment; the present allocates nothing from the rings.
    segments_[run_log_.segment].fence = std::max(segments_[run_log_.segment].fence, fence);
    if (backbuffer_capture_.buffer && backbuffer_capture_.fence == 0u && !backbuffer_capture_pending_path_.empty()) {
        backbuffer_capture_.fence = fence;
    }
    if (presenting) {
        present_swapchain_image();
    }
    frame_ = saved_frame;
    commands_ = saved_commands;
    scene_pass_open_ = saved_pass;
    swap_image_ = saved_image;
    reset_draw_bindings();
    sequence_.first = false;
    subframe_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++subframe_record_samples_;
    return true;
}

bool RendererVk::take_eval_readback(std::vector<std::uint32_t>& words, float& t) {
    if (eval_readback_fence_ == 0u || completed_fence_value() < eval_readback_fence_ || !eval_readback_) {
        return false;
    }
    eval_readback_fence_ = 0u;
    vk::Context::get().invalidate(*eval_readback_);
    words.resize(static_cast<std::size_t>(interp_output_->size / 4u));
    std::memcpy(words.data(), eval_readback_->mapped, words.size() * 4u);
    t = eval_readback_t_;
    return true;
}

void RendererVk::release_replay_resources() noexcept {
    const VkDevice device = vk::Context::get().device();
    if (device == VK_NULL_HANDLE) {
        return;
    }
    for (ReplayList& list : replay_lists_) {
        if (list.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, list.pool, nullptr);
        if (list.descriptors != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, list.descriptors, nullptr);
        list = ReplayList{};
    }
    if (subframe_timestamps_ != VK_NULL_HANDLE) vkDestroyQueryPool(device, subframe_timestamps_, nullptr);
    subframe_timestamps_ = VK_NULL_HANDLE;
    subframe_timestamp_pending_ = {};
    for (Frame& slot : subframes_) {
        slot.retired.clear();
        if (slot.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, slot.pool, nullptr);
        if (slot.descriptors != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, slot.descriptors, nullptr);
        for (VkDescriptorPool pool : slot.overflow_descriptors) vkDestroyDescriptorPool(device, pool, nullptr);
        if (slot.acquired != VK_NULL_HANDLE) vkDestroySemaphore(device, slot.acquired, nullptr);
        slot = Frame{};
    }
    for (Segment& segment : segments_) {
        segment.retired.clear();
        if (segment.descriptors != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, segment.descriptors, nullptr);
        for (VkDescriptorPool pool : segment.overflow) vkDestroyDescriptorPool(device, pool, nullptr);
        segment = Segment{};
    }
    for (VkPipeline pipeline : {eval_copy_pipeline_, eval_pipeline_}) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    }
    eval_copy_pipeline_ = eval_pipeline_ = VK_NULL_HANDLE;
    if (eval_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, eval_layout_, nullptr);
    if (eval_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, eval_set_layout_, nullptr);
    if (eval_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, eval_pool_, nullptr);
    eval_layout_ = VK_NULL_HANDLE;
    eval_set_layout_ = VK_NULL_HANDLE;
    eval_pool_ = VK_NULL_HANDLE;
    program_buffers_ = {};
    interp_output_.reset();
    interp_retired_.clear();
    eval_frames_.reset();
    eval_readback_.reset();
    for (SceneSet* set : {&replay_scene_, &replay_alt_scene_}) {
        for (VkFramebuffer framebuffer : {set->framebuffer, set->resolve_framebuffer}) {
            if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        *set = SceneSet{};
    }
    replay_alt_scale_ = FrameMap{};
    replay_alt_storage_ = false;
    std::swap(motion_, replay_motion_);
    destroy_motion_framebuffers();
    motion_ = MotionTargets{};
    std::swap(motion_, replay_motion_);
    subframe_display_ = DisplayTexture{};
}

}  // namespace galaxy::render
