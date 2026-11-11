// SPDX-License-Identifier: GPL-3.0-only
// The sky and bounce light (rt_indirect.hlsl): what the sky and the lit surfaces near a surface give it. Off: the pass is not written yet, and its images are not made.
#include "galaxy/render/render_core.h"

namespace galaxy::render {

bool RenderCore::rt_pass_indirect_on() const { return false; }

void RenderCore::rt_pass_indirect() {}

}  // namespace galaxy::render
