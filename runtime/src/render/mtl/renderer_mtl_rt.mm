// SPDX-License-Identifier: GPL-3.0-only
// RendererMtl: what the ray tracer records through (renderer_mtl.h, "What the ray tracer records through"), as the
// same functions of vk/renderer_vk_frame.cpp. Metal tracks the renderer's textures between encoders, so a hand-over is
// the open encoder ended; the tracer's compute encoders follow in the same command buffer.
#include "renderer_mtl_internal.h"

#include "galaxy/render/program_table.h"
#include "galaxy/render/texture_store.h"

namespace galaxy::render {

using namespace mtl_detail;

RendererMtl::RtCommands RendererMtl::rt_commands_begin() {
    end_scene_pass("ray tracing");
    rt_outer_pass_ = current_pass_;
    gpu_pass(GpuPass::RayTracing);
    rt_textures_ = {};
    if (bindless_ && mirrored_store_ != nullptr) {
        const TextureBackend& backend = mirrored_store_->backend();
        rt_textures_.set = backend.texture_array();
        if (!rt_table_resident_) {
            rt_table_resident_ = [this](void* encoder) {
                if (mirrored_store_ != nullptr) {
                    mirrored_store_->backend().use_textures_compute(encoder);
                }
            };
        }
        rt_textures_.layout = &rt_table_resident_;
    }
    return {commands_, rt_textures_.set != nullptr ? &rt_textures_ : nullptr};
}

void RendererMtl::rt_commands_end() { gpu_pass(rt_outer_pass_); }

bool RendererMtl::rt_depth_begin(rt::TextureView& depth, unsigned& width, unsigned& height, rt::ImageFormat& color,
                                 rt::TextureView* color_view) {
    if (!scene_.color || !scene_.depth || scene_.samples > 1u ||
        (scene_.color->format != draw_layout_.scene_color && scene_.color->format != mtl::Format::Rgba16f)) {
        return false;
    }
    end_scene_pass("ray tracing");
    settle_depth_clear();
    depth = {scene_.depth->texture, static_cast<std::uint32_t>(scene_.depth->format)};
    if (color_view != nullptr) {
        if ((scene_.color->use & mtl::kUseSample) == 0u) {
            return false;
        }
        *color_view = {scene_.color->texture, static_cast<std::uint32_t>(scene_.color->format)};
    }
    width = scene_.depth->width;
    height = scene_.depth->height;
    color = scene_.color->format == mtl::Format::Rgba16f ? rt::ImageFormat::Rgba16f : rt::ImageFormat::Rgb10a2;
    return true;
}

void RendererMtl::rt_copy_to_scene(void* image) {
    const auto* const source = static_cast<const mtl::Texture*>(image);
    if (source == nullptr || !scene_.color || source->width != scene_.color->width || source->height != scene_.color->height ||
        source->format != scene_.color->format) {
        return;
    }
    end_scene_pass("ray tracing");
    @autoreleasepool {
        id<MTLBlitCommandEncoder> const blit = [commands_of(commands_) blitCommandEncoder];
        if (blit == nil) {
            return;
        }
        blit.label = @"ray tracing over the scene";
        [blit copyFromTexture:texture_of(*source) toTexture:texture_of(scene_.color)];
        [blit endEncoding];
    }
}

bool RendererMtl::rt_items_begin() {
    if (!scene_.color || !scene_.depth || scene_.samples > 1u || target_pass_active_) {
        return false;
    }
    if (!item_target_ || item_target_->width != scene_.depth->width || item_target_->height != scene_.depth->height) {
        end_scene_pass("ray tracing");
        retire(std::move(item_target_));
        item_target_ = mtl::Context::get().create_texture(scene_.depth->width, scene_.depth->height, 1u, draw_layout_.items,
                                                          mtl::kUseTarget | mtl::kUseSample, mtl::Storage::Private,
                                                          "ray tracing items");
        if (!item_target_) {
            return false;
        }
    }
    item_clear_pending_ = true;
    return true;
}

bool RendererMtl::rt_light_begin(rt::TextureView& depth, rt::TextureView& items, unsigned& width, unsigned& height) {
    if (!scene_.depth || !item_target_ || scene_.samples > 1u) {
        return false;
    }
    if (item_clear_pending_) {
        begin_scene_pass_with(kAuxPrepass, nullptr, false, "ray tracing items");  // the prepass drew nothing: cleared all the same
    }
    end_scene_pass("ray tracing");
    settle_depth_clear();
    depth = {scene_.depth->texture, static_cast<std::uint32_t>(scene_.depth->format)};
    items = {item_target_->texture, static_cast<std::uint32_t>(item_target_->format)};
    width = scene_.depth->width;
    height = scene_.depth->height;
    return true;
}

bool RendererMtl::rt_reactive_begin(rt::TextureView& reactive) {
    if (!reactive_scene_ || !motion_.reactive || (motion_.reactive->use & mtl::kUseWrite) == 0u) {
        return false;
    }
    end_scene_pass("ray tracing");
    reactive = {motion_.reactive->texture, static_cast<std::uint32_t>(motion_.reactive->format)};
    return true;
}

bool RendererMtl::rt_target_read_begin(std::uint64_t key, rt::TextureView& view) {
    const auto found = target_slots_.find(key);
    if (found == target_slots_.end() || !found->second.set.color || (target_pass_active_ && target_key_ == key)) {
        return false;
    }
    end_scene_pass("ray tracing");
    const mtl::Texture& color = *found->second.set.color;
    view = {color.texture, static_cast<std::uint32_t>(color.format)};
    return true;
}

void RendererMtl::rt_unpublish_lights(TextureStore& textures) {
    for (unsigned which = 0u; which < kRtLightPictures; ++which) {  // light, added, mirror, facing, volume, scatter, transmit, indirect, two guides, the layer's items
        const auto found = pictures_.find(kRtLightPicture + which);
        if (found == pictures_.end()) {
            continue;
        }
        drop_picture(found->second, textures);
        pictures_.erase(found);
    }
}

std::uint32_t RendererMtl::rt_publish_light(void* image, TextureStore& textures, unsigned which) {
    auto* const texture = static_cast<mtl::Texture*>(image);
    if (texture == nullptr) {
        // The array's last slot reads the null texture while the array has this length (TextureBackend::array_slots).
        const std::uint32_t slots = textures.backend().array_slots();
        return slots != 0u ? slots - 1u : 0u;
    }
    // The picture holds the texture while the array names it: the tracer may let it go while draws still sample it.
    static_cast<void>(publish_picture(kRtLightPicture + which, texture->width, texture->height, gpu::texture_ref(texture), 0u,
                                      textures));
    return pictures_[kRtLightPicture + which].view;
}

}  // namespace galaxy::render
