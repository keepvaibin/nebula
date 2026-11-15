#include "Game/MapObj/SpiderThreadPart.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/SpiderThreadMainPoint.hpp"
#include "Game/MapObj/SpiderThreadPoint.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <NebulaGpuTypes.h>
#include "Game/System/NebulaInput.hpp"

namespace {
    static Color8 sColorPlusX(0x64, 0x64, 0x64, 0xFF);
    static Color8 sColorMinusX(0x96, 0x96, 0x96, 0xFF);
    static Color8 sColorMinusY(0xFF, 0xFF, 0xFF, 0xFF);

    const f32 sPointInterval = 50.0f;
    const f32 sFrictionLengthMin = 200.0f;
    const f32 sFrictionLengthMax = 700.0f;
    const s32 sTouchTimerMax = 5;
    static const s32 sActorTouchTimerMax = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sForceAttachTime = 20;
    const f32 sWindFrictionRate = 0.93f;
    const f32 sTexRateV = 0.002f;
    const f32 sIndirectTexRateV = 0.001f;
    const f32 sIndirectTexSpeed = -0.002f;
};  // namespace

SpiderThreadPart::SpiderThreadPart(SpiderThreadMainPoint* pPointA, SpiderThreadMainPoint* pPointB, f32 boundBoxPadding)
    : mIsStill(true), mIsCut(false), mIsFree(false), mNumHangingPoints(0), mStopTime(0), mTouchTimeout(0),
      mPointerPosPad0(0.0f, 0.0f, 0.0f), mPointerPosPad1(0.0f, 0.0f, 0.0f), mPointA(pPointA), mPointB(pPointB), mNumPoints(0), mPoints(nullptr),
      mSide(1.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mDistBetweenMainPoints(0.0f), mDistBetweenPoints(0.0f),
      mTextureOffset(0.0f) {
    Vec3f forward(mPointB->mPosition);
    forward.sub(mPointA->mPosition);
    mDistBetweenMainPoints = forward.length();
    mNumPoints = mDistBetweenMainPoints / ::sPointInterval;
    mDistBetweenPoints = mDistBetweenMainPoints / (mNumPoints + 1);
    MR::normalize(&forward);

    mPoints = new SpiderThreadPoint*[mNumPoints];

    for (s32 idx = 0; idx < mNumPoints; idx++) {
        Vec3f pos(forward);
        pos.scale(mDistBetweenPoints * (idx + 1));
        pos.add(mPointA->mPosition);

        s32 halfNumPoints = mNumPoints / 2;
        s32 index = MR::abs(halfNumPoints - idx);
        f32 indexNormalized = 1.0f - (static_cast< f32 >(index) / static_cast< f32 >(halfNumPoints));

        f32 length = mDistBetweenMainPoints - ::sFrictionLengthMin;
        if (length < 0.0f) {
            length = 0.0f;
        } else if (length > ::sFrictionLengthMax - ::sFrictionLengthMin) {
            length = ::sFrictionLengthMax - ::sFrictionLengthMin;
        }

        f32 lengthNormalized = length / (::sFrictionLengthMax - ::sFrictionLengthMin);

        // Friction runs from 0.9 at the ends to 1 in the middle; the length term blends 0.9 with 0.9.
        f32 indexSpring =
            ((0.9f * (1.0f - MR::getEaseOutValue(indexNormalized, 0.0f, 1.0f, 1.0f))) + MR::getEaseOutValue(indexNormalized, 0.0f, 1.0f, 1.0f));
        f32 lengthSpring = (0.9f * (1.0f - lengthNormalized) + (0.9f * lengthNormalized));
        f32 friction = indexSpring * lengthSpring;

        mPoints[idx] = new SpiderThreadPoint(pos, friction);
    }

    mSide.set(0.0f, 0.0f, -1.0f);
    mUp.set< f32 >(forward.y, -forward.x, 0.0f);
    mFront.set< f32 >(-forward.y, forward.x, 0.0f);

    mBoundBox.i.zero();
    mBoundBox.f.zero();
    mBoundBox.extend(mPointA->mPosition);
    mBoundBox.extend(mPointB->mPosition);

    padBox(boundBoxPadding);

    f32 minZ = mPointA->mPosition.z >= mPointB->mPosition.z ? mPointB->mPosition.z : mPointA->mPosition.z;
    Vec3f far(mBoundBox.i.x, mBoundBox.i.y, minZ - 1000.0f);

    f32 maxZ = mPointA->mPosition.z >= mPointB->mPosition.z ? mPointA->mPosition.z : mPointB->mPosition.z;
    Vec3f near(mBoundBox.f.x, mBoundBox.f.y, maxZ + 100.0f);
    mBoundBox.extend(far);
    mBoundBox.extend(near);
}

void SpiderThreadPart::cut(SpiderThreadMainPoint* pPointA, SpiderThreadMainPoint* pPointB) {
    if (pPointA == mPointA) {
        mPointA = mPointB;
        mPointB = pPointB;
    } else {
        mPointB = pPointB;
    }

    if (mIsCut) {
        mIsFree = true;
    }

    mIsCut = true;
    mIsStill = false;
}

void SpiderThreadPart::updatePointerPos() {
    if (mIsCut || mIsFree) {
        return;
    }

    Vec3f center(mPointA->mPosition);
    center.add(mPointB->mPosition);
    center.scale(0.5f);

    if (MR::isStarPointerInScreen(PLAYER_1)) {
        MR::calcStarPointerPosOnPlane(&mPointerPosPad0, center, Vec3f(0.0f, 0.0f, 1.0f), PLAYER_1, false);
    }

    if (MR::isStarPointerInScreen(PLAYER_2)) {
        MR::calcStarPointerPosOnPlane(&mPointerPosPad1, center, Vec3f(0.0f, 0.0f, 1.0f), PLAYER_2, false);
    }
}

void SpiderThreadPart::update() {
    if (mIsFree) {
        return;
    }

    mTextureOffset += ::sIndirectTexSpeed;

    if (mIsCut) {
        updateCutPoints();
        return;
    }

    if (mIsStill) {
        mTouchTimeout = 0;
        return;
    }

    if (mNumHangingPoints > 0) {
        updateDrawVec();
        return;
    }

    if (mTouchTimeout > 0) {
        mTouchTimeout--;
    }

    updateBasePos();

    if (mStopTime > 0) {
        updateForceAttach();
        updateDrawVec();
        return;
    }

    mIsStill = true;
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        if (!getPoint(idx)->updateSpring()) {
            mIsStill = false;
        }
    }

    if (!mIsStill) {
        updateDrawVec();
    }
}

void SpiderThreadPart::draw(TypedDraw& rDraw) const {
    if (mIsFree) {
        return;
    }

    if (mIsStill) {
        drawLine(rDraw);
        return;
    }

    drawPoints(rDraw);
}

void SpiderThreadPart::startHang(s32 index) {
    mNumHangingPoints++;
    mIsStill = false;
    mTouchTimeout = 0;
    mPointA->addNearPointToRadial(getPoint(index));
    if (mPointA->mRadialLine != mPointB->mRadialLine) {
        mPointB->addNearPointToRadial(getPoint(index));
    }
    updatePointPosHanging(index);
}

void SpiderThreadPart::updateHang(const Vec3f& rPos, s32 index) {
    getPoint(index)->updateHang(rPos);
    updatePointPosHanging(index);
}

void SpiderThreadPart::endHang(s32 cutIndex) {
    Vec3f front(mPointB->mPosition);
    front.sub(mPointA->mPosition);
    MR::normalize(&front);

    // Each other point moves toward the eased line from its end to the released point, across the thread only.
    SpiderThreadPoint* point = getPoint(cutIndex);

    for (s32 idx = 0; idx < cutIndex; idx++) {
        f32 t = MR::getEaseOutValue(static_cast< f32 >(idx + 1) / static_cast< f32 >(cutIndex + 1), 0.0f, 1.0f, 1.0f);

        Vec3f linePos;
        linePos.x = point->mPosition.x * t + (1.0f - t) * mPointA->mPosition.x;
        linePos.y = point->mPosition.y * t + (1.0f - t) * mPointA->mPosition.y;
        linePos.z = point->mPosition.z * t + (1.0f - t) * mPointA->mPosition.z;

        Vec3f offset(linePos);
        offset.sub(getPoint(idx)->mPosition);
        MR::vecKillElement(offset, front, &linePos);
        linePos.add(getPoint(idx)->mPosition);

        getPoint(idx)->setPos(linePos);
    }

    for (s32 idx = cutIndex + 1; idx < mNumPoints; idx++) {
        f32 t = MR::getEaseOutValue(static_cast< f32 >(mNumPoints - idx) / static_cast< f32 >(mNumPoints - cutIndex), 0.0f, 1.0f, 1.0f);

        Vec3f linePos;
        linePos.x = point->mPosition.x * t + (1.0f - t) * mPointB->mPosition.x;
        linePos.y = point->mPosition.y * t + (1.0f - t) * mPointB->mPosition.y;
        linePos.z = point->mPosition.z * t + (1.0f - t) * mPointB->mPosition.z;

        Vec3f offset(linePos);
        offset.sub(getPoint(idx)->mPosition);
        MR::vecKillElement(offset, front, &linePos);
        linePos.add(getPoint(idx)->mPosition);

        getPoint(idx)->setPos(linePos);
    }

    mPointA->removeNearPointFromRadial(point);
    if (mPointA->mRadialLine != mPointB->mRadialLine) {
        mPointB->removeNearPointFromRadial(point);
    }
    mIsStill = false;
    mNumHangingPoints--;
}

void SpiderThreadPart::stopAllPoints() {
    if (mIsStill) {
        return;
    }

    mStopTime = ::sForceAttachTime;
}

f32 SpiderThreadPart::calcNearestPointInfo(const Vec3f** pPos, const Vec3f** pNeutralPos, s32* pPointNum, const Vec3f& rPos) {
    if (mIsStill) {
        updatePointPosToLine();
    }
    f32 dist = findNearestPointNo(pPointNum, rPos);
    *pPos = &getPoint(*pPointNum)->mPosition;
    *pNeutralPos = &getPoint(*pPointNum)->mBasePos;
    return dist;
}

void SpiderThreadPart::updatePointPosToLine() {
    Vec3f front(mPointB->mPosition);
    front.sub(mPointA->mPosition);

    f32 delta = front.length() / (mNumPoints + 1);
    MR::normalize(&front);

    SpiderThreadPoint* point;
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        Vec3f pos(front);
        pos.scale(delta * (idx + 1));
        pos.add(mPointA->mPosition);

        point = getPoint(idx);
        point->setPos(pos);
    }
}

void SpiderThreadPart::updatePointPosAndBasePosToLine() {
    updatePointPosToLine();
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        getPoint(idx)->mBasePos.set(getPoint(idx)->mPosition);
    }
}

bool SpiderThreadPart::touchActor(s32 index, const Vec3f& rVel) {
    if (mTouchTimeout > 0) {
        return false;
    }

    // Only the part of rVel across the thread (up and front) moves it.
    f32 projUp = mUp.dot(rVel);
    f32 projFront = mFront.dot(rVel);
    Vec3f scaleUp(mUp);
    Vec3f scaleFront(mFront);
    scaleUp.scale(projUp);
    scaleFront.scale(projFront);
    Vec3f vel(scaleUp);
    vel.add(scaleFront);

    getPoint(index)->mVelocity.add(vel);
    updatePointVelocityTouch(index, vel);
    mIsStill = false;
    mTouchTimeout = ::sActorTouchTimerMax;

    return true;
}

void SpiderThreadPart::tryTouch(f32 radius, const Vec3f& rVel, s32 padChannel) {
    if (mIsCut || mIsFree) {
        return;
    }

    if (mTouchTimeout > 0) {
        return;
    }

    Vec3f* pointerPos = &mPointerPosPad0;
    if (padChannel == PLAYER_2) {
        pointerPos = &mPointerPosPad1;
    }

    if (mIsStill) {
        if (!mBoundBox.intersectsPoint(*pointerPos)) {
            return;
        }

        updatePointPosToLine();
    }

    s32 index = findTouchPoint(radius, rVel, padChannel);
    if (index >= 0) {
        updatePointVelocityTouch(index, rVel);
    }
}

s32 SpiderThreadPart::findTouchPoint(f32 radius, const Vec3f& rVel, s32 padChannel) {
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        if (getPoint(idx)->tryTouch(radius, rVel, padChannel)) {
            mTouchTimeout = ::sTouchTimerMax;
            mIsStill = false;
            return idx;
        }
    }
    return -1;
}

f32 SpiderThreadPart::findNearestPointNo(s32* pPointNum, const Vec3f& rPos) const {
    *pPointNum = 0;
    f32 minDistance = MR::calcDistanceXY(getPoint(0)->mPosition, rPos);

    f32 dist;
    for (s32 idx = 1; idx < mNumPoints; idx++) {
        dist = MR::calcDistanceXY(getPoint(idx)->mPosition, rPos);
        if (dist < minDistance) {
            *pPointNum = idx;
            minDistance = dist;
        }
    }

    return minDistance;
}

void SpiderThreadPart::updatePointPosHanging(s32 hangIndex) {
    SpiderThreadPoint* point = getPoint(hangIndex);

    for (s32 idx = 0; idx < hangIndex; idx++) {
        f32 t = static_cast< f32 >(idx + 1) / static_cast< f32 >(hangIndex + 1);

        Vec3f linePos;
        linePos.x = point->mPosition.x * t + (1.0f - t) * mPointA->mPosition.x;
        linePos.y = point->mPosition.y * t + (1.0f - t) * mPointA->mPosition.y;
        linePos.z = point->mPosition.z * t + (1.0f - t) * mPointA->mPosition.z;

        getPoint(idx)->setPos(linePos);
    }

    for (s32 idx = hangIndex + 1; idx < mNumPoints; idx++) {
        f32 t = static_cast< f32 >(mNumPoints - idx) / static_cast< f32 >(mNumPoints - hangIndex);

        Vec3f linePos;
        linePos.x = point->mPosition.x * t + (1.0f - t) * mPointB->mPosition.x;
        linePos.y = point->mPosition.y * t + (1.0f - t) * mPointB->mPosition.y;
        linePos.z = point->mPosition.z * t + (1.0f - t) * mPointB->mPosition.z;

        getPoint(idx)->setPos(linePos);
    }
}

void SpiderThreadPart::updatePointVelocityTouch(s32 hangIndex, const Vec3f& rVel) {
    for (s32 idx = 0; idx < hangIndex; idx++) {
        f32 t = MR::getEaseOutValue(static_cast< f32 >(idx + 1) / static_cast< f32 >(hangIndex + 1), 0.0f, 1.0f, 1.0f);

        getPoint(idx)->mVelocity.add(rVel * t);
    }

    for (s32 idx = hangIndex + 1; idx < mNumPoints; idx++) {
        f32 t = MR::getEaseOutValue(static_cast< f32 >(mNumPoints - idx) / static_cast< f32 >(mNumPoints - hangIndex), 0.0f, 1.0f, 1.0f);

        getPoint(idx)->mVelocity.add(rVel * t);
    }
}

void SpiderThreadPart::updateBasePos() {
    Vec3f front(mPointB->mPosition);
    front.sub(mPointA->mPosition);

    if (MR::isNearZero(front)) {
        return;
    }

    f32 delta = front.length() / (mNumPoints + 1);
    MR::normalize(&front);

    for (s32 idx = 0; idx < mNumPoints; idx++) {
        Vec3f pos(front);
        pos.scale((idx + 1) * delta);
        pos.add(mPointA->mPosition);
        getPoint(idx)->mBasePos.set(pos);
    }
}

void SpiderThreadPart::updateForceAttach() {
    if (--mStopTime <= 0) {
        mIsStill = true;
        return;
    }

    f32 t = mStopTime / static_cast< f32 >(::sForceAttachTime);
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        Vec3f vel(getPoint(idx)->mVelocity);
        getPoint(idx)->updateSpring();

        getPoint(idx)->setPos(getPoint(idx)->mPosition * t + getPoint(idx)->mBasePos * (1.0f - t));
        getPoint(idx)->mVelocity.set(vel);
    }
}

void SpiderThreadPart::updateCutPoints() {
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        getPoint(idx)->updateWind(::sWindFrictionRate);
    }

    Vec3f* pos = &mPointA->mPosition;
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        getPoint(idx)->restrict(pos, ::sPointInterval);
        pos = &getPoint(idx)->mPosition;
    }

    for (s32 idx = 0; idx < mNumPoints; idx++) {
        getPoint(idx)->updatePos();
    }

    Vec3f direction(getPoint(mNumPoints - 2)->mPosition);
    direction.sub(getPoint(mNumPoints - 1)->mPosition);

    if (!MR::isNearZero(direction)) {
        MR::normalize(&direction);
        direction.scale(::sPointInterval);
        direction.add(getPoint(mNumPoints - 1)->mPosition);
        mPointB->setPos(direction);
    }
}

void SpiderThreadPart::updateDrawVec() {
    Vec3f* pos = &mPointA->mPosition;
    for (s32 idx = 0; idx < mNumPoints - 1; idx++) {
        getPoint(idx)->updateDrawVec(pos, &getPoint(idx + 1)->mPosition);
        pos = &getPoint(idx)->mPosition;
    }

    getPoint(mNumPoints - 1)->updateDrawVec(pos, &mPointB->mPosition);
}

namespace {
    // One edge of the thread's cross-section: two corners, each `pos + s * side + f * front`, texture u 0 then 1.
    void sendEdge(VertexPCT2*& rVtx, const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rFront, f32 texY1, f32 texY2, f32 side0, f32 front0,
                  Rgba8 color0, f32 side1, f32 front1, Rgba8 color1) {
        const f32 s[2] = {side0, side1};
        const f32 f[2] = {front0, front1};
        const Rgba8 color[2] = {color0, color1};
        for (int i = 0; i < 2; i++) {
            const f32 u = static_cast< f32 >(i);
            *rVtx++ = {rPos.x + s[i] * rSide.x + f[i] * rFront.x,
                       rPos.y + s[i] * rSide.y + f[i] * rFront.y,
                       rPos.z + s[i] * rSide.z + f[i] * rFront.z,
                       color[i],
                       u,
                       texY1,
                       u,
                       texY2};
        }
    }

    void sendPointsUpper(VertexPCT2*& rVtx, const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rFront, f32 texY1, f32 texY2) {
        sendEdge(rVtx, rPos, rSide, rFront, texY1, texY2, -18.0f, 3.0f, sColorMinusX, 18.0f, 3.0f, sColorPlusX);
    }

    void sendPointsRightLower(VertexPCT2*& rVtx, const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rFront, f32 texY1, f32 texY2) {
        sendEdge(rVtx, rPos, rSide, rFront, texY1, texY2, 18.0f, 18.0f, sColorPlusX, -15.0f, -18.0f, sColorMinusY);
    }

    void sendPointsLeftLower(VertexPCT2*& rVtx, const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rFront, f32 texY1, f32 texY2) {
        sendEdge(rVtx, rPos, rSide, rFront, texY1, texY2, 15.0f, -18.0f, sColorMinusY, -18.0f, 18.0f, sColorMinusX);
    }
};  // namespace

void SpiderThreadPart::drawLine(TypedDraw& rDraw) const {
    VertexPCT2* vtx;
    vtx = rDraw.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
    {
        ::sendPointsUpper(vtx, mPointA->mPosition, mSide, mUp, 0.0f, mTextureOffset);
        ::sendPointsUpper(vtx, mPointB->mPosition, mSide, mUp, mDistBetweenMainPoints * ::sTexRateV,
                          mTextureOffset + mDistBetweenMainPoints * ::sIndirectTexRateV);
    }

    vtx = rDraw.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
    {
        ::sendPointsRightLower(vtx, mPointA->mPosition, mSide, mUp, 0.0f, mTextureOffset);
        ::sendPointsRightLower(vtx, mPointB->mPosition, mSide, mUp, mDistBetweenMainPoints * ::sTexRateV,
                               mTextureOffset + mDistBetweenMainPoints * ::sIndirectTexRateV);
    }

    vtx = rDraw.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
    {
        ::sendPointsLeftLower(vtx, mPointA->mPosition, mSide, mUp, 0.0f, mTextureOffset);
        ::sendPointsLeftLower(vtx, mPointB->mPosition, mSide, mUp, mDistBetweenMainPoints * ::sTexRateV,
                              mTextureOffset + mDistBetweenMainPoints * ::sIndirectTexRateV);
    }
}

void SpiderThreadPart::drawPoints(TypedDraw& rDraw) const {
    VertexPCT2* vtx;
    f32 offset2, offset1;
    f32 delta1 = (mDistBetweenMainPoints * ::sTexRateV) / (mNumPoints + 1);
    f32 delta2 = (mDistBetweenMainPoints * ::sIndirectTexRateV) / (mNumPoints + 1);

    vtx = rDraw.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (mNumPoints + 2) * 2);
    {
        ::sendPointsUpper(vtx, mPointA->mPosition, mSide, mUp, 0.0f, mTextureOffset);

        offset1 = delta1;
        offset2 = delta2 + mTextureOffset;
        for (s32 idx = 0; idx < mNumPoints; idx++) {
            ::sendPointsUpper(vtx, getPoint(idx)->mPosition, mSide, getPoint(idx)->mUp, offset1, offset2);
            offset1 += delta1;
            offset2 += delta2;
        }

        ::sendPointsUpper(vtx, mPointB->mPosition, mSide, getPoint(mNumPoints - 1)->mUp, mDistBetweenMainPoints * ::sTexRateV,
                          mTextureOffset + mDistBetweenMainPoints * ::sIndirectTexRateV);
    }

    vtx = rDraw.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (mNumPoints + 2) * 2);
    {
        ::sendPointsRightLower(vtx, mPointA->mPosition, mSide, mUp, 0.0f, mTextureOffset);
        offset1 = delta1;
        offset2 = delta2 + mTextureOffset;
        for (s32 idx = 0; idx < mNumPoints; idx++) {
            ::sendPointsRightLower(vtx, getPoint(idx)->mPosition, mSide, getPoint(idx)->mUp, offset1, offset2);
            offset1 += delta1;
            offset2 += delta2;
        }

        ::sendPointsRightLower(vtx, mPointB->mPosition, mSide, getPoint(mNumPoints - 1)->mUp, mDistBetweenMainPoints * ::sTexRateV,
                               mTextureOffset + mDistBetweenMainPoints * ::sIndirectTexRateV);
    }

    vtx = rDraw.vertices< VertexPCT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (mNumPoints + 2) * 2);
    {
        ::sendPointsLeftLower(vtx, mPointA->mPosition, mSide, mUp, 0.0f, mTextureOffset);
        offset1 = delta1;
        offset2 = delta2 + mTextureOffset;
        for (s32 idx = 0; idx < mNumPoints; idx++) {
            ::sendPointsLeftLower(vtx, getPoint(idx)->mPosition, mSide, getPoint(idx)->mUp, offset1, offset2);
            offset1 += delta1;
            offset2 += delta2;
        }

        ::sendPointsLeftLower(vtx, mPointB->mPosition, mSide, getPoint(mNumPoints - 1)->mUp, mDistBetweenMainPoints * ::sTexRateV,
                              mTextureOffset + mDistBetweenMainPoints * ::sIndirectTexRateV);
    }
}
