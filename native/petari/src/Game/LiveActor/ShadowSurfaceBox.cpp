#include "Game/LiveActor/ShadowSurfaceBox.hpp"
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/Util/DirectDraw.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JGeometry/TVec.hpp>
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXGeometry.h>
#include <revolution/gx/GXVert.h>
#include <revolution/mtx.h>

ShadowSurfaceBox::~ShadowSurfaceBox() {
}

ShadowSurfaceBox::ShadowSurfaceBox() : ShadowSurfaceDrawer("\211\145\225\140\211\346[\220\205\226\312\203\173\203\142\203\116\203\130]" /* 影描画[水面ボックス] */), mSize(100.0f) {
}

void ShadowSurfaceBox::setSize(const TVec3f& pSize) {
    mSize.set(pSize);
}

void ShadowSurfaceBox::draw() const {
    MtxPtr baseMtx;
    ShadowController* controller = getController();

    if (!controller->isProjected() || !controller->isDraw()) {
        return;
    }

    TPos3f modelMtx;
    baseMtx = controller->_18;
    TVec3f side(baseMtx[0][0], baseMtx[1][0], baseMtx[2][0]);
    TVec3f up(baseMtx[0][1], baseMtx[1][1], baseMtx[2][1]);
    TVec3f front(baseMtx[0][2], baseMtx[1][2], baseMtx[2][2]);
    TVec3f normal;
    controller->getProjectionNormal(&normal);
    f32 sideDot = normal.dot(side);
    f32 upDot = normal.dot(up);
    f32 frontDot = normal.dot(front);
    TVec3f size(mSize);
    TVec3f position;

    if (controller->isFollowHostScale()) {
        TVec3f scale(controller->getHost()->mScale);
        scale /= 2.0f;
        size *= scale;
    } else {
        size /= 2.0f;
    }

    side -= normal * sideDot;
    up -= normal * upDot;
    front -= normal * frontDot;
    side.scale(size.x);
    up.scale(size.y);
    front.scale(size.z);
    modelMtx.setXYZDir(side, up, front);
    controller->getProjectionPos(&position);
    modelMtx.setTrans(position + normal);
    TDDraw::setModelMtx(modelMtx.toMtxPtr());

    sideDot = sideDot >= 0.0f ? 1.0f : -1.0f;
    upDot = upDot >= 0.0f ? 1.0f : -1.0f;
    frontDot = frontDot >= 0.0f ? 1.0f : -1.0f;

    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
    J3DNativeDraw::position(sideDot, -sideDot, -1.0f);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(sideDot, -sideDot, 1.0f);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(sideDot, sideDot, 1.0f);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(sideDot, sideDot, -1.0f);
    J3DNativeDraw::color(0x80);

    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
    J3DNativeDraw::position(-1.0f, upDot, -upDot);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(1.0f, upDot, -upDot);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(1.0f, upDot, upDot);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(-1.0f, upDot, upDot);
    J3DNativeDraw::color(0x80);

    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_COLOR0);
    J3DNativeDraw::position(-frontDot, -1.0f, frontDot);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(-frontDot, 1.0f, frontDot);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(frontDot, 1.0f, frontDot);
    J3DNativeDraw::color(0x80);
    J3DNativeDraw::position(frontDot, -1.0f, frontDot);
    J3DNativeDraw::color(0x80);
}
