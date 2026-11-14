// SPDX-License-Identifier: GPL-3.0-only
// The Direct3D 11 renderer's parts that are not draws of the game's records: the presentation chain (SMAA or FXAA, FSR 1,
// RCAS, the last write; the post lane's), the game's bloom, ambient occlusion and the scene camera (the effects lane's).
// As post_process.h, game_bloom.h, ambient_occlusion.h and scene_camera.h for Direct3D 12, from the same shaders
// compiled for shader model 5.0.
#pragma once

#include "galaxy/render/d3d11/ambient_occlusion_d3d11.h"
#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/d3d11/game_bloom_d3d11.h"
#include "galaxy/render/d3d11/post_process_d3d11.h"
#include "galaxy/render/d3d11/scene_camera_d3d11.h"
