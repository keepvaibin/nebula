#include "JSystem/J2DGraph/J2DGrafContext.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>

J2DGrafContext::J2DGrafContext(f32 x, f32 y, f32 width, f32 height)
    : mBounds(x, y, x + width, y + height), mScissorBounds(x, y, x + width, y + height) {
    JUtility::TColor color(-1);
    setColor(color);
    mLineWidth = 6;
    J3DNativeState::setLineWidth(mLineWidth, GX_TO_ZERO);
}

void J2DGrafContext::setColor(JUtility::TColor colorTL, JUtility::TColor colorTR, JUtility::TColor colorBR, JUtility::TColor colorBL) {
    mColorTL = colorTL;
    mColorTR = colorTR;
    mColorBR = colorBR;
    mColorBL = colorBL;
    _B0.mType = GX_BM_BLEND;
    _B0.mSrcFactor = GX_BL_SRCALPHA;
    _B0.mDestFactor = GX_BL_INVSRCALPHA;
    mLinePart.mType = GX_BM_BLEND;
    mLinePart.mSrcFactor = GX_BL_SRCALPHA;
    mLinePart.mDestFactor = GX_BL_INVSRCALPHA;
    mBoxPart.mType = GX_BM_BLEND;
    mBoxPart.mSrcFactor = GX_BL_SRCALPHA;
    mBoxPart.mDestFactor = GX_BL_INVSRCALPHA;
    if ((mColorTL & 0xFF) != 0xFF) {
        return;
    }

    _B0.mType = GX_BM_NONE;
    _B0.mSrcFactor = GX_BL_ONE;
    _B0.mDestFactor = GX_BL_ZERO;
    if ((mColorBR & 0xFF) != 0xFF) {
        return;
    }

    mLinePart.mType = GX_BM_NONE;
    mLinePart.mSrcFactor = GX_BL_ONE;
    mLinePart.mDestFactor = GX_BL_ZERO;
    if ((mColorTR & 0xFF) != 0xFF) {
        return;
    }

    if ((mColorBL & 0xFF) != 0xFF) {
        return;
    }

    mBoxPart.mType = GX_BM_NONE;
    mBoxPart.mSrcFactor = GX_BL_ONE;
    mBoxPart.mDestFactor = GX_BL_ZERO;
}

void J2DGrafContext::setPort() {
    setScissor();
    setup2D();

    JGeometry::TBox2< float > bounds(mBounds);

    if (bounds.i.x < 0.0f) {
        bounds.i.x = 0.0f;
    }

    if (bounds.i.y < 0.0f) {
        bounds.i.y = 0.0f;
    }

    J3DNativeView::setViewport(bounds.i.x, bounds.i.y, bounds.getWidth(), bounds.getHeight(), 0.0f, 1.0f);
}

// The 2D view's fixed state: the panes draw with their own native materials (J2DPicture, J2DMaterial,
// fillBox's vertex_color), so the TEV program the console reset here has no reader.
void J2DGrafContext::setup2D() {
    J3DNativeState::setZCompLoc(GX_FALSE);
    J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_GREATER, 0);
    J3DNativeState::setZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeShape::setPosition(0, mPosMtx);
    Mtx mtx;
    NebulaMtxIdentity(mtx);
    J3DNativeShape::setTexture(J3DNativeShape::kIdentityTexture, mtx, 3);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setLineWidth(mLineWidth, GX_TO_ZERO);
}

void J2DGrafContext::setScissor() {
    JGeometry::TBox2< f32 > bounds(0, 0, 1024, 1024);
    JGeometry::TBox2< f32 > curBounds(mScissorBounds);
    mScissorBounds.intersect(bounds);
    curBounds.absolute();
    if (curBounds.intersect(bounds)) {
        curBounds.i.x = (u32)curBounds.i.x;
        curBounds.i.y = (u32)curBounds.i.y;
        curBounds.f.x = ceil(curBounds.f.x);
        curBounds.f.y = ceil(curBounds.f.y);
        J3DNativeView::setScissor(curBounds.i.x, curBounds.i.y, curBounds.getWidth(), curBounds.getHeight());
    } else {
        J3DNativeView::setScissor(0, 0, 0, 0);
    }
}

void J2DGrafContext::place(JGeometry::TBox2< f32 > const& rBounds) {
    if (rBounds.i.x < 0.0f || rBounds.i.y < 0.0f) {
    }

    mBounds = rBounds;
    mScissorBounds = rBounds;
}

void J2DGrafContext::fillBox(JGeometry::TBox2< f32 > const& rBox) {
    // runtime/src/render/materials/vertex_color.hlsl: the corners' colours where their alpha is above 0 (setup2D's
    // test), the box's blending, no depth test or culling.
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_VERTEX_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                       NEBULA_CULL_NONE);
    if (mBoxPart.mType == GX_BM_BLEND) {
        NativeShader::setBlend(material, mBoxPart.mSrcFactor, mBoxPart.mDestFactor);
    } else if (mBoxPart.mType == GX_BM_SUBTRACT) {
        material.blend_op = NEBULA_BLEND_SUBTRACT;
    }
    material.constants[4] = 0.0f;
    J3DNativeShape::setPosition(0, mPosMtx);
    static const u8 kIdentity[8] = {GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                    GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
    J3DNativeShape::setCurrent(GX_PNMTX0, kIdentity);
    NativeShader::send(material);
    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
    J3DNativeDraw::position(rBox.i.x, rBox.i.y, 0);
    J3DNativeDraw::color(mColorTL);
    J3DNativeDraw::position(rBox.f.x, rBox.i.y, 0);
    J3DNativeDraw::color(mColorTR);
    J3DNativeDraw::position(rBox.f.x, rBox.f.y, 0);
    J3DNativeDraw::color(mColorBL);
    J3DNativeDraw::position(rBox.i.x, rBox.f.y, 0);
    J3DNativeDraw::color(mColorBR);
}

void J2DGrafContext::setLookat() {
}

J2DGrafType J2DGrafContext::getGrafType() const {
    return J2DGraf_Base;
}

void J2DGrafContext_FORCE_MATCH(J2DGrafContext* pContext, JUtility::TColor color) {
    pContext->setColor(color);
    pContext->setColor(color);
    pContext->setColor(color);
    pContext->setColor(color);
}
