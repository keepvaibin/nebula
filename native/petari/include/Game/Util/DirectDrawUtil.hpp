#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <revolution/gx.h>

namespace MR {
    void ddSetVtxFormat(u32);
    void ddChangeTev();
    void ddSendVtxData(const TVec3f&, const TVec3f&, const TVec2f&);
    void ddSendVtxData(const TVec3f&, const TVec2f&);
    // The values ddSendVtxData sends per vertex for the format ddSetVtxFormat set (NEBULA_SHAPE_* bits).
    u32 ddVtxAttributes();
    void ddLightingOn(u16);
    void ddLightingOff();
};  // namespace MR
