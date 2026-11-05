#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>

namespace MR {
    void ddSetVtxFormat(u32);
    void ddSendVtxData(const Vec3f&, const Vec3f&, const Vec2f&);
    void ddSendVtxData(const Vec3f&, const Vec2f&);
    // The values ddSendVtxData sends per vertex for the format ddSetVtxFormat set (NEBULA_SHAPE_* bits).
    u32 ddVtxAttributes();
};  // namespace MR
