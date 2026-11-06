#include "Game/Util/CameraUtil.hpp"
#include "Game/LiveActor/ShadowSurfaceBox.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/Util/DirectDraw.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>

ShadowSurfaceBox::~ShadowSurfaceBox() {
}

ShadowSurfaceBox::ShadowSurfaceBox() : ShadowSurfaceDrawer("\211\145\225\140\211\346[\220\205\226\312\203\173\203\142\203\116\203\130]" /* 影描画[水面ボックス] */), mSize(100.0f) {
}

void ShadowSurfaceBox::setSize(const Vec3f& pSize) {
    mSize.set(pSize);
}

void ShadowSurfaceBox::draw() const {
    MtxPtr baseMtx;
    ShadowController* controller = getController();

    if (!controller->isProjected() || !controller->isDraw()) {
        return;
    }

    Pos3f modelMtx;
    baseMtx = controller->_18;
    Vec3f side(baseMtx[0][0], baseMtx[1][0], baseMtx[2][0]);
    Vec3f up(baseMtx[0][1], baseMtx[1][1], baseMtx[2][1]);
    Vec3f front(baseMtx[0][2], baseMtx[1][2], baseMtx[2][2]);
    Vec3f normal;
    controller->getProjectionNormal(&normal);
    f32 sideDot = normal.dot(side);
    f32 upDot = normal.dot(up);
    f32 frontDot = normal.dot(front);
    Vec3f size(mSize);
    Vec3f position;

    if (controller->isFollowHostScale()) {
        Vec3f scale(controller->getHost()->mScale);
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
    TypedDraw rDraw(ShadowSurfaceDrawInit::material(), modelMtx.toMtxPtr(), MR::getCameraViewMtx());

    sideDot = sideDot >= 0.0f ? 1.0f : -1.0f;
    upDot = upDot >= 0.0f ? 1.0f : -1.0f;
    frontDot = frontDot >= 0.0f ? 1.0f : -1.0f;

    VertexPC* vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
    *vtx++ = {sideDot, -sideDot, -1.0f, Rgba8(0x80)};
    *vtx++ = {sideDot, -sideDot, 1.0f, Rgba8(0x80)};
    *vtx++ = {sideDot, sideDot, 1.0f, Rgba8(0x80)};
    *vtx++ = {sideDot, sideDot, -1.0f, Rgba8(0x80)};

    vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
    *vtx++ = {-1.0f, upDot, -upDot, Rgba8(0x80)};
    *vtx++ = {1.0f, upDot, -upDot, Rgba8(0x80)};
    *vtx++ = {1.0f, upDot, upDot, Rgba8(0x80)};
    *vtx++ = {-1.0f, upDot, upDot, Rgba8(0x80)};

    vtx = rDraw.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
    *vtx++ = {-frontDot, -1.0f, frontDot, Rgba8(0x80)};
    *vtx++ = {-frontDot, 1.0f, frontDot, Rgba8(0x80)};
    *vtx++ = {frontDot, 1.0f, frontDot, Rgba8(0x80)};
    *vtx++ = {frontDot, -1.0f, frontDot, Rgba8(0x80)};
}
