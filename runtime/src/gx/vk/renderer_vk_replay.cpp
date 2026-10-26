// SPDX-License-Identifier: GPL-3.0-only
// Interpolated sub-frames on Vulkan (renderer_vk.h). Each game frame's draws, clears and copies are logged; a
// sub-frame evaluates the moving constants at its t on the GPU (interp_eval.h), replays the log with them into a
// private EFB (the chunks of the next game frame keep the live one meanwhile) and presents its display copy. The design
// is RendererD3D12's (renderer_d3d12.cpp, begin_replay_record to present_replay_subframe): the replay is recorded
// once per sequence and submitted again for each of its sub-frames.
#include "galaxy/gx/vk/renderer_vk.h"

#include "renderer_vk_internal.h"

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/interp_eval.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/texture_cache.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

namespace galaxy::gx {

using namespace vk_detail;

namespace {

// InterpEvalFrame blocks at uniform-offset alignment: two per sub-frame slot (the copies' dispatch, the ops').
constexpr VkDeviceSize kEvalFrameBlock = 256u;

}  // namespace

bool RendererVk::interpolation_mode() const {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
    return interpolated_frames_per_game_frame(rate) != 0u;
}

// Interp: the upload rings, texture tables and retired textures are segmented per game frame, so a logged frame's
// data lives until its segment comes round again; Normal: per frame slot. A change, or a ring whose frames took over
// half their budget, waits for an idle GPU.
void RendererVk::update_ring_mode() {
    const RingMode mode = interpolation_mode() ? RingMode::Interp : RingMode::Normal;
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
    const auto place = [&](UploadRing& ring, const char* name) {
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
    if (!place(vertex_ring_, "vertex") || !place(index_ring_, "index") || !place(constant_ring_, "constant") ||
        !place(matrix_ring_, "matrix")) {
        throw std::runtime_error("upload ring resize failed");
    }
    // Rebuilt rings may be new buffers: no cached set may name the old ones.
    constants_sets_.clear();
    reset_gx_bindings();
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
    if (prev_valid_ && prev_log_.segment == ring_segment_) {
        prev_valid_ = false;
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
        for (const auto& [key, dest] : efb_copy_dests_) {
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
            snapshot = vk::Context::get().create_texture(source.width, source.height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
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
    viewports.clear();
    clears.clear();
    copies.clear();
    blooms.clear();
    tables.clear();
    surround = 0.0f;
    movie = false;
    stripe_draws.clear();
    stripe_vertices.clear();
    snapshots.clear();
    written.clear();
}

void RendererVk::note_stripe_vertices(std::span<const StripeVertex> vertices) {
    if (!logging() || vertices.empty()) {
        return;
    }
    recording_log_.stripe_draws.push_back({static_cast<std::uint32_t>(recording_log_.draws.size()),
                                          static_cast<std::uint32_t>(recording_log_.stripe_vertices.size()),
                                          static_cast<std::uint32_t>(vertices.size())});
    recording_log_.stripe_vertices.insert(recording_log_.stripe_vertices.end(), vertices.begin(), vertices.end());
}

void RendererVk::begin_replay_record() {
    recording_log_.clear();
    recording_log_.segment = ring_segment_;
    replay_scene_count_ = 0u;
    recording_log_valid_ = true;
    if (start_clear_valid_) {
        start_clear_valid_ = false;
        recording_log_.ops.push_back({ReplayKind::Clear, 0u});
        recording_log_.clears.push_back(start_clear_);
    }
}

void RendererVk::close_replay_log(std::uint32_t xfb_addr) {
    recording_log_valid_ = false;
    // One 3D scene: a frame without one (menus between scenes) or with two is shown as it is.
    const bool valid = replay_scene_count_ == 1u;
    replay_reject_ = replay_scene_count_ == 0u ? 1u : replay_scene_count_ > 1u ? 2u : 0u;
    ++replay_serial_;
    replay_xfb_addr_ = xfb_addr;
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
    // The playing log becomes the one before (the draw pairing reads it).
    prev_valid_ = run_valid_;
    if (run_valid_) {
        std::swap(prev_log_, run_log_);
    }
    std::swap(ready_log_, run_log_);
    run_valid_ = true;
    ready_valid_ = false;
    return true;
}

// The playing log's draws paired with the log before's (RendererD3D12::prepare_vertex_blend, the same rules): a draw
// pairs with the next draw of its matched record (`previous_record[record]`, the record of the scene before) with as
// many vertices, skipping at most three drawn in one frame only. Each pixel-constant record takes the record its paired
// draws used (-2 when they disagree), so its colours interpolate. Paired draws whose vertices the CPU wrote (in
// different places each frame) blend them per sub-frame after the GPU checked the pair (kBlendValidateCs); JPA stripes
// pair particle by particle, their earlier vertices gathered first (kStripeGatherCs).
void RendererVk::prepare_vertex_blend(std::span<const std::int32_t> previous_record) {
    run_blend_vertices_.assign(run_log_.draws.size(), BlendSource{});
    blend_pairs_.clear();
    stripe_gather_.clear();
    blend_validate_pending_ = false;
    std::int32_t ps_records = 0;
    for (const DrawCall& call : run_log_.draws) {
        ps_records = std::max(ps_records, call.ps_record + 1);
    }
    run_ps_previous_.assign(static_cast<std::size_t>(ps_records), -1);
    if (!prev_valid_ || prev_log_.serial + 1u != run_log_.serial || previous_record.empty()) {
        return;
    }
    // Where a draw's vertices are: the vertex ring (0) or the resident decoded-vertex buffer (1), and the byte offset.
    const gpu::Address ring_base = vertex_ring_.gpu_base();
    const std::uint64_t ring_bytes = vertex_ring_.total_bytes();
    const auto locate = [&](gpu::Address address, std::uint32_t bytes, std::uint32_t& offset) {
        if (address >= ring_base && address + bytes <= ring_base + ring_bytes) {
            offset = static_cast<std::uint32_t>(address - ring_base);
            return 0;
        }
        if (resident_vertex_base_ != 0u && address >= resident_vertex_base_ &&
            address + bytes <= resident_vertex_base_ + resident_vertex_bytes_) {
            offset = static_cast<std::uint32_t>(address - resident_vertex_base_);
            return 1;
        }
        return -1;
    };
    // The draws of each previous record, in log order (counting sort).
    std::int32_t records = 0;
    for (const DrawCall& call : prev_log_.draws) {
        records = std::max(records, call.replay_record + 1);
    }
    const std::size_t count = static_cast<std::size_t>(records);
    pairing_scratch_.assign(2u * count + 1u + prev_log_.draws.size(), 0u);
    std::uint32_t* const first = pairing_scratch_.data();  // [records + 1]
    std::uint32_t* const fill = first + count + 1u;         // [records]
    std::uint32_t* const order = fill + count;              // [draws]
    for (const DrawCall& call : prev_log_.draws) {
        if (call.replay_record >= 0) ++first[call.replay_record + 1];
    }
    for (std::size_t r = 0; r < count; ++r) first[r + 1u] += first[r];
    for (std::size_t d = 0; d < prev_log_.draws.size(); ++d) {
        const std::int32_t r = prev_log_.draws[d].replay_record;
        if (r >= 0) order[first[r] + fill[r]++] = static_cast<std::uint32_t>(d);
    }
    // `fill` becomes each previous record's cursor.
    std::fill(fill, fill + count, 0u);
    stripe_of_draw_.assign(run_log_.draws.size(), -1);
    for (std::size_t i = 0; i < run_log_.stripe_draws.size(); ++i) {
        const ReplayLog::StripeDraw& stripe = run_log_.stripe_draws[i];
        if (stripe.draw < stripe_of_draw_.size()) stripe_of_draw_[stripe.draw] = static_cast<std::int32_t>(i);
    }
    stripe_of_prev_draw_.assign(prev_log_.draws.size(), -1);
    for (std::size_t i = 0; i < prev_log_.stripe_draws.size(); ++i) {
        const ReplayLog::StripeDraw& stripe = prev_log_.stripe_draws[i];
        if (stripe.draw < stripe_of_prev_draw_.size()) {
            stripe_of_prev_draw_[stripe.draw] = static_cast<std::int32_t>(i);
        }
    }
    const bool pairs_checked = ensure_blend_pipelines();
    std::vector<std::array<std::uint32_t, 2>> stripe_blends;  // per stripe draw: its draw, its first gathered vertex
    std::vector<std::uint32_t> seen;
    for (std::size_t d = 0; d < run_log_.draws.size(); ++d) {
        const DrawCall& call = run_log_.draws[d];
        const std::int32_t r = call.replay_record;
        if (r < 0 || static_cast<std::size_t>(r) >= previous_record.size()) continue;
        const std::int32_t m = previous_record[static_cast<std::size_t>(r)];
        if (m < 0 || m >= records) continue;
        if (seen.size() <= static_cast<std::size_t>(r)) seen.resize(static_cast<std::size_t>(r) + 1u, 0u);
        ++seen[static_cast<std::size_t>(r)];
        std::uint32_t at = first[m] + fill[m];
        const std::uint32_t end = std::min(first[m + 1], at + 4u);
        const std::int32_t stripe = stripe_of_draw_[d];
        if (stripe >= 0) {
            // A stripe pairs with its record's next stripe draw whatever the particle count, then particle by particle.
            while (at < end && stripe_of_prev_draw_[order[at]] < 0) {
                ++at;
            }
        } else {
            while (at < end && prev_log_.draws[order[at]].vertex_view.SizeInBytes != call.vertex_view.SizeInBytes) {
                ++at;
            }
        }
        if (at >= end) continue;
        fill[m] = at + 1u - first[m];
        const DrawCall& before = prev_log_.draws[order[at]];
        if (call.ps_record >= 0 && before.ps_record >= 0) {
            std::int32_t& pair = run_ps_previous_[static_cast<std::size_t>(call.ps_record)];
            pair = pair == -1 || pair == before.ps_record ? before.ps_record : -2;
        }
        std::uint32_t current_offset = 0u, previous_offset = 0u;
        const int current_buffer =
            locate(call.vertex_view.BufferLocation, call.vertex_view.SizeInBytes, current_offset);
        const int previous_buffer =
            locate(before.vertex_view.BufferLocation, before.vertex_view.SizeInBytes, previous_offset);
        // A strip of the same length slides along itself (index pairing, as any pair); one that grew or shrank pairs
        // particle by particle.
        if (stripe >= 0 && before.vertex_view.SizeInBytes != call.vertex_view.SizeInBytes) {
            const std::uint32_t vertices = call.vertex_view.StrideInBytes == 144u
                ? call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes : 0u;
            const std::uint32_t previous_vertices = before.vertex_view.StrideInBytes == 144u
                ? before.vertex_view.SizeInBytes / before.vertex_view.StrideInBytes : 0u;
            if (vertices == 0u || current_buffer < 0 || previous_buffer < 0 ||
                stripe_gather_.size() + vertices > (1u << 20u)) {
                continue;
            }
            // Previous particles by (particle, pass, side, lifetime); a match continues only if its age moved on by at
            // most one frame.
            const ReplayLog::StripeDraw& prev_draw =
                prev_log_.stripe_draws[static_cast<std::size_t>(stripe_of_prev_draw_[order[at]])];
            const auto key_of = [](const StripeVertex& v) {
                return (static_cast<std::uint64_t>(v.particle) << 32u) |
                       (static_cast<std::uint64_t>(v.strip) << 24u) | (static_cast<std::uint64_t>(v.side) << 16u) |
                       (v.age_lifetime & 0xFFFFu);
            };
            stripe_keys_.clear();
            for (std::uint32_t i = 0; i < prev_draw.count; ++i) {
                const StripeVertex& v = prev_log_.stripe_vertices[prev_draw.first + i];
                if (v.vertex < previous_vertices) stripe_keys_.emplace_back(key_of(v), prev_draw.first + i);
            }
            std::sort(stripe_keys_.begin(), stripe_keys_.end());
            const auto base = static_cast<std::uint32_t>(stripe_gather_.size());
            for (std::uint32_t i = 0; i < vertices; ++i) {
                stripe_gather_.push_back({current_offset + i * 144u, static_cast<std::uint32_t>(current_buffer)});
            }
            stripe_matched_.assign(vertices, -1);
            const ReplayLog::StripeDraw& draw = run_log_.stripe_draws[static_cast<std::size_t>(stripe)];
            for (std::uint32_t i = 0; i < draw.count; ++i) {
                const StripeVertex& v = run_log_.stripe_vertices[draw.first + i];
                if (v.vertex >= vertices) continue;
                const std::uint64_t key = key_of(v);
                const auto found = std::lower_bound(stripe_keys_.begin(), stripe_keys_.end(),
                                                    std::pair<std::uint64_t, std::uint32_t>{key, 0u});
                if (found == stripe_keys_.end() || found->first != key) continue;
                const StripeVertex& was = prev_log_.stripe_vertices[found->second];
                const auto steps = static_cast<std::uint16_t>((v.age_lifetime >> 16u) - (was.age_lifetime >> 16u));
                if (steps > 1u) continue;
                stripe_gather_[base + v.vertex] = {previous_offset + was.vertex * 144u,
                                                   static_cast<std::uint32_t>(previous_buffer)};
                stripe_matched_[v.vertex] = static_cast<std::int32_t>(was.vertex);
            }
            // A new particle grows out of where its nearest surviving neighbour (same side of the strip) was.
            for (std::uint32_t i = 0; i < vertices; ++i) {
                if (stripe_matched_[i] >= 0) continue;
                for (std::uint32_t step = 2u; step <= 16u; step += 2u) {
                    const std::int64_t below = static_cast<std::int64_t>(i) - step;
                    const std::uint32_t above = i + step;
                    std::int32_t from = -1;
                    if (above < vertices && stripe_matched_[above] >= 0) {
                        from = stripe_matched_[above];
                    } else if (below >= 0 && stripe_matched_[static_cast<std::size_t>(below)] >= 0) {
                        from = stripe_matched_[static_cast<std::size_t>(below)];
                    }
                    if (from >= 0) {
                        stripe_gather_[base + i] = {previous_offset + static_cast<std::uint32_t>(from) * 144u,
                                                    static_cast<std::uint32_t>(previous_buffer)};
                        break;
                    }
                }
            }
            stripe_blends.push_back({static_cast<std::uint32_t>(d), base});
            continue;
        }
        if (pairs_checked && before.vertex_view.BufferLocation != call.vertex_view.BufferLocation &&
            before.vertex_view.StrideInBytes == call.vertex_view.StrideInBytes && call.vertex_view.SizeInBytes != 0u &&
            current_buffer >= 0 && previous_buffer >= 0 && blend_pairs_.size() < kStripeVerdict) {
            const auto slot = static_cast<std::uint32_t>(blend_pairs_.size());
            blend_pairs_.push_back({current_offset, previous_offset,
                                    call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes,
                                    static_cast<std::uint32_t>(current_buffer) |
                                        (static_cast<std::uint32_t>(previous_buffer) << 1u)});
            run_blend_vertices_[d] = {before.vertex_view.BufferLocation, slot};
        }
    }
    if (!stripe_gather_.empty()) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(stripe_gather_.size()) * 144u;
        if (!stripe_earlier_ || stripe_earlier_->size < bytes) {
            if (stripe_earlier_) {
                interp_retired_.emplace_back(std::move(stripe_earlier_), last_submitted_);
            }
            std::uint64_t size = 1ull << 20u;
            while (size < bytes) {
                size *= 2u;
            }
            stripe_earlier_ = vk::Context::get().create_buffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                               "earlier stripe vertices");
        }
        if (stripe_earlier_) {
            for (const auto& [draw, base] : stripe_blends) {
                run_blend_vertices_[draw] = {stripe_earlier_->address + static_cast<std::uint64_t>(base) * 144u,
                                             kStripeVerdict};
            }
        } else {
            stripe_gather_.clear();
        }
    }
    blend_validate_pending_ = !blend_pairs_.empty() || !stripe_gather_.empty();
}

bool RendererVk::ensure_blend_pipelines() {
    if (blend_validate_pipeline_ != VK_NULL_HANDLE) {
        return true;
    }
    if (blend_failed_) {
        return false;
    }
    blend_failed_ = true;
    const VkDevice device = vk::Context::get().device();
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (std::uint32_t i = 0; i < 4u; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 4;
    layout_info.pBindings = bindings;
    vk::check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &blend_set_layout_), "blend set layout");
    const VkPushConstantRange limits{VK_SHADER_STAGE_COMPUTE_BIT, 0, 3u * sizeof(std::uint32_t)};
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &blend_set_layout_;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &limits;
    vk::check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &blend_layout_), "blend layout");
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4u * 2u * 3u};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 2u * 3u;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &size;
    vk::check(vkCreateDescriptorPool(device, &pool_info, nullptr, &blend_pool_), "blend descriptor pool");
    for (const auto& [source, pipeline] : {std::pair{kBlendValidateCs, &blend_validate_pipeline_},
                                           std::pair{kStripeGatherCs, &stripe_gather_pipeline_}}) {
        std::string error;
        const std::vector<std::uint32_t> spirv = vk::compile_hlsl(source, vk::ShaderStage::Compute, "main", {}, error);
        if (spirv.empty()) {
            std::cerr << "[interp-blend] " << error << '\n';
            return false;
        }
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = spirv.size() * sizeof(std::uint32_t);
        module_info.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        vk::check(vkCreateShaderModule(device, &module_info, nullptr, &module), "blend shader module");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = blend_layout_;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, pipeline);
        vkDestroyShaderModule(device, module, nullptr);
        vk::check(result, "blend pipeline");
    }
    blend_failed_ = false;
    return true;
}

// The first sub-frame of a blending replay, before the evaluation: the earlier stripe vertices gathered from the log
// before, then every pair checked (the verdicts its draws read).
void RendererVk::record_blend_validation(VkCommandBuffer commands) {
    if (!blend_validate_pending_ || blend_validate_pipeline_ == VK_NULL_HANDLE) {
        return;
    }
    blend_validate_pending_ = false;
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    ProgramBuffer& program = program_buffers_[program_index_];
    const VkBuffer ring = reinterpret_cast<VkBuffer>(vertex_ring_.resource());
    const std::uint64_t ring_bytes = vertex_ring_.total_bytes();
    const bool resident = resident_vertex_base_ != 0u && resident_vertex_bytes_ >= 12u;
    const VkBuffer resident_buffer = resident ? context.resolve(resident_vertex_base_).buffer : ring;
    const std::uint64_t resident_bytes = resident ? resident_vertex_bytes_ : ring_bytes;
    // A table of the sequence's (rewritten only after the sub-frames that read it completed: the program fence).
    const auto table = [&](std::shared_ptr<vk::Buffer>& buffer, const void* data, std::size_t bytes, const char* name) {
        if (!buffer || buffer->size < bytes) {
            std::uint64_t size = 64u << 10u;
            while (size < bytes) {
                size *= 2u;
            }
            buffer = context.create_buffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, name);
        }
        if (buffer) {
            std::memcpy(buffer->mapped, data, bytes);
        }
        return static_cast<bool>(buffer);
    };
    const auto set_for = [&](VkDescriptorSet& set, std::array<VkBuffer, 4>& bound, const std::array<VkBuffer, 4>& buffers) {
        if (set == VK_NULL_HANDLE) {
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            info.descriptorPool = blend_pool_;
            info.descriptorSetCount = 1;
            info.pSetLayouts = &blend_set_layout_;
            vk::check(vkAllocateDescriptorSets(device, &info, &set), "blend set");
        }
        if (bound != buffers) {
            VkDescriptorBufferInfo infos[4]{};
            VkWriteDescriptorSet writes[4]{};
            for (unsigned i = 0; i < 4u; ++i) {
                infos[i] = {buffers[i], 0, VK_WHOLE_SIZE};
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = &infos[i];
            }
            vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
            bound = buffers;
        }
        return set;
    };
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    // The outputs are rewritten after the sub-frames before read them (queue order).
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 0, nullptr);
    if (!stripe_gather_.empty() && stripe_earlier_ &&
        table(program.sources, stripe_gather_.data(), stripe_gather_.size() * sizeof(stripe_gather_[0]),
              "stripe sources")) {
        const VkDescriptorSet set =
            set_for(program.gather_set, program.gather_bound,
                    {ring, resident_buffer, program.sources->buffer, stripe_earlier_->buffer});
        const std::uint32_t limits[3]{
            static_cast<std::uint32_t>(std::min<std::uint64_t>(ring_bytes - 4u, 0xFFFFFFF0u)),
            static_cast<std::uint32_t>(std::min<std::uint64_t>(resident_bytes - 4u, 0xFFFFFFF0u)),
            static_cast<std::uint32_t>(stripe_gather_.size())};
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, stripe_gather_pipeline_);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, blend_layout_, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(commands, blend_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(limits), limits);
        const std::uint64_t threads = static_cast<std::uint64_t>(stripe_gather_.size()) * 36u;
        vkCmdDispatch(commands, static_cast<std::uint32_t>((threads + 63u) / 64u), 1u, 1u);
    }
    if (!blend_pairs_.empty() &&
        table(program.pairs, blend_pairs_.data(), blend_pairs_.size() * sizeof(blend_pairs_[0]), "blend pairs")) {
        const VkDescriptorSet set = set_for(program.validate_set, program.validate_bound,
                                            {ring, resident_buffer, program.pairs->buffer, blend_verdicts_->buffer});
        const std::uint32_t limits[3]{
            static_cast<std::uint32_t>(std::min<std::uint64_t>(ring_bytes - 12u, 0xFFFFFFF0u)),
            static_cast<std::uint32_t>(std::min<std::uint64_t>(resident_bytes - 12u, 0xFFFFFFF0u)), 0u};
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, blend_validate_pipeline_);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, blend_layout_, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(commands, blend_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(limits), limits);
        vkCmdDispatch(commands, static_cast<std::uint32_t>(blend_pairs_.size()), 1u, 1u);
    }
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
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
    VkDescriptorSetLayoutBinding bindings[6]{};
    for (std::uint32_t i = 0; i < 6u; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 5u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 6;
    layout_info.pBindings = bindings;
    vk::check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &eval_set_layout_), "eval set layout");
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &eval_set_layout_;
    vk::check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &eval_layout_), "eval layout");
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5u * 3u},
                                          {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 3u}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 3;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = sizes;
    vk::check(vkCreateDescriptorPool(device, &pool_info, nullptr, &eval_pool_), "eval descriptor pool");
    for (const auto& [entry, pipeline] : {std::pair{"copy_main", &eval_copy_pipeline_},
                                          std::pair{"eval_main", &eval_pipeline_}}) {
        std::string error;
        const std::vector<std::uint32_t> spirv =
            vk::compile_hlsl(kInterpEvalCs, vk::ShaderStage::Compute, entry, {}, error);
        if (spirv.empty()) {
            std::cerr << "[interp-eval] " << entry << ": " << error << '\n';
            return false;
        }
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = spirv.size() * sizeof(std::uint32_t);
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
    if (!blend_verdicts_ || !eval_frames_) {
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
    const std::array<VkBuffer, 5> buffers = {program.buffer->buffer, ring_buffer(constant_ring_),
                                             ring_buffer(matrix_ring_), interp_output_->buffer,
                                             blend_verdicts_->buffer};
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
    VkDescriptorBufferInfo infos[6]{};
    for (unsigned i = 0; i < 5u; ++i) {
        infos[i] = {buffers[i], 0, VK_WHOLE_SIZE};
    }
    infos[5] = {eval_frames_->buffer, 0, sizeof(InterpEvalFrame)};
    VkWriteDescriptorSet writes[6]{};
    for (unsigned i = 0; i < 6u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = program.set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 5u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
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
    frame.frame_slot = kFrameValuesElement;
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
    interp_copied_.assign(static_cast<std::size_t>(layout.vs_count) + layout.ps_count, 0u);
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
        if (call.ps_record >= 0 && static_cast<std::size_t>(call.ps_record) < sequence.ps_slot.size()) {
            const std::int32_t slot = sequence.ps_slot[static_cast<std::size_t>(call.ps_record)];
            const std::size_t seen = layout.vs_count + static_cast<std::size_t>(slot);
            if (slot >= 0 && interp_copied_[seen] == 0u) {
                interp_copied_[seen] = 1u;
                if (!copy_block(call.ps_constants, interp_gpu::kPsBytes,
                                layout.ps_offset(static_cast<std::uint32_t>(slot)))) {
                    replay_failure_ = 9u;
                    return false;
                }
            }
        }
    }
    for (std::uint32_t q = 0; q < layout.palette_count && q < sequence.palette_source.size(); ++q) {
        const std::uint64_t source = static_cast<std::uint64_t>(sequence.palette_source[q]) * 16u;
        if (source + interp_gpu::kPaletteBytes > matrices_bytes) {
            replay_failure_ = 9u;
            return false;
        }
        interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices),
                                  static_cast<std::uint32_t>(source), layout.palette_offset(q),
                                  interp_gpu::kPaletteWords});
        if (q < layout.prev_palette_count) {
            interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices),
                                      static_cast<std::uint32_t>(source), layout.prev_palette_offset(q),
                                      interp_gpu::kPrevPaletteWords});
        }
    }

    // The program (16 bytes of tail room for Load4), in a buffer no sub-frame in flight reads.
    const interp_gpu::Program& program = *sequence.program;
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
            info.pSetLayouts = &gx_layout_.sets[1];
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

// A logged copy again, from the private EFB into `target` (a stand-in or the sub-frame's XFB).
void RendererVk::replay_copy(const ReplayCopy& copy, vk::Texture& target) {
    if (copy.exact) {
        end_efb_pass();
        const VkCommandBuffer commands = commands_;
        image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT);
        image_barrier(commands, target.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffset = {static_cast<std::int32_t>(copy.box_left), static_cast<std::int32_t>(copy.box_top), 0};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {copy.width, copy.height, 1};
        vkCmdCopyImage(commands, efb_.color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(commands, target.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
        image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        target.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        reset_gx_bindings();
        bind_efb(nullptr);
        return;
    }
    if (copy.display) {
        // Display copies never convert to intensity; gamma and the copy filter apply.
        record_conversion_blit(target, copy.width, copy.height, copy.params, false, false, true, 15u, copy.geometry[0],
                               copy.geometry[1], copy.geometry[2], copy.geometry[3], copy.geometry[4]);
        return;
    }
    // Intensity copies to RA4/RA8 (IA4/IA8) keep the source alpha; pure-I formats replicate Y into alpha.
    const bool keep_alpha = copy.params.target_format == 2u || copy.params.target_format == 3u;
    record_conversion_blit(target, copy.width, copy.height, copy.params, copy.depth, copy.params.intensity, keep_alpha,
                           copy.params.target_format, copy.geometry[0], copy.geometry[1], copy.geometry[2],
                           copy.geometry[3], copy.geometry[4]);
}

// The playing log into `list`'s command buffer, with the sequence's constants: draws read the evaluated blocks, copies
// write stand-ins (later draws of the replay read them), the display copy writes the sub-frame's XFB.
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
        alloc.commandBufferCount = 1;
        vk::check(vkAllocateCommandBuffers(device, &alloc, &list.commands), "replay command buffer");
        // Patched texture tables and the replay's constants sets (constants_set).
        list.descriptors =
            create_pool(device, kFrameSets,
                        constants_sets(64u, {.images = kFrameSets * 8u, .storage_images = vk_detail::kPassStorageImages}));
    }
    // The scene EFB's scale (the output EFB's place while the live frame's 2D layer is in it).
    const unsigned scene_scale = output_efb_active_ ? alt_efb_scale_ : efb_scale_;
    const unsigned scene_samples = output_efb_active_ ? alt_efb_.samples : efb_.samples;
    if (replay_efb_.color && (replay_efb_scale_ != scene_scale || replay_efb_.samples != scene_samples)) {
        destroy_efb_set(replay_efb_);
    }
    if (!replay_efb_.color) {
        if (!create_efb_set(replay_efb_, scene_scale, scene_samples)) {
            replay_failure_ = 5u;
            return false;
        }
        replay_efb_scale_ = scene_scale;
    }
    const ReplayCopy* display = nullptr;
    for (const ReplayCopy& copy : run_log_.copies) {
        if (copy.display) display = &copy;
    }
    if (display == nullptr) {
        replay_failure_ = 3u;
        return false;
    }
    if (!subframe_xfb_.texture || subframe_xfb_.width != display->width || subframe_xfb_.height != display->height) {
        if (subframe_xfb_.texture) {
            retire(std::move(subframe_xfb_.texture));
        }
        subframe_xfb_.texture = context.create_texture(
            display->width, display->height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            true, "sub-frame XFB");
        if (!subframe_xfb_.texture) {
            replay_failure_ = 4u;
            return false;
        }
        subframe_xfb_.width = static_cast<std::uint16_t>(display->width);
        subframe_xfb_.height = static_cast<std::uint16_t>(display->height);
    }
    subframe_xfb_.safety_surround = run_log_.surround;
    subframe_xfb_.prerecorded_movie = run_log_.movie;

    const auto record_start = std::chrono::steady_clock::now();
    vk::check(vkResetCommandPool(device, list.pool, 0), "vkResetCommandPool");
    vk::check(vkResetDescriptorPool(device, list.descriptors, 0), "vkResetDescriptorPool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vk::check(vkBeginCommandBuffer(list.commands, &begin), "vkBeginCommandBuffer");

    const VkCommandBuffer saved_commands = commands_;
    const bool saved_pass = efb_pass_open_;
    // The live frame's EFB back in place when its 2D layer is in the output EFB (the log starts at the scene's).
    const bool live_output = output_efb_active_;
    if (live_output) {
        swap_efb_sets();
    }
    commands_ = list.commands;
    efb_pass_open_ = false;
    std::swap(efb_, replay_efb_);
    std::swap(alt_efb_, replay_alt_efb_);
    std::swap(alt_efb_scale_, replay_alt_scale_);
    replaying_ = true;
    reset_gx_bindings();
    redirect_.clear();
    patched_.clear();
    // Copy textures the frame read before rewriting them: its snapshots of them.
    for (const auto& [view, snapshot] : run_log_.snapshots) {
        redirect_[view] = snapshot->view;
    }
    replay_list_ = &list;
    replay_constant_sets_.clear();
    list.held.clear();
    bind_efb(nullptr);
    const gpu::Address output = interp_output_address_;
    // The log before's vertices are gone once its segment came round (a later game frame records there).
    const bool blend = sequence.vertex_blend && prev_valid_ && (!blend_pairs_.empty() || !stripe_gather_.empty()) &&
                       run_blend_vertices_.size() == run_log_.draws.size();
    std::uint64_t blended = 0u;
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
                    call.matrix_palette = output + layout.palette_base();
                }
            }
            if (call.ps_record >= 0 && static_cast<std::size_t>(call.ps_record) < sequence.ps_slot.size()) {
                if (const std::int32_t slot = sequence.ps_slot[static_cast<std::size_t>(call.ps_record)]; slot >= 0) {
                    call.ps_constants = output + layout.ps_offset(static_cast<std::uint32_t>(slot));
                }
            }
            if (blend && run_blend_vertices_[op.index].vertices != 0u) {
                call.blend_vertices = run_blend_vertices_[op.index].vertices;
                call.blend_slot = run_blend_vertices_[op.index].slot;
                ++blended;
            }
            if (!redirect_.empty() && call.texture_table.ptr != 0u) {
                call.texture_table.ptr = patched_table(list, call.texture_table.ptr);
            }
            draw(call);
            break;
        }
        case ReplayKind::BindEfb: {
            const ReplayViewport& viewport = run_log_.viewports[op.index];
            bind_efb(viewport.valid ? viewport.values : nullptr);
            break;
        }
        case ReplayKind::Clear: {
            const ReplayClear& c = run_log_.clears[op.index];
            clear_efb(c.ar, c.gb, c.z24, c.color, c.alpha, c.depth, c.x, c.y, c.w, c.h, c.format);
            break;
        }
        case ReplayKind::SceneAa:
            record_scene_aa();
            break;
        case ReplayKind::Bloom:
            record_game_bloom(run_log_.blooms[op.index]);
            break;
        case ReplayKind::OutputEfbBegin:
            if (!output_efb_active_) {
                (void)enter_output_efb(op.index);
            }
            break;
        case ReplayKind::SwapEfbSets:
            if (alt_efb_.color) {
                swap_efb_sets();
                bind_efb(nullptr);
            }
            break;
        case ReplayKind::Copy: {
            const ReplayCopy& copy = run_log_.copies[op.index];
            if (copy.display) {
                replay_copy(copy, *subframe_xfb_.texture);
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
                    copy.width, copy.height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
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
    end_efb_pass();
    if (output_efb_active_) {
        swap_efb_sets();
    }
    std::swap(alt_efb_, replay_alt_efb_);
    std::swap(alt_efb_scale_, replay_alt_scale_);
    std::swap(efb_, replay_efb_);
    if (live_output) {
        swap_efb_sets();
    }
    replaying_ = false;
    replay_list_ = nullptr;
    commands_ = saved_commands;
    efb_pass_open_ = saved_pass;
    reset_gx_bindings();
    vk::check(vkEndCommandBuffer(list.commands), "vkEndCommandBuffer");
    list.sequence = sequence_.serial;
    list.segment = run_log_.segment;
    list.blend = blend && blended != 0u;
    list.blend_segment = prev_log_.segment;
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

bool RendererVk::present_replay_subframe(const SubframeTime& time, const Affine34& camera_t,
                                         const Affine34& camera_prev) {
    if (!sequence_.ready || !run_valid_) {
        replay_failure_ = sequence_.ready ? 2u : 10u;
        return false;
    }
    // A list of this sequence whose last submission completed; else the one completed longest ago, recorded now.
    const std::uint64_t completed = completed_fence_value();
    ReplayList* list = nullptr;
    for (ReplayList& entry : replay_lists_) {
        if (entry.sequence == sequence_.serial && entry.fence <= completed &&
            (!entry.blend || (prev_valid_ && entry.blend_segment == prev_log_.segment))) {
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
        alloc.commandBufferCount = 2;
        VkCommandBuffer buffers[2]{};
        vk::check(vkAllocateCommandBuffers(device, &alloc, buffers), "vkAllocateCommandBuffers");
        slot.uploads = buffers[0];   // the evaluation
        slot.commands = buffers[1];  // the present
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
    const bool saved_pass = efb_pass_open_;
    const std::uint32_t saved_image = swap_image_;
    frame_ = &slot;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(slot.uploads, &begin), "vkBeginCommandBuffer");
    if (subframe_timestamps_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(slot.uploads, subframe_timestamps_, index * 2u, 2u);
        vkCmdWriteTimestamp(slot.uploads, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, subframe_timestamps_, index * 2u);
    }
    if (list->blend && !sequence_.validated) {
        record_blend_validation(slot.uploads);
        sequence_.validated = true;
    }
    record_interp_eval(slot.uploads, time, camera_t, camera_prev);
    vk::check(vkEndCommandBuffer(slot.uploads), "vkEndCommandBuffer");
    vk::check(vkBeginCommandBuffer(slot.commands, &begin), "vkBeginCommandBuffer");
    commands_ = slot.commands;
    efb_pass_open_ = false;
    swap_image_ = UINT32_MAX;
    (void)present(&subframe_xfb_, subframe_capture_);
    if (subframe_timestamps_ != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(slot.commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, subframe_timestamps_, index * 2u + 1u);
        subframe_timestamp_pending_[index] = true;
    }
    vk::check(vkEndCommandBuffer(slot.commands), "vkEndCommandBuffer");
    const VkCommandBuffer buffers[] = {slot.uploads, list->commands, slot.commands};
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
    if (list->blend) {
        segments_[list->blend_segment].fence = std::max(segments_[list->blend_segment].fence, fence);
    }
    if (backbuffer_capture_.buffer && backbuffer_capture_.fence == 0u && !backbuffer_capture_pending_path_.empty()) {
        backbuffer_capture_.fence = fence;
    }
    if (presenting) {
        present_swapchain_image();
    }
    frame_ = saved_frame;
    commands_ = saved_commands;
    efb_pass_open_ = saved_pass;
    swap_image_ = saved_image;
    reset_gx_bindings();
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
    for (VkPipeline pipeline : {eval_copy_pipeline_, eval_pipeline_, blend_validate_pipeline_, stripe_gather_pipeline_}) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
    }
    eval_copy_pipeline_ = eval_pipeline_ = blend_validate_pipeline_ = stripe_gather_pipeline_ = VK_NULL_HANDLE;
    if (blend_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, blend_layout_, nullptr);
    if (blend_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, blend_set_layout_, nullptr);
    if (blend_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, blend_pool_, nullptr);
    blend_layout_ = VK_NULL_HANDLE;
    blend_set_layout_ = VK_NULL_HANDLE;
    blend_pool_ = VK_NULL_HANDLE;
    stripe_earlier_.reset();
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
    for (EfbSet* set : {&replay_efb_, &replay_alt_efb_}) {
        for (VkFramebuffer framebuffer : {set->framebuffer, set->resolve_framebuffer}) {
            if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        *set = EfbSet{};
    }
    replay_alt_scale_ = 0u;
    subframe_xfb_ = XfbTexture{};
}

}  // namespace galaxy::gx
