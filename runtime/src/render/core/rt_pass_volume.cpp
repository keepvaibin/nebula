// SPDX-License-Identifier: GPL-3.0-only
// The light volume (rt_volume.hlsl): the light in the air over the scene camera's view. Off: the pass is not written yet, and its images are not made.
#include "galaxy/render/render_core.h"

namespace galaxy::render {

bool RenderCore::rt_pass_volume_on() const { return false; }

void RenderCore::rt_pass_volume() {}

}  // namespace galaxy::render
