#include "Game/Util/DirectDrawUtil.hpp"
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>

u32 mVtxMode;
u32 mLightingFlag;
u16 mLightingMask;

namespace MR {
    void ddSetVtxFormat(u32 format) {
        mVtxMode = format;
        if (mLightingFlag != 0) {
            ddLightingOn(mLightingMask);
        } else {
            ddLightingOff();
        }

        ddChangeTev();
    }

    void ddChangeTev() {
        if ((mVtxMode & 2) != 0) {
            GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
            GXSetNumTexGens(1);
            GXSetNumTevStages(1);
            if (mLightingFlag != 0) {
                GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
                GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
            } else {
                GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
                GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
            }
        } else {
            if ((mVtxMode & 8) != 0) {
                GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_NRM, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
                GXSetNumTexGens(1);
                GXSetNumTevStages(1);
                if (mLightingFlag != 0) {
                    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
                    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
                } else {
                    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
                    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
                }
            } else {
                GXSetNumTexGens(0);
                GXSetNumTevStages(1);
                GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
                GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            }
        }
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

    void ddLightingOn(u16 light) {
        mLightingFlag = 1;
        mLightingMask = light;
        if ((mVtxMode & 4) != 0) {
            GXSetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_VTX, GX_SRC_VTX, mLightingMask, GX_DF_CLAMP, GX_AF_NONE);
        } else {
            GXSetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, mLightingMask, GX_DF_CLAMP, GX_AF_NONE);
        }
    }

    void ddLightingOff() {
        mLightingFlag = 0;
        if ((mVtxMode & 4) != 0) {
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
        } else {
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
        }
    }
};  // namespace MR
