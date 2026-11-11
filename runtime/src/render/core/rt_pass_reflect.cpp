// SPDX-License-Identifier: GPL-3.0-only
// The reflections (rt_reflect.hlsl): what glossy surfaces mirror, by their roughness. Off: the pass is not written yet, and its images are not made.
#include "galaxy/render/render_core.h"

namespace galaxy::render {

bool RenderCore::rt_pass_reflect_on() const { return false; }

void RenderCore::rt_pass_reflect() {}

}  // namespace galaxy::render
