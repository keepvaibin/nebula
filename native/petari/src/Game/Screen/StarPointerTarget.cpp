#include "Game/Screen/StarPointerTarget.hpp"
#include "Game/Util/Array.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcRadius2d_f1, "calcRadius2d.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRadius2d_f2, "calcRadius2d.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPositionFromView_f1, "calcScreenPositionFromView.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPositionFromView_f2, "calcScreenPositionFromView.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPositionFromView_f3, "calcScreenPositionFromView.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScreenPositionFromView_f4, "calcScreenPositionFromView.f4", 0.5f);

namespace {
    NEBULA_TUNABLE(f32, hZCheckAddDepth, 60.0f);

    inline void multProj(const Proj3f& rMtx, const Vec3f& rSrc, Vec3f& rDest) {
        f32 w = -rSrc.z;
        s32 col = 3;
        Vec4f pos(rSrc.x * rMtx.mMtx[0][0] + rSrc.z * rMtx.mMtx[0][2], rSrc.y * rMtx.mMtx[1][1] + rSrc.z * rMtx.mMtx[1][2],
                   rSrc.z * rMtx.mMtx[2][2] + rMtx.mMtx[2][col], w);
        rDest.scale(1.0f / pos.w, *pos.toVec3());
    }
};  // namespace

StarPointerTarget::StarPointerTarget(f32 radius, const Vec3f* pPosition, MtxPtr pMtx, Vec3f offset)
    : mRadius3d(radius), mOffset(offset), mLastPointedChannel(PLAYER_1), mPosition(pPosition), mMtx(pMtx) {
}

bool StarPointerTarget::calcScreenPosition(Vec2f* pScreenPos) const {
    Vec3f worldPos, viewPos;
    calcPosition(&worldPos);
    NebulaMtxMultVec(MR::getStarPointerViewMtx(), &worldPos, &viewPos);
    return calcScreenPositionFromView(pScreenPos, viewPos);
}

bool StarPointerTarget::isPointing(const Vec2f& rPointerPos, f32 zMargin, f32 radiusMargin) const {
    Vec3f worldPos, viewPos;
    calcPosition(&worldPos);
    NebulaMtxMultVec(MR::getStarPointerViewMtx(), &worldPos, &viewPos);

    if (-viewPos.z <= 0.0f) {
        return false;
    }

    if (zMargin + mRadius3d + ::hZCheckAddDepth < -viewPos.z) {
        return false;
    }

    f32 radius = calcRadius2d(mRadius3d, radiusMargin, -viewPos.z);
    Vec2f screenPos;
    if (!calcScreenPositionFromView(&screenPos, viewPos)) {
        return false;
    }

    if (rPointerPos.squared(screenPos) < radius * radius) {
        return true;
    }

    return false;
}

void StarPointerTarget::calcPosition(Vec3f* pWorldPos) const {
    if (mMtx != nullptr) {
        if (mPosition != nullptr) {
            pWorldPos->set< f32 >(mPosition->x, mPosition->y, mPosition->z);
        } else {
            pWorldPos->set< f32 >(mMtx[0][3], mMtx[1][3], mMtx[2][3]);
        }

        pWorldPos->x += mMtx[0][0] * mOffset.x + mMtx[0][1] * mOffset.y + mMtx[0][2] * mOffset.z;
        pWorldPos->y += mMtx[1][0] * mOffset.x + mMtx[1][1] * mOffset.y + mMtx[1][2] * mOffset.z;
        pWorldPos->z += mMtx[2][0] * mOffset.x + mMtx[2][1] * mOffset.y + mMtx[2][2] * mOffset.z;
    } else {
        pWorldPos->set< f32 >(mPosition->x, mPosition->y, mPosition->z);
        pWorldPos->add(mOffset);
    }
}

f32 StarPointerTarget::calcRadius2d(f32 radius3d, f32 margin, f32 z) const {
    f32 fovyRad = MR::getRenderFovy() * PI_180;
    f32 radius2d = (radius3d * ((MR::getScreenHeight() * cNum_calcRadius2d_f1) / MR::tan(fovyRad * cNum_calcRadius2d_f2)) / z);
    return radius2d + margin;
}

bool StarPointerTarget::calcScreenCircle(Vec2f* pScreenPos, f32* pRadius) const {
    Vec3f worldPos, viewPos;
    calcPosition(&worldPos);
    NebulaMtxMultVec(MR::getStarPointerViewMtx(), &worldPos, &viewPos);
    if (-viewPos.z <= 0.0f || !calcScreenPositionFromView(pScreenPos, viewPos)) {
        return false;
    }
    *pRadius = calcRadius2d(mRadius3d, 0.0f, -viewPos.z);
    return true;
}

bool StarPointerTarget::calcScreenPositionFromView(Vec2f* pScreenPos, const Vec3f& rViewPos) const {
    Proj3f mtx;
    mtx.setInline(MR::getStarPointerProjMtx());
    Vec3f pos;
    multProj(mtx, rViewPos, pos);
    pos.y = -pos.y;

    if (1.0f < MR::abs(pos.x) || 1.0f < MR::abs(pos.y)) {
        return false;
    }

    if (0.0f < pos.z) {
        return false;
    }

    f32 width = MR::getScreenWidth();
    f32 height = MR::getScreenHeight();
    pScreenPos->set< f32 >(width * cNum_calcScreenPositionFromView_f1 + pos.x * width * cNum_calcScreenPositionFromView_f2, height * cNum_calcScreenPositionFromView_f3 + pos.y * height * cNum_calcScreenPositionFromView_f4);
    return true;
}

StarPointerLayoutTarget::StarPointerLayoutTarget(LayoutActor* pLayoutActor, const char* pLayoutName, const char* pPaneName)
    : mLayout(pLayoutActor), mLayoutName(pLayoutName), mPaneName(pPaneName), mType(TargetType_Circle), mPosition(0.0f, 0.0f), mRadius(0.0f),
      mWidth(0.0f), mHeight(0.0f) {
}

bool StarPointerLayoutTarget::isPointing(const Vec2f& rPos) const {
    switch (mType) {
    case TargetType_Circle:
        return isPointingCircle(rPos);
    case TargetType_Rectangle:
        return isPointingRectangle(rPos);
    default:
        return false;
    }
}

bool StarPointerLayoutTarget::isPointingCircle(const Vec2f& rPos) const {
    Vec2f pos;
    calcBasePos(&pos);
    f32 r = mRadius;
    r *= r;
    f32 dist = pos.squared(rPos);
    return dist < r;
}

namespace {
    inline void setRectangle(Box2f& rBox, f32 x, f32 y, f32 width, f32 height) {
        rBox.setInline(x, y, x + width, y + height);
    }
};  // namespace

bool StarPointerLayoutTarget::isPointingRectangle(const Vec2f& rPos) const {
    Vec2f pos;
    calcBasePos(&pos);

    Box2f bounds;
    setRectangle(bounds, pos.x, pos.y, mWidth, mHeight);

    return bounds.intersectsPoint(rPos);
}

void StarPointerLayoutTarget::calcBasePos(Vec2f* pPos) const {
    if (mPaneName == nullptr) {
        pPos->y = 0.0f;
        pPos->x = 0.0f;
    } else {
        MR::convertPaneLocalPosToScreenPos(pPos, MR::getPane(mLayout, mPaneName), Vec2f(0.0f, 0.0f));
    }

    pPos->x += mPosition.x;
    pPos->y += mPosition.y;
}

StarPointerLayoutTargetKeeper::StarPointerLayoutTargetKeeper(int maxNumTargets) : mNumTargets(), mMaxNumTargets(maxNumTargets), mTargets() {
    mTargets = new StarPointerLayoutTarget*[mMaxNumTargets];
    memset(mTargets, 0, mMaxNumTargets * sizeof(StarPointerLayoutTarget*));
}

void StarPointerLayoutTargetKeeper::addTargetCircle(LayoutActor* pLayoutActor, const char* pLayoutName, f32 radius, const Vec2f& rPosition,
                                                    const char* pPaneName) {
    StarPointerLayoutTarget* target = new StarPointerLayoutTarget(pLayoutActor, pLayoutName, pPaneName != nullptr ? pPaneName : pLayoutName);
    target->mType = StarPointerLayoutTarget::TargetType_Circle;
    target->mRadius = radius;
    target->mPosition.set(rPosition);
    registerTarget(target);
}

StarPointerLayoutTarget* StarPointerLayoutTargetKeeper::getTarget(const char* pLayoutName) const {
    for (s32 idx = 0; idx < mNumTargets; idx++) {
        StarPointerLayoutTarget* target = mTargets[idx];
        if (MR::isEqualString(pLayoutName, target->mLayoutName)) {
            return target;
        }
    }
    return nullptr;
}

void StarPointerLayoutTargetKeeper::registerTarget(StarPointerLayoutTarget* pTarget) {
    if (mNumTargets == mMaxNumTargets) {
        mTargets = MR::growArray(mTargets, mMaxNumTargets);
        mMaxNumTargets *= 2;
    }

    mTargets[mNumTargets] = pTarget;
    mNumTargets++;
}
