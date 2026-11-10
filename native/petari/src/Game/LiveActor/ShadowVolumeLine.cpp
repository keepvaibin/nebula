#include "Game/LiveActor/ShadowVolumeLine.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ShadowVolumeLine_f1, "ShadowVolumeLine.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_ShadowVolumeLine_f2, "ShadowVolumeLine.f2", 100.0f);

ShadowVolumeLine::ShadowVolumeLine() : ShadowVolumeDrawer("影描画[ボリュームライン]") {
    mFromShadowController = 0;
    mToShadowController = 0;
    mFromWidth = cNum_ShadowVolumeLine_f1;
    mToWidth = cNum_ShadowVolumeLine_f2;
    mLastFromSide.zero();
    mLastToSide.zero();
    mSideFlips = 0;
    mHasSides = false;
}

// The ends of the line, their drop directions and the side vectors the quads stand on (drop x direction). False when
// the line has no length or runs along a drop direction: nothing is drawn.
bool ShadowVolumeLine::calcLine(Vec3f* pFrom, Vec3f* pTo, Vec3f* pFromDrop, Vec3f* pToDrop, Vec3f* pFromSide, Vec3f* pToSide) const {
    const ShadowController* fromController = mFromShadowController;
    const ShadowController* toController = mToShadowController;
    calcBaseDropPosition(pFrom, fromController);
    calcBaseDropPosition(pTo, toController);
    Vec3f direction;
    MR::normalizeOrZero(*pTo - *pFrom, &direction);
    if (MR::isNearZero(direction)) {
        return false;
    }

    fromController->getDropDir(pFromDrop);
    toController->getDropDir(pToDrop);
    pFromSide->cross(*pFromDrop, direction);
    if (MR::isNearZero(*pFromSide)) {
        return false;
    }

    MR::normalize(pFromSide);
    pToSide->cross(*pToDrop, direction);
    if (MR::isNearZero(*pToSide)) {
        return false;
    }

    MR::normalize(pToSide);
    return true;
}

void ShadowVolumeLine::loadModelDrawMtx(TypedDraw& rDraw) const {
    rDraw.setWorld(nullptr);
    // A side vector is the cross product of the drop direction and the line: when the line swings through the drop
    // direction it turns half a circle and the quads' corners trade places. Changing the sign would turn the quads
    // inside out (the stencil counts front and back faces), so the shape stays as built and the draws of a flipped side
    // take numbers of their own: a generated frame never blends a corner into its opposite.
    Vec3f from, to, fromDrop, toDrop, fromSide, toSide;
    if (!calcLine(&from, &to, &fromDrop, &toDrop, &fromSide, &toSide)) {
        return;
    }

    if (mHasSides && (fromSide.dot(mLastFromSide) < 0.0f || toSide.dot(mLastToSide) < 0.0f)) {
        mSideFlips++;
    }

    mLastFromSide = fromSide;
    mLastToSide = toSide;
    mHasSides = true;
}

void ShadowVolumeLine::drawShape(TypedDraw& rDraw) const {
    Vec3f points[8];
    Vec3f from, to, fromDrop, toDrop, fromSide, toSide;
    if (!calcLine(&from, &to, &fromDrop, &toDrop, &fromSide, &toSide)) {
        return;
    }

    f32 fromLength = mFromWidth + calcBaseDropLength(mFromShadowController);
    f32 toLength = mToWidth + calcBaseDropLength(mToShadowController);
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
