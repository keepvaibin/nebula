#include "Game/LiveActor/ShadowVolumeLine.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"

ShadowVolumeLine::ShadowVolumeLine() : ShadowVolumeDrawer("影描画[ボリュームライン]") {
    mFromShadowController = 0;
    mToShadowController = 0;
    mFromWidth = 100.0f;
    mToWidth = 100.0f;
}

void ShadowVolumeLine::loadModelDrawMtx(TypedDraw& rDraw) const {
    rDraw.setWorld(nullptr);
}

void ShadowVolumeLine::drawShape(TypedDraw& rDraw) const {
    const ShadowController* fromController = mFromShadowController;
    const ShadowController* toController = mToShadowController;
    Vec3f points[8];
    Vec3f from;
    Vec3f to;
    calcBaseDropPosition(&from, fromController);
    calcBaseDropPosition(&to, toController);
    Vec3f direction;
    MR::normalizeOrZero(to - from, &direction);
    if (MR::isNearZero(direction)) {
        return;
    }

    Vec3f fromDrop;
    Vec3f toDrop;
    fromController->getDropDir(&fromDrop);
    toController->getDropDir(&toDrop);
    Vec3f fromSide;
    fromSide.cross(fromDrop, direction);
    if (MR::isNearZero(fromSide)) {
        return;
    }

    MR::normalize(&fromSide);
    Vec3f toSide;
    toSide.cross(toDrop, direction);
    if (MR::isNearZero(toSide)) {
        return;
    }

    MR::normalize(&toSide);
    f32 fromLength = mFromWidth + calcBaseDropLength(fromController);
    f32 toLength = mToWidth + calcBaseDropLength(toController);
    points[0].scaleAdd(-mFromWidth, fromSide, from);
    points[1].scaleAdd(mFromWidth, fromSide, from);
    points[2].scaleAdd(fromLength, fromDrop, points[0]);
    points[3].scaleAdd(fromLength, fromDrop, points[1]);
    points[4].scaleAdd(-mToWidth, toSide, to);
    points[5].scaleAdd(mToWidth, toSide, to);
    points[6].scaleAdd(toLength, toDrop, points[4]);
    points[7].scaleAdd(toLength, toDrop, points[5]);

    VertexP* vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_QUADS, 4);
    *vtx++ = {points[1].x, points[1].y, points[1].z};
    *vtx++ = {points[5].x, points[5].y, points[5].z};
    *vtx++ = {points[7].x, points[7].y, points[7].z};
    *vtx++ = {points[3].x, points[3].y, points[3].z};

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_QUADS, 4);
    *vtx++ = {points[0].x, points[0].y, points[0].z};
    *vtx++ = {points[2].x, points[2].y, points[2].z};
    *vtx++ = {points[6].x, points[6].y, points[6].z};
    *vtx++ = {points[4].x, points[4].y, points[4].z};

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 10);
    *vtx++ = {points[0].x, points[0].y, points[0].z};
    *vtx++ = {points[1].x, points[1].y, points[1].z};
    *vtx++ = {points[2].x, points[2].y, points[2].z};
    *vtx++ = {points[3].x, points[3].y, points[3].z};
    *vtx++ = {points[6].x, points[6].y, points[6].z};
    *vtx++ = {points[7].x, points[7].y, points[7].z};
    *vtx++ = {points[4].x, points[4].y, points[4].z};
    *vtx++ = {points[5].x, points[5].y, points[5].z};
    *vtx++ = {points[0].x, points[0].y, points[0].z};
    *vtx++ = {points[1].x, points[1].y, points[1].z};
}

// The line stands between two shadows' hosts: either host renewed starts a new object.
u32 ShadowVolumeLine::calcDrawLives() const {
    u32 lives = ShadowVolumeDrawer::calcDrawLives();
    if (mFromShadowController != nullptr) {
        lives = lives * 61u + mFromShadowController->getHost()->mMotionEpoch;
    }

    if (mToShadowController != nullptr) {
        lives = lives * 61u + mToShadowController->getHost()->mMotionEpoch;
    }

    return lives;
}

void ShadowVolumeLine::setFromShadowController(const ShadowController* pController) {
    mFromShadowController = pController;
}

void ShadowVolumeLine::setToShadowController(const ShadowController* pController) {
    mToShadowController = pController;
}

void ShadowVolumeLine::setFromWidth(f32 width) {
    mFromWidth = width;
}

void ShadowVolumeLine::setToWidth(f32 width) {
    mToWidth = width;
}

ShadowVolumeLine::~ShadowVolumeLine() {
}
