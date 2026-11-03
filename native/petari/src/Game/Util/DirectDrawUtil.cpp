#include "Game/Util/DirectDrawUtil.hpp"
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>

u32 mVtxMode;

namespace MR {
    // The vertex values ddSendVtxData sends: 1 normals, 2 texture coordinates, 4 colours (the site's native material
    // reads them).
    void ddSetVtxFormat(u32 format) {
        mVtxMode = format;
    }

    void ddSendVtxData(const TVec3f& rVec1, const TVec3f& rVec2, const TVec2f& rVec3) {
        J3DNativeDraw::position(rVec1.x, rVec1.y, rVec1.z);
        if ((mVtxMode & 1) != 0) {
            J3DNativeDraw::normal(rVec2.x, rVec2.y, rVec2.z);
        }

        if ((mVtxMode & 2) != 0) {
            J3DNativeDraw::texCoord(rVec3.x, rVec3.y);
        }
    }

    void ddSendVtxData(const TVec3f& rVec1, const TVec2f& rVec2) {
        J3DNativeDraw::position(rVec1.x, rVec1.y, rVec1.z);
        if ((mVtxMode & 2) != 0) {
            J3DNativeDraw::texCoord(rVec2.x, rVec2.y);
        }
    }

    u32 ddVtxAttributes() {
        u32 attributes = 0;
        if ((mVtxMode & 1) != 0) {
            attributes |= NEBULA_SHAPE_NORMAL;
        }
        if ((mVtxMode & 2) != 0) {
            attributes |= NEBULA_SHAPE_TEXCOORD0;
        }
        if ((mVtxMode & 4) != 0) {
            attributes |= NEBULA_SHAPE_COLOR0;
        }
        return attributes;
    }
};  // namespace MR
