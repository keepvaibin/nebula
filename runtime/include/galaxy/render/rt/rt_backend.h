// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What the tracer asks of a graphics API, one implementation per back end (rt_backend_d3d12.cpp, vk/rt_backend_vk.cpp).
// Kept small: acceleration structures, a bound scene, compute dispatches. Inline ray queries only, so a back end without
// ray tracing pipelines can provide it.

#include "galaxy/render/gpu_types.h"

namespace galaxy::render::rt {

// The driver takes the tracer's shaders: the measuring pipeline is made from this build's bytes and let go. Logged.
// False when the build holds none for this back end or the driver refuses them.
[[nodiscard]] bool shaders_load(gpu::Device* device);

}  // namespace galaxy::render::rt
