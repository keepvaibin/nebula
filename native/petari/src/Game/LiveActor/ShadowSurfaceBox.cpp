#include "Game/Util/CameraUtil.hpp"
#include "Game/LiveActor/ShadowSurfaceBox.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/Util/DirectDraw.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 2.0f);

ShadowSurfaceBox::~ShadowSurfaceBox() {
}

ShadowSurfaceBox::ShadowSurfaceBox() : ShadowSurfaceDrawer("影描画[水面ボックス]"), mSize(100.0f) {
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

    beginDrawObject();
    Pos3f modelMtx;
    baseMtx = controller->mHostMtx;
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
        scale /= cNum_draw_f1;
        size *= scale;
    } else {
        size /= cNum_draw_f2;
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
    // The faces' corners flip to the far side when the normal crosses an axis: each of the eight arrangements has numbers of
    // its own (three quads), so a flip shows the new faces at once and never blends the old corners into them.
    rDraw.setPart(((sideDot > 0.0f ? 1u : 0u) | (upDot > 0.0f ? 2u : 0u) | (frontDot > 0.0f ? 4u : 0u)) * 3u);

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
