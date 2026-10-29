#include "JSystem/J3DGraphBase/J3DNativeView.hpp"
#include <nebula_game_api.h>
#include <nebula_gx_fifo.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // GXInit's projection (an orthographic identity-like one) until the game sets its own.
    bool sPerspective = false;
    f32 sTerms[6] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -2.0f};

    // The viewport as GX keeps it (set by the game each frame before anything reads it; a read before then stops).
    bool sViewportSet = false;
    f32 sViewport[6] = {};
    f32 sZScale = 16777216.0f;
    f32 sZOffset = 0.0f;
    // The scissor's corners in GX's coordinates (GXSetScissor's fields).
    bool sScissorSet = false;
    u32 sScissorTop = 0, sScissorLeft = 0, sScissorBottom = 0, sScissorRight = 0;

    void sendOp(const NebulaStateOp& rOp) {
        const NebulaHostServices* pHost = nebula_host();
        pHost->state(pHost->user, __NebulaGXWriter.ptr, &rOp, 1);
    }

    void sendViewport() {
        // GX's terms (__GXSetViewport): half sizes, centres offset by 342, the depth range scaled.
        NebulaStateOp op;
        memset(&op, 0, sizeof(op));
        op.kind = NEBULA_STATE_VIEWPORT;
        const f32 zMin = sViewport[4] * sZScale;
        const f32 zMax = sViewport[5] * sZScale;
        op.f[0] = sViewport[2] / 2.0f;
        op.f[1] = -sViewport[3] / 2.0f;
        op.f[2] = zMax - zMin;
        op.f[3] = sViewport[0] + (sViewport[2] / 2.0f) + 342.0f;
        op.f[4] = sViewport[1] + (sViewport[3] / 2.0f) + 342.0f;
        op.f[5] = zMax + sZOffset;
        sendOp(op);
    }

    void send() {
        NebulaStateOp op;
        memset(&op, 0, sizeof(op));
        op.kind = NEBULA_STATE_PROJECTION;
        op.a[0] = sPerspective ? 0 : 1;
        for (u32 i = 0; i < 6; i++) {
            op.f[i] = sTerms[i];
        }
        const NebulaHostServices* pHost = nebula_host();
        pHost->state(pHost->user, __NebulaGXWriter.ptr, &op, 1);
    }
}  // namespace

namespace J3DNativeView {
    void captureReadback(u32 channel) {
        const NebulaHostServices* pHost = nebula_host();
        pHost->readback_capture(pHost->user, __NebulaGXWriter.ptr, channel);
    }

    u32 readbackColor(u32 channel, s32 x, s32 y) {
        const NebulaHostServices* pHost = nebula_host();
        return pHost->readback_pixel(pHost->user, channel, 0, static_cast< u32 >(x), static_cast< u32 >(y));
    }

    u32 readbackDepth(u32 channel, s32 x, s32 y) {
        const NebulaHostServices* pHost = nebula_host();
        return pHost->readback_pixel(pHost->user, channel, 1, static_cast< u32 >(x), static_cast< u32 >(y));
    }

    void setProjection(const f32 (*pMtx)[4], GXProjectionType type) {
        // GXSetProjection's terms: the orthographic one takes the translation column, the perspective one the third.
        sPerspective = type == GX_PERSPECTIVE;
        sTerms[0] = pMtx[0][0];
        sTerms[2] = pMtx[1][1];
        sTerms[4] = pMtx[2][2];
        sTerms[5] = pMtx[2][3];
        sTerms[1] = sPerspective ? pMtx[0][2] : pMtx[0][3];
        sTerms[3] = sPerspective ? pMtx[1][2] : pMtx[1][3];
        send();
    }

    void setProjectionv(const f32* pValues) {
        sPerspective = pValues[0] == 0.0f;
        for (u32 i = 0; i < 6; i++) {
            sTerms[i] = pValues[1 + i];
        }
        send();
    }

    void getProjectionv(f32* pValues) {
        pValues[0] = sPerspective ? 0.0f : 1.0f;
        for (u32 i = 0; i < 6; i++) {
            pValues[1 + i] = sTerms[i];
        }
    }

    void setViewportJitter(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ, u32 field) {
        if (field == 0) {
            top -= 0.5f;
        }
        sViewport[0] = left;
        sViewport[1] = top;
        sViewport[2] = width;
        sViewport[3] = height;
        sViewport[4] = nearZ;
        sViewport[5] = farZ;
        sViewportSet = true;
        sendViewport();
    }

    void setViewport(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ) {
        setViewportJitter(left, top, width, height, nearZ, farZ, 1);
    }

    void getViewportv(f32* pValues) {
        if (!sViewportSet) {
            OSPanic(__FILE__, __LINE__, "J3DNativeView: the viewport read before the game set one");
        }
        for (u32 i = 0; i < 6; i++) {
            pValues[i] = sViewport[i];
        }
    }

    void setZScaleOffset(f32 scale, f32 offset) {
        sZOffset = offset * static_cast< f32 >(0x00FFFFFF);
        sZScale = scale * static_cast< f32 >(0xFFFFFF) + 1.0f;
        if (sViewportSet) {
            sendViewport();
        }
    }

    void setScissor(u32 left, u32 top, u32 width, u32 height) {
        const u32 tp = top + 342;
        const u32 lf = left + 342;
        sScissorTop = tp & 0x7FF;
        sScissorLeft = lf & 0x7FF;
        sScissorBottom = (tp + height - 1) & 0x7FF;
        sScissorRight = (lf + width - 1) & 0x7FF;
        sScissorSet = true;
        NebulaStateOp op;
        memset(&op, 0, sizeof(op));
        op.kind = NEBULA_STATE_SCISSOR;
        op.f[0] = static_cast< f32 >(left);
        op.f[1] = static_cast< f32 >(top);
        op.f[2] = static_cast< f32 >(width);
        op.f[3] = static_cast< f32 >(height);
        sendOp(op);
    }

    void getScissor(u32* pLeft, u32* pTop, u32* pWidth, u32* pHeight) {
        if (!sScissorSet) {
            OSPanic(__FILE__, __LINE__, "J3DNativeView: the scissor read before the game set one");
        }
        *pLeft = sScissorLeft - 342;
        *pTop = sScissorTop - 342;
        *pWidth = sScissorRight - sScissorLeft + 1;
        *pHeight = sScissorBottom - sScissorTop + 1;
    }

    void setScissorBoxOffset(s32 x, s32 y) {
        NebulaStateOp op;
        memset(&op, 0, sizeof(op));
        op.kind = NEBULA_STATE_SCISSOR_OFFSET;
        op.f[0] = static_cast< f32 >(x);
        op.f[1] = static_cast< f32 >(y);
        sendOp(op);
    }
};  // namespace J3DNativeView

extern "C" {
void J3DNativeViewSetProjection(const f32 (*pMtx)[4], GXProjectionType type) {
    J3DNativeView::setProjection(pMtx, type);
}

void J3DNativeViewSetProjectionv(const f32* pValues) {
    J3DNativeView::setProjectionv(pValues);
}

void J3DNativeViewGetProjectionv(f32* pValues) {
    J3DNativeView::getProjectionv(pValues);
}

void J3DNativeViewSetViewport(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ) {
    J3DNativeView::setViewport(left, top, width, height, nearZ, farZ);
}

void J3DNativeViewGetViewportv(f32* pValues) {
    J3DNativeView::getViewportv(pValues);
}

void J3DNativeViewSetZScaleOffset(f32 scale, f32 offset) {
    J3DNativeView::setZScaleOffset(scale, offset);
}

void J3DNativeViewSetScissor(u32 left, u32 top, u32 width, u32 height) {
    J3DNativeView::setScissor(left, top, width, height);
}

void J3DNativeViewGetScissor(u32* pLeft, u32* pTop, u32* pWidth, u32* pHeight) {
    J3DNativeView::getScissor(pLeft, pTop, pWidth, pHeight);
}
}
