#include "Game/Screen/NebulaFadeGroup.hpp"

#ifdef TARGET_PC
#include "Game/Util/MotionSignal.hpp"
#include <JSystem/JUtility/JUTVideo.hpp>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <revolution/gx.h>
#include <revolution/mtx.h>

namespace {
    // Groups open inside one another (a window's fade around its buttons' fades).
    const s32 cDepthMax = 4;
    // The largest EFB (640x528), as RGBA8.
    const u32 cBackdropBytes = 640 * 528 * 4;

    // The copies live in the game image: the renderer reads GX textures from the game's own memory. Each is the whole
    // screen: the copy and the quad that lays it back then never move, so interpolated frames between two game frames
    // (which move a sliding window but replay the copy where it was) still lay the screen back where it came from, and
    // the renderer keeps one texture size per copy.
    alignas(32) u8 sBackdropImage[cDepthMax][cBackdropBytes];

    s32 sCalcDepth = 0;
    s32 sDrawDepth = 0;

    // A picture, window or text box that draws (Pane::DrawSelf of each), not faded out.
    bool drawsItself(const nw4r::lyt::Pane* pPane) {
        if (pPane->GetMaterial() == nullptr || pPane->mAlpha == 0) {
            return false;
        }
        const nw4r::lyt::TextBox* pText = nw4r::ut::DynamicCast< const nw4r::lyt::TextBox* >(pPane);
        return pText == nullptr || (pText->mTextBuf != nullptr && pText->mpFont != nullptr && pText->mTextLen != 0);
    }

    // The layers that draw in a pane and its visible children, counted up to `limit`.
    s32 countLayers(const nw4r::lyt::Pane* pPane, s32 limit) {
        s32 count = drawsItself(pPane) ? 1 : 0;
        const nw4r::lyt::PaneList& rChildren = pPane->GetChildList();
        for (nw4r::lyt::PaneList::ConstIterator it = rChildren.GetBeginIter(); it != rChildren.GetEndIter() && count < limit; ++it) {
            if (!it->IsVisible() || (it->IsInfluencedAlpha() && it->mAlpha == 0)) {
                continue;
            }
            count += countLayers(&*it, limit - count);
        }
        return count;
    }
};  // namespace

namespace NebulaFadeGroup {
    bool enter(const nw4r::lyt::Pane* pPane) {
        if (sCalcDepth >= cDepthMax || pPane->mAlpha == 0 || countLayers(pPane, 2) < 2) {
            return false;
        }
        sCalcDepth++;
        return true;
    }

    void leave() {
        sCalcDepth--;
    }

    void begin(const nw4r::lyt::Pane* pPane, const nw4r::lyt::DrawInfo& rInfo) {
        // Deeper than the copies (a branch shown after CalculateMtx left it): drawn without one.
        const s32 depth = sDrawDepth++;
        if (depth >= cDepthMax) {
            return;
        }
        // As CaptureScreenDirector copies the screen: no vertical filter.
        GXRenderModeObj* pMode = JUTVideo::getManager()->getRenderMode();
        GXSetCopyFilter(GX_FALSE, pMode->sample_pattern, GX_FALSE, pMode->vfilter);
        GXSetTexCopySrc(0, 0, pMode->fbWidth, pMode->efbHeight);
        GXSetTexCopyDst(pMode->fbWidth, pMode->efbHeight, GX_TF_RGBA8, GX_FALSE);
        GXCopyTex(sBackdropImage[depth], GX_FALSE);
        GXPixModeSync();
        GXSetCopyFilter(GX_FALSE, pMode->sample_pattern, GX_TRUE, pMode->vfilter);
    }

    void end(const nw4r::lyt::Pane* pPane, u8 alpha) {
        const s32 depth = --sDrawDepth;
        if (depth >= cDepthMax || alpha == 255) {
            return;
        }
        MotionSignal::layoutPaneGroup(pPane);
        GXRenderModeObj* pMode = JUTVideo::getManager()->getRenderMode();
        f32 proj[7];
        f32 viewport[6];
        GXGetProjectionv(proj);
        GXGetViewportv(viewport);

        // The copy over the screen, in EFB pixels, at 255 less the pane's alpha.
        Mtx44 ortho;
        C_MTXOrtho(ortho, 0.0f, pMode->efbHeight, 0.0f, pMode->fbWidth, 0.0f, 1.0f);
        GXSetProjection(ortho, GX_ORTHOGRAPHIC);
        GXSetViewport(0.0f, 0.0f, pMode->fbWidth, pMode->efbHeight, 0.0f, 1.0f);
        Mtx identity;
        PSMTXIdentity(identity);
        GXLoadPosMtxImm(identity, GX_PNMTX0);
        GXSetCurrentMtx(GX_PNMTX0);

        GXTexObj texture;
        GXInitTexObj(&texture, sBackdropImage[depth], pMode->fbWidth, pMode->efbHeight, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GXInitTexObjLOD(&texture, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        GXLoadTexObj(&texture, GX_TEXMAP0);

        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetNumTexGens(1);
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        GXSetNumIndStages(0);
        GXSetNumTevStages(1);
        GXSetTevDirect(GX_TEVSTAGE0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
        GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
        // The copy's colour; the vertex's alpha (the screen copy's own alpha is whatever the EFB held).
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        GXSetZMode(GX_FALSE, GX_NEVER, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);

        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

        const f32 x1 = pMode->fbWidth;
        const f32 y1 = pMode->efbHeight;
        const u8 rest = static_cast< u8 >(255 - alpha);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition2f32(0.0f, 0.0f);
        GXColor4u8(255, 255, 255, rest);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition2f32(x1, 0.0f);
        GXColor4u8(255, 255, 255, rest);
        GXTexCoord2f32(1.0f, 0.0f);
        GXPosition2f32(x1, y1);
        GXColor4u8(255, 255, 255, rest);
        GXTexCoord2f32(1.0f, 1.0f);
        GXPosition2f32(0.0f, y1);
        GXColor4u8(255, 255, 255, rest);
        GXTexCoord2f32(0.0f, 1.0f);
        GXEnd();

        GXSetProjectionv(proj);
        GXSetViewport(viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]);
    }
};  // namespace NebulaFadeGroup
#endif
