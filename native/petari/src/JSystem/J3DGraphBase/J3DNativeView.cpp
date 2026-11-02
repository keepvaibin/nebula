#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "JSystem/J3DGraphBase/J3DNativeView.hpp"
#include <nebula_game_api.h>
#include "Game/Util/RenderSignals.hpp"
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // An orthographic identity-like projection until the game sets its own.
    bool sPerspective = false;
    f32 sTerms[6] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -2.0f};

    // The viewport the game set (left, top, width, height, near, far; set each frame before anything reads it, a read
    // before then stops) and the depth scale and offset it maps near and far through.
    bool sViewportSet = false;
    f32 sViewport[6] = {};
    f32 sDepthScale = 1.0f;
    f32 sDepthOffset = 0.0f;
    // The scissor box: left, top, width, height.
    bool sScissorSet = false;
    u32 sScissor[4] = {};

    void sendOp(const NebulaStateOp& rOp) {
        J3DNativeDraw::flush();
        const NebulaHostServices* pHost = nebula_host();
        pHost->state(pHost->user, &rOp, 1);
    }

    void sendViewport() {
        // The rectangle in frame pixels and the depth range (0 near .. 1 far) the near and far planes map to.
        NebulaStateOp op;
        memset(&op, 0, sizeof(op));
        op.kind = NEBULA_STATE_VIEWPORT;
        op.f[0] = sViewport[0];
        op.f[1] = sViewport[1];
        op.f[2] = sViewport[2];
        op.f[3] = sViewport[3];
        op.f[4] = sViewport[4] * sDepthScale + sDepthOffset;
        op.f[5] = sViewport[5] * sDepthScale + sDepthOffset;
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
        J3DNativeDraw::flush();
        pHost->state(pHost->user, &op, 1);
    }
}  // namespace

namespace J3DNativeView {
    void probeScreen(u32 channel, NebulaScreenProbeKind what, const NebulaScreenProbe* pProbes, u32 count) {
        J3DNativeDraw::flush();
        const NebulaHostServices* pHost = nebula_host();
        pHost->screen_probe(pHost->user, channel, what, pProbes, count);
    }

    u32 screenProbeResults(u32 channel, NebulaScreenProbe* pProbes, u32 capacity) {
        const NebulaHostServices* pHost = nebula_host();
        return pHost->screen_probe_results(pHost->user, channel, pProbes, capacity);
    }

    namespace {
        // The matrix's terms: the orthographic one keeps its translation column, the perspective its third.
        void setProjection(const f32 (*pMtx)[4], bool perspective) {
            sPerspective = perspective;
            sTerms[0] = pMtx[0][0];
            sTerms[2] = pMtx[1][1];
            sTerms[4] = pMtx[2][2];
            sTerms[5] = pMtx[2][3];
            sTerms[1] = perspective ? pMtx[0][2] : pMtx[0][3];
            sTerms[3] = perspective ? pMtx[1][2] : pMtx[1][3];
            send();
        }
    }  // namespace

    void setPerspective(const f32 (*pMtx)[4]) {
        setProjection(pMtx, true);
    }

    void setOrthographic(const f32 (*pMtx)[4]) {
        setProjection(pMtx, false);
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
            NEBULA_PANIC("J3DNativeView: the viewport read before the game set one");
        }
        for (u32 i = 0; i < 6; i++) {
            pValues[i] = sViewport[i];
        }
    }

    void setZScaleOffset(f32 scale, f32 offset) {
        sDepthScale = scale;
        sDepthOffset = offset;
        if (sViewportSet) {
            sendViewport();
        }
    }

    void setScissor(u32 left, u32 top, u32 width, u32 height) {
        sScissor[0] = left;
        sScissor[1] = top;
        sScissor[2] = width;
        sScissor[3] = height;
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
            NEBULA_PANIC("J3DNativeView: the scissor read before the game set one");
        }
        *pLeft = sScissor[0];
        *pTop = sScissor[1];
        *pWidth = sScissor[2];
        *pHeight = sScissor[3];
    }
};  // namespace J3DNativeView

extern "C" {
void J3DNativeViewSetPerspective(const f32 (*pMtx)[4]) {
    J3DNativeView::setPerspective(pMtx);
}

void J3DNativeViewSetOrthographic(const f32 (*pMtx)[4]) {
    J3DNativeView::setOrthographic(pMtx);
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
