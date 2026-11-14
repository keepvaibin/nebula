// SPDX-License-Identifier: GPL-3.0-only
// RendererMtl: where the renderer's parts (fx_mtl.h) meet the frame: the game's bloom, ambient occlusion, the scene's
// anti-aliasing and the wide scene with its camera. As the same functions of vk/renderer_vk_frame.cpp.
#include "renderer_mtl_internal.h"

#include "galaxy/render/game_bloom_shape.h"
#include "galaxy/render/texture_store.h"

#include <nebula_frame_list.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>

namespace galaxy::render {

using namespace mtl_detail;

// SMAA or FXAA of the 3D scene in the scene target, before the 2D layer draws.
void RendererMtl::record_scene_aa() {
    // A temporal upscaler antialiases the jittered scene itself.
    if (!post_.ready() || scene_temporal_ || !scene_.color || scene_.wide || target_pass_active_ ||
        post_.settings().aa == PostProcessSettings::Aa::kOff) {
        return;
    }
    const GpuPassScope scope{*this, GpuPass::SceneAa};
    // The picture the display copy takes, against its size on screen, decides AUTO.
    const FrameMap shown = output_frame_for_window();
    end_scene_pass();
    const mtl::Texture& color = *scene_.color;
    const AaRun ran = post_.prepare_scene_aa(commands_, color, color.width, color.height, scene_map_.width, scene_map_.height,
                                             std::max(shown.width, 1u), std::max(shown.height, 1u));
    begin_scene_pass();
    post_.finish_scene_aa(pass_, ran, color.width, color.height, scene_.samples);
    reset_draw_bindings();
}

// The occlusion of the opaque scene: the scene pass ends for its compute passes, which read the depth, and the
// multiply draws in it again.
void RendererMtl::record_scene_ao() {
    if (!ao_.ready() || !scene_.color || !scene_.depth || target_pass_active_) {
        return;
    }
    const GpuPassScope scope{*this, GpuPass::SceneAo};
    end_scene_pass();
    settle_depth_clear();
    resolve_scene_depth();
    const bool made = ao_.prepare(commands_, *scene_.depth, scene_ao_view_, get_render_config().ambient_occlusion_ssao,
                                  scene_ao_objects_, matrix_ring_);
    gpu_pass(GpuPass::AoApply);
    begin_scene_pass();
    if (made) {
        ao_.finish(pass_, scene_.wide ? draw_layout_.wide_color : draw_layout_.scene_color, scene_.samples);
    }
    reset_draw_bindings();
}

// The game's bloom at this point of the stream: the scene pass ends for the passes that copy or read the scene, and
// the composite is drawn in it again.
void RendererMtl::game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b) {
    if (!bloom_.ready() || !scene_.color || target_pass_active_) {
        return;
    }
    const float strength = static_cast<float>(std::min(get_render_config().bloom, kMaxBloom)) / 100.0f;
    if (kind == NEBULA_PASS_BLOOM_SIMPLE && scene_.samples <= 1u &&
        bloom::adds_nothing(a & 0xFFu, (a >> 8u) & 0xFFu, (a >> 16u) & 0xFFu, a >> 24u, strength)) {
        return;  // a simple bloom that adds nothing leaves the frame, and its pass open
    }
    const GpuPassScope scope{*this, GpuPass::Bloom};
    end_scene_pass();
    mtl::GameBloom::Inputs in{};
    in.color = scene_.color.get();
    in.scale = static_cast<float>(scene_map_.height / kAuthoredLines);
    in.frame_width = std::min(scene_map_.width, scene_.color->width);
    in.frame_height = std::min(scene_map_.height, scene_.color->height);
    if (kind == NEBULA_PASS_BLOOM_HOLD) {
        if (scene_.samples <= 1u) {
            // The scene and the bloom's texture trade places instead of the scene being copied (the wide scene's too: a
            // pass takes its target's format from the texture). The game draws the bloom's objects on black into the new
            // target, over the same depth.
            bloom_.keep_picture(swap_scene_color(bloom_.take_kept_image()));
            clear_scene(0u, true, true, false);
            return;
        }
        static_cast<void>(bloom_.keep(commands_, in));
    } else {
        mtl::GameBloom::Params p{};
        p.threshold = a & 0xFFu;
        p.intensity = a >> 24u;
        if (kind == NEBULA_PASS_BLOOM_NORMAL) {
            p.kind = mtl::GameBloom::Kind::Normal;
            p.tap1 = (a >> 8u) & 0xFFu;
            p.tap2 = (a >> 16u) & 0xFFu;
            p.ring_x = std::bit_cast<float>(b);
        } else {
            p.kind = mtl::GameBloom::Kind::Simple;
            p.channel = (a >> 8u) & 0xFFu;
            p.tap1 = (a >> 16u) & 0xFFu;
            p.ring_y = std::bit_cast<float>(b);
        }
        if (!(p.ring_x > 0.0f && p.ring_x < 16.0f) || !(p.ring_y > 0.0f && p.ring_y < 16.0f)) {
            p.ring_x = p.ring_y = 1.0f;
        }
        p.strength = strength;
        const bool whole = in.frame_width == scene_.color->width && in.frame_height == scene_.color->height;
        const mtl::Format color_format = scene_.wide ? draw_layout_.wide_color : draw_layout_.scene_color;
        // As RendererVk::game_bloom: the simple bloom's source is the frame, traded with the scene instead of copied where
        // the composite writes the whole target from it; with nothing to draw, the frame is drawn back from its picture.
        gpu::TextureRef source;
        if (p.kind == mtl::GameBloom::Kind::Simple && whole && scene_.samples <= 1u &&
            bloom_.prepare_draw_back(color_format, scene_.samples)) {
            source = swap_scene_color(bloom_.take_source_image());
            bloom_.give_source(source);
        }
        if (bloom_.prepare(commands_, in, p)) {
            // The composite writes every channel of each pixel it covers, unblended (from the kept scene and the
            // levels): over the whole target the pass opens without loading the colour it replaces.
            begin_scene_pass_with(0u, nullptr, whole);
            bloom_.finish(pass_, color_format, scene_.samples);
        } else if (source) {
            begin_scene_pass_with(0u, nullptr, true);
            bloom_.draw_back(pass_, color_format, scene_.samples, *source, in.frame_width, in.frame_height);
        }
    }
    reset_draw_bindings();
}

// ---- the wide scene: the 3D scene in a wide colour target over the scene target's depth, its camera at the end ----

// The wide colour targets for the live scene target's size and samples, over its depth. False when the device cannot
// make them: the scene stays in the scene target.
bool RendererMtl::ensure_wide_set(SceneSet& set) {
    if (set.color && set.depth_serial == scene_.depth_serial && set.samples == scene_.samples) {
        set.depth = scene_.depth;
        set.ms_depth = scene_.ms_depth;
        return true;
    }
    mtl::Context& context = mtl::Context::get();
    const std::uint32_t width = scene_.color->width;
    const std::uint32_t height = scene_.color->height;
    retire(std::move(set.color));
    retire(std::move(set.ms_color));
    gpu::TextureRef color = context.create_texture(width, height, 1u, draw_layout_.wide_color, mtl::kUseTarget | mtl::kUseSample,
                                                   mtl::Storage::Private, "wide scene color");
    gpu::TextureRef ms_color;
    if (scene_.samples > 1u) {
        ms_color = context.create_texture(width, height, 1u, draw_layout_.wide_color, mtl::kUseTarget | mtl::kUseSample,
                                          mtl::Storage::Private, "wide scene color MSAA", scene_.samples);
    }
    if (!color || (scene_.samples > 1u && !ms_color)) {
        std::cerr << "[scene-camera] wide scene " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    set.color = std::move(color);
    set.ms_color = std::move(ms_color);
    set.depth = scene_.depth;
    set.ms_depth = scene_.ms_depth;
    set.samples = scene_.samples;
    set.depth_serial = scene_.depth_serial;
    set.wide = true;
    return true;
}

bool RendererMtl::begin_wide_scene(bool wanted, const SceneClearValues* initial_clear) {
    if (!wanted || scene_.wide || !scene_camera_.ready() || !scene_.color || output_scene_active_ || target_pass_active_) {
        return false;
    }
    end_scene_pass();
    wide_scene_used_frame_ = frame_index_;
    if (!ensure_wide_set(wide_scene_)) {
        return false;
    }
    record_wide_begin(initial_clear);
    return true;
}

// What the scene target holds (its pass end resolved a multisampled one) goes on under the scene.
void RendererMtl::record_wide_begin(const SceneClearValues* initial_clear) {
    const GpuPassScope scope{*this, GpuPass::Camera};
    end_scene_pass();
    SceneSet& set = wide_scene_;
    if (initial_clear != nullptr) {
        // A full clear immediately before the first scene draw has no background picture to copy: the wide colour
        // and the shared depth clear together, and the pass stays open for the geometry.
        std::swap(scene_, set);
        begin_scene_pass(initial_clear);
        reset_draw_bindings();
        return;
    }
    const gpu::TextureRef picture = scene_.color;
    set.depth_dirty = scene_.depth_dirty;
    std::swap(scene_, set);
    begin_scene_pass_with(0u, nullptr, true);
    scene_camera_.widen(pass_, draw_layout_.wide_color, scene_.samples, *picture);
    // The pass stays open for the scene's geometry.
    reset_draw_bindings();
}

void RendererMtl::end_wide_scene() {
    if (!scene_.wide) {
        return;
    }
    end_scene_pass();
    record_wide_end();
}

void RendererMtl::record_wide_end() {
    if (!scene_.wide) {
        return;
    }
    const GpuPassScope scope{*this, GpuPass::Camera};
    end_scene_pass();
    settle_depth_clear();
    SceneSet& set = wide_scene_;  // the scene target's own set, waiting
    const bool per_sample = scene_.samples > 1u;
    const gpu::TextureRef wide = scene_.color;
    const gpu::TextureRef wide_ms = scene_.ms_color;
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - camera_metered_).count();
    camera_metered_ = now;
    resolve_scene_depth();
    // After a load or a long pause the eye starts open.
    scene_camera_.meter(commands_, *wide, *scene_.depth, seconds > 1.0f ? -1.0f : seconds);
    const bool depth_dirty = scene_.depth_dirty;
    const bool resolve_output = scene_.samples > 1u && output_frame_for_window() == scene_map_ &&
                                ensure_output_set(scene_map_, false);
    std::swap(scene_, set);
    scene_.depth_dirty = depth_dirty;
    if (resolve_output) {
        // All 3D samples remain available to the tone mapper; its averaged result goes straight to the single-sample
        // picture used by post-AA and 2D, not back into a multisampled target.
        swap_scene_sets();
        clear_scene(0u, false, false, true);
    }
    begin_scene_pass_with(0u, nullptr, true);
    scene_camera_.draw(pass_, draw_layout_.scene_color, scene_.samples, resolve_output || per_sample ? *wide_ms : *wide, per_sample,
                       resolve_output);
    reset_draw_bindings();
}

}  // namespace galaxy::render
