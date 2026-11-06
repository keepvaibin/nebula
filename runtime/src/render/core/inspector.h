// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The developer inspector (NEBULA_DEV builds): what the walker tells it, and how a walk asks it. A release has none of it.

#include "galaxy/render/gpu_types.h"
#include "render/core/gpu_encoder.h"

#include <nebula_frame_list.h>

#include <cstdint>

#if defined(NEBULA_DEV)

namespace galaxy::render {
class RenderCore;
namespace core {
struct PipelineEntry;
}
namespace inspector {

struct Access;
void begin_walk(RenderCore& core, const NebulaFrameList& list);
// An item about to draw: false skips it (GALAXY_INSPECT_SOLO). `pipeline` may become the one a GALAXY_INSPECT_SHOW asks.
[[nodiscard]] bool item(RenderCore& core, std::uint32_t index, std::uint32_t instances, const NebulaFramePass& pass, std::uint8_t aux,
                        gpu::Pipeline*& pipeline, const core::PipelineEntry& entry);
void draw_begin(RenderCore& core, std::uint32_t index, std::uint32_t instances, const NebulaFramePass& pass, gpu::Encoder& encoder,
                const gpu::Rect& scissor);
void draw_end(RenderCore& core);
void end_walk(RenderCore& core);
// A real frame has ended: its list again under a one-pixel scissor, when one is asked for.
void after_frame(RenderCore& core, void* list_entry);
[[nodiscard]] bool replaying() noexcept;
[[nodiscard]] std::uint32_t show_flags() noexcept;

}  // namespace inspector
}  // namespace galaxy::render

#define NEBULA_INSPECT_BEGIN(core, list) ::galaxy::render::inspector::begin_walk(core, list)
#define NEBULA_INSPECT_ITEM(core, index, instances, pass, aux, pipeline, entry)                        \
    if (!::galaxy::render::inspector::item(core, index, instances, pass, aux, pipeline, entry)) {      \
        i += instances;                                                                                \
        continue;                                                                                      \
    }
#define NEBULA_INSPECT_DRAW_BEGIN(core, index, instances, pass, encoder, scissor) \
    ::galaxy::render::inspector::draw_begin(core, index, instances, pass, encoder, scissor)
#define NEBULA_INSPECT_DRAW_END(core) ::galaxy::render::inspector::draw_end(core)
#define NEBULA_INSPECT_END(core) ::galaxy::render::inspector::end_walk(core)
#define NEBULA_INSPECT_AFTER_FRAME(core, entry) ::galaxy::render::inspector::after_frame(core, &(entry))
#define NEBULA_INSPECT_REPLAYING() ::galaxy::render::inspector::replaying()
#define NEBULA_INSPECT_FLAGS() ::galaxy::render::inspector::show_flags()

#else

#define NEBULA_INSPECT_BEGIN(core, list)
#define NEBULA_INSPECT_ITEM(core, index, instances, pass, aux, pipeline, entry)
#define NEBULA_INSPECT_DRAW_BEGIN(core, index, instances, pass, encoder, scissor)
#define NEBULA_INSPECT_DRAW_END(core)
#define NEBULA_INSPECT_END(core)
#define NEBULA_INSPECT_AFTER_FRAME(core, entry)
#define NEBULA_INSPECT_REPLAYING() false
#define NEBULA_INSPECT_FLAGS() 0u

#endif
