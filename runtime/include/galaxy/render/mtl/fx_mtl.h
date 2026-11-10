// SPDX-License-Identifier: GPL-3.0-only
// The Metal renderer's parts that are not draws of the game's records: the presentation chain (SMAA or FXAA, FSR 1 or
// MetalFX's spatial scaler, sharpening, the last write), the game's bloom, ambient occlusion and the scene camera.
// As vk/post_process_vk.h, vk/game_bloom_vk.h, vk/ambient_occlusion_vk.h and vk/scene_camera_vk.h for Vulkan, from
// the same shaders (mtl/utility_msl.h).
#pragma once

#include "galaxy/render/mtl/ambient_occlusion_mtl.h"
#include "galaxy/render/mtl/game_bloom_mtl.h"
#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/mtl/post_process_mtl.h"
#include "galaxy/render/mtl/scene_camera_mtl.h"
#include "galaxy/render/post_process_settings.h"

namespace galaxy::render::mtl {

}  // namespace galaxy::render::mtl
