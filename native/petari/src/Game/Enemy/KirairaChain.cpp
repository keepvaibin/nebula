#include "Game/Enemy/KirairaChain.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/SimpleModelDrawer.hpp"
#include "Game/Ride/SwingRopePoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_KirairaChain_f1, "KirairaChain.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_KirairaChain_f2, "KirairaChain.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_KirairaChain_f3, "KirairaChain.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_KirairaChain_f4, "KirairaChain.f4", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KirairaChain_f5, "KirairaChain.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f1, "updatePoints.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f2, "updatePoints.f2", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f3, "updatePoints.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f4, "updatePoints.f4", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f5, "updatePoints.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f6, "updatePoints.f6", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f7, "updatePoints.f7", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f8, "updatePoints.f8", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f9, "updatePoints.f9", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f10, "updatePoints.f10", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f11, "updatePoints.f11", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f12, "updatePoints.f12", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f13, "updatePoints.f13", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f14, "updatePoints.f14", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f15, "updatePoints.f15", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f16, "updatePoints.f16", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoints_f17, "updatePoints.f17", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f4, "control.f4", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f5, "control.f5", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f6, "control.f6", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromTop_f1, "restrictPointFromTop.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromTop_f2, "restrictPointFromTop.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromBottom_f1, "restrictPointFromBottom.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromBottom_f2, "restrictPointFromBottom.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromTopAndCalcAxis_f1, "restrictPointFromTopAndCalcAxis.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromTopAndCalcAxis_f2, "restrictPointFromTopAndCalcAxis.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromBottomAndCalcAxis_f1, "restrictPointFromBottomAndCalcAxis.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_restrictPointFromBottomAndCalcAxis_f2, "restrictPointFromBottomAndCalcAxis.f2", 40.0f);

KirairaChain::KirairaChain(const LiveActor* pHost)
    : LiveActor("キライラの鎖"), mHost(pHost), mIsCut(), mCutPos(0.0f, 0.0f, 0.0f), mRailStart(0.0f, 0.0f, 0.0f), mRailEnd(0.0f, 0.0f, 0.0f),
      mFixPointTopDir(1.0f, 0.0f, 0.0f), mFixPointBottomDir(1.0f, 0.0f, 0.0f), mPointCount(), mPoints(), mAccelPointIdx(),
      mAccelTimer(), mAccel(0.0f, 0.0f, 0.0f), mClippingCenter(0.0f, 0.0f, 0.0f), mModelDrawer(), mFixPointTop(),
      mFixPointBottom() {
    mFixPointTopMtx.identity();
    mFixPointBottomMtx.identity();

    mPosition.set(pHost->mPosition);
    mRailStart.set(mPosition);
    mRailEnd.set(mPosition);

    f32 railLength;

    if (MR::isExistRail(mHost)) {
        Vec3f startDir;
        Vec3f endDir;
        MR::calcRailStartPointPosAndDirection(&mRailStart, &startDir, mHost);
        MR::calcRailEndPointPosAndDirection(&mRailEnd, &endDir, mHost);
        mPosition.set(mRailEnd);

        railLength = MR::getRailTotalLength(mHost);

        Vec3f up(0.0f, 0.0f, 1.0f);
        MR::makeMtxUpFront(&mFixPointTopMtx, startDir, up);
        endDir.scale(-1.0f);
        MR::makeMtxUpFront(&mFixPointBottomMtx, endDir, up);
        mFixPointTopMtx.setTrans(mRailStart);
        mFixPointBottomMtx.setTrans(mRailEnd);

        mClippingCenter.set(mRailStart);
        mClippingCenter.add(mRailEnd);
        mClippingCenter.x *= cNum_KirairaChain_f1;
        mClippingCenter.y *= cNum_KirairaChain_f2;
        mClippingCenter.z *= cNum_KirairaChain_f3;

        MR::setClippingTypeSphere(this, mClippingCenter.distance(mRailStart), &mClippingCenter);
    } else {
        mPosition.y -= cNum_KirairaChain_f4;
        railLength = mHost->mPosition.distance(mPosition);
        mFixPointBottomMtx.setTrans(mPosition);
    }

    mPointCount = railLength / cNum_KirairaChain_f5;

    if (!MR::isExistRail(mHost)) {
        mPointCount -= 2;
    }

    // A chain is at least two points (a short rail would give none or a divide by zero in initPoints).
    if (mPointCount < 2) {
        mPointCount = 2;
    }

    mFixPointTopMtx.getXDir(mFixPointTopDir);
    mFixPointBottomMtx.getXDir(mFixPointBottomDir);
}

void KirairaChain::initPoints() {
    mPoints = new SwingRopePoint*[mPointCount];

    for (s32 i = 0; i < mPointCount; i++) {
        f32 t = static_cast< f32 >(i) / (mPointCount - 1);
        Vec3f point;

        if (MR::isExistRail(mHost)) {
            point = mRailStart * (1.0f - t) + mRailEnd * t;
        } else {
            point = mPosition * (1.0f - t) + mHost->mPosition * t;
        }

        mPoints[i] = new SwingRopePoint(point);
    }
}

void KirairaChain::draw() const {
    MaterialDraw material(mModelDrawer->mMaterial);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    Pos3f mtx;
    mtx.identity();

    for (s32 i = 0; i < mPointCount; i++) {
        SwingRopePoint* point = mPoints[i];

        mtx.setXYZDir(point->mSide, point->mUp, point->mFront);

        mtx.setTrans(point->mPosition);

        site.setWorld(mtx);
        material.drawUnder(site, mModelDrawer->mShape);
    }
}

void KirairaChain::updatePoints() {
    Vec3f accel(mGravity);

    if (MR::isExistRail(mHost)) {
        accel.scale(cNum_updatePoints_f1);
    } else {
        accel.scale(cNum_updatePoints_f2);
    }

    for (s32 i = 0; i < mPointCount; i++) {
        mPoints[i]->addAccel(accel);
    }

    if (MR::isExistRail(mHost)) {
        f32 offset = cNum_updatePoints_f3;
        if (mIsCut) {
            offset = 0.0f;
        }

        s32 lowIdx = MR::clamp(static_cast< s32 >((MR::getRailCoord(mHost) - offset) / cNum_updatePoints_f4), 0, mPointCount - 1);
        s32 highIdx = MR::clamp(static_cast< s32 >((offset + MR::getRailCoord(mHost)) / cNum_updatePoints_f5), 0, mPointCount - 1);

        Vec3f posA;
        MR::calcRailPosAtCoord(&posA, mHost, cNum_updatePoints_f6 * (lowIdx + 1));
        Vec3f posB;
        MR::calcRailPosAtCoord(&posB, mHost, cNum_updatePoints_f7 * highIdx);

        if (lowIdx + 1 < highIdx - 1) {
            restrictPointFromBottom(lowIdx + 1, highIdx - 1, posB, 1.0f);
            restrictPointFromTopAndCalcAxis(lowIdx + 1, highIdx - 1, posA, cNum_updatePoints_f8);
        }

        if (!mIsCut && lowIdx < mPointCount - 1) {
            s32 idx = lowIdx + 1;
            posA.set(mPoints[idx]->mPosition);
        }

        if (lowIdx > 0) {
            restrictPointFromBottom(0, lowIdx, posA, 1.0f);
            restrictPointFromTopAndCalcAxis(0, lowIdx, mRailStart, cNum_updatePoints_f9);
        }

        if (lowIdx > 0 && lowIdx < mPointCount - 1) {
            s32 nextIdx = lowIdx + 1;
            mPoints[lowIdx]->restrict(mPoints[nextIdx]->mPosition, cNum_updatePoints_f10, nullptr);
            s32 prevIdx = lowIdx - 1;
            mPoints[lowIdx]->updatePosAndAxis(mPoints[prevIdx]->mSide, cNum_updatePoints_f11);
        }

        if (!mIsCut && highIdx > 0) {
            s32 idx = highIdx - 1;
            posB.set(mPoints[idx]->mPosition);
        }

        s32 lastPointIdx = mPointCount - 1;
        if (highIdx < lastPointIdx) {
            restrictPointFromTop(highIdx, lastPointIdx, posB, 1.0f);
            restrictPointFromBottomAndCalcAxis(highIdx, lastPointIdx, mRailEnd, cNum_updatePoints_f12);
        }

        if (mIsCut) {
            s32 lastIdx = mPointCount - 1;
            if (lowIdx == lastIdx) {
                mCutPos.set(mPoints[lastIdx]->mPosition);
            } else if (highIdx == 0) {
                mCutPos.set(mPoints[0]->mPosition);
            } else {
                s32 nextIdx = lowIdx + 1;
                mCutPos.set(mPoints[nextIdx]->mPosition);
                s32 prevIdx = highIdx - 1;
                mCutPos.add(mPoints[prevIdx]->mPosition);
                mCutPos.x *= cNum_updatePoints_f13;
                mCutPos.y *= cNum_updatePoints_f14;
                mCutPos.z *= cNum_updatePoints_f15;
            }
        }
    } else {
        MtxPtr baseMtx = mHost->getBaseMtx();
        Vec3f dir(baseMtx[1][0], baseMtx[1][1], baseMtx[1][2]);
        dir.scale(-cNum_updatePoints_f16);
        dir.add(mHost->mPosition);
        restrictPointFromTopAndCalcAxis(0, mPointCount - 1, dir, cNum_updatePoints_f17);
    }
}

void KirairaChain::control() {
    if (mAccelTimer <= 0) {
        mAccelPointIdx = MR::getRandom(0, mPointCount - 1);
        if (MR::isExistRail(mHost)) {
            mAccelTimer = MR::getRandom(3, 10);
        } else {
            mAccelTimer = MR::getRandom(5, 10);
        }

        mAccel.set< f32 >(MR::getRandom(-cNum_control_f1, cNum_control_f2), MR::getRandom(-cNum_control_f3, cNum_control_f4), MR::getRandom(-cNum_control_f5, cNum_control_f6));
    }

    mAccelTimer--;
    mPoints[mAccelPointIdx]->addAccel(mAccel);
    updatePoints();
}

void KirairaChain::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, MR::DrawType_KirairaChain);
    initPoints();

    mModelDrawer = new SimpleModelDrawer(this, "キライラの鎖描画", "KirairaChain", -1);
    mModelDrawer->initWithoutIter();

    mFixPointTop = MR::createModelObjEnemy("キライラの固定点[Top]", "KirairaFixPointTop", mFixPointTopMtx.toMtxPtr());
    mFixPointBottom = MR::createModelObjEnemy("キライラの固定点[Bottom]", "KirairaFixPointBottom", mFixPointBottomMtx.toMtxPtr());

    mFixPointTop->appear();
    mFixPointBottom->appear();
    makeActorAppeared();
}

void KirairaChain::restrictPointFromTop(s32 startIdx, s32 endIdx, const Vec3f& rPos, f32 radius) {
    mPoints[startIdx]->restrict(rPos, cNum_restrictPointFromTop_f1, nullptr);
    mPoints[startIdx]->updatePos(radius);

    for (s32 i = startIdx + 1; i <= endIdx; i++) {
        bool isRestricted = mPoints[i]->restrict(mPoints[i - 1]->mPosition, cNum_restrictPointFromTop_f2, nullptr);
        mPoints[i]->updatePos(radius);

        if (!isRestricted) {
            break;
        }
    }
}

void KirairaChain::restrictPointFromBottom(s32 startIdx, s32 endIdx, const Vec3f& rPos, f32 radius) {
    mPoints[endIdx]->restrict(rPos, cNum_restrictPointFromBottom_f1, nullptr);
    mPoints[endIdx]->updatePos(radius);

    for (s32 i = endIdx - 1; i >= startIdx; i--) {
        bool isRestricted = mPoints[i]->restrict(mPoints[i + 1]->mPosition, cNum_restrictPointFromBottom_f2, nullptr);
        mPoints[i]->updatePos(radius);

        if (!isRestricted) {
            break;
        }
    }
}

void KirairaChain::restrictPointFromTopAndCalcAxis(s32 startIdx, s32 endIdx, const Vec3f& rPos, f32 radius) {
    mPoints[startIdx]->restrict(rPos, cNum_restrictPointFromTopAndCalcAxis_f1, nullptr);
    mPoints[startIdx]->updatePosAndAxis(mFixPointTopDir, radius);

    for (s32 i = startIdx + 1; i <= endIdx; i++) {
        mPoints[i]->restrict(mPoints[i - 1]->mPosition, cNum_restrictPointFromTopAndCalcAxis_f2, nullptr);
        mPoints[i]->updatePosAndAxis(mPoints[i - 1]->mSide, radius);
    }
}

void KirairaChain::restrictPointFromBottomAndCalcAxis(s32 startIdx, s32 endIdx, const Vec3f& rPos, f32 radius) {
    mPoints[endIdx]->restrict(rPos, cNum_restrictPointFromBottomAndCalcAxis_f1, nullptr);
    mPoints[endIdx]->updatePosAndAxis(mFixPointBottomDir, radius);

    for (s32 i = endIdx - 1; i >= startIdx; i--) {
        mPoints[i]->restrict(mPoints[i + 1]->mPosition, cNum_restrictPointFromBottomAndCalcAxis_f2, nullptr);
        mPoints[i]->updatePosAndAxis(mPoints[i + 1]->mSide, radius);
    }
}
