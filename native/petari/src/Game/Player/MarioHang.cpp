#include "Game/Player/MarioHang.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <cstring>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_fixHangDir_f1, "fixHangDir.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f1, "checkHang.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f2, "checkHang.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f3, "checkHang.f3", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f4, "checkHang.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f5, "checkHang.f5", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f6, "checkHang.f6", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f7, "checkHang.f7", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f8, "checkHang.f8", 0.08726647f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f9, "checkHang.f9", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f10, "checkHang.f10", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f11, "checkHang.f11", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f12, "checkHang.f12", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f13, "checkHang.f13", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f14, "checkHang.f14", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f15, "checkHang.f15", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f16, "checkHang.f16", 0.08726647f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f17, "checkHang.f17", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f18, "checkHang.f18", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkHang_f19, "checkHang.f19", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableHang_f1, "isEnableHang.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableHang_f2, "isEnableHang.f2", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableHang_f3, "isEnableHang.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableHang_f4, "isEnableHang.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableHang_f5, "isEnableHang.f5", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableBackHang_f1, "isEnableBackHang.f1", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableBackHang_f2, "isEnableBackHang.f2", 84.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableSideHang_f1, "isEnableSideHang.f1", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableSideHang_f2, "isEnableSideHang.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableSideHang_f3, "isEnableSideHang.f3", 84.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f5, "update.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f6, "update.f6", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f7, "update.f7", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f8, "update.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f9, "update.f9", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f10, "update.f10", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f11, "update.f11", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f12, "update.f12", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f13, "update.f13", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f14, "update.f14", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f15, "update.f15", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_notice_f1, "notice.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f1, "tryClimb.f1", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f2, "tryClimb.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f3, "tryClimb.f3", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f4, "tryClimb.f4", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f5, "tryClimb.f5", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f6, "tryClimb.f6", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f7, "tryClimb.f7", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f8, "tryClimb.f8", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f9, "tryClimb.f9", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f10, "tryClimb.f10", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f11, "tryClimb.f11", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryClimb_f12, "tryClimb.f12", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_postureCtrl_f1, "postureCtrl.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_postureCtrl_f2, "postureCtrl.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_postureCtrl_f3, "postureCtrl.f3", 0.01f);

bool Mario::isHanging() const {
    return getCurrentStatus() == MarioStatus_Hang;
}

bool Mario::fixHangDir(const Vec3f& rPosition, Vec3f* pDirection) {
    Vec3f horizontal(rPosition - mPosition);
    MR::vecKillElement(horizontal, *getGravityVec(), &horizontal);
    MR::normalizeOrZero(&horizontal);
    Triangle pTriangle;
    if (MR::getFirstPolyOnLineToMap(nullptr, &pTriangle, mPosition, horizontal * cNum_fixHangDir_f1)) {
        *pDirection = -*pTriangle.getNormal(0);
        mHang->recordWallPolygon(&pTriangle);
        return true;
    }

    if (pDirection->dot(horizontal) < 0.0f) {
        *pDirection = horizontal;
        return false;
    }

    return false;
}

void Mario::checkHang() {
    if (mHang->mHangTimer) {
        return;
    }

    if (mMovementStates.jumping && isRising()) {
        mMovementStates.hangBlocked = false;
    }

    if (mMovementStates.jumping && mAirTimer > 20) {
        mMovementStates.hangBlocked = false;
    }

    if (isCeiling()) {
        mMovementStates.hangAllowed = false;
    }

    if (!mMovementStates.hangAllowed) {
        return;
    }

    if (getPlayer()->mDrawStates.inWater) {
        return;
    }

    if (isStatusActive(MarioStatus_Rabbit)) {
        return;
    }

    if (mActor->mTakeCount) {
        return;
    }

    if (getPlayerMode() == PlayerMode_Bee) {
        return;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        return;
    }

    bool front = true;
    bool back = true;
    bool side = true;
    if (mMovementStates.sideWall) {
        const Triangle* pTriangle = mSideWallTriangle;
        if (!MR::isSameMtx(pTriangle->getBaseMtx()->toMtxPtr(), pTriangle->getPrevBaseMtx()->toMtxPtr())) {
            front = false;
            back = false;
        }
    }

    if (mMovementStates.frontWall) {
        const Triangle* pTriangle = mFrontWallTriangle;
        if (!MR::isSameMtx(pTriangle->getBaseMtx()->toMtxPtr(), pTriangle->getPrevBaseMtx()->toMtxPtr())) {
            side = false;
            back = false;
        }
    }

    if (mMovementStates.backWall) {
        const Triangle* pTriangle = mBackWallTriangle;
        if (!MR::isSameMtx(pTriangle->getBaseMtx()->toMtxPtr(), pTriangle->getPrevBaseMtx()->toMtxPtr())) {
            front = false;
            side = false;
        }
    }

    Vec3f previousPosition(mPosition);
    if (front && mMovementStates.frontFloorFound && isEnableHang()) {
        Vec3f direction(mFrontFloorPos - mPosition);
        MR::vecKillElement(direction, getAirGravityVec(), &direction);
        MR::normalizeOrZero(&direction);
        if (getFrontWallNorm().dot(direction) >= -cNum_checkHang_f1) {
            return;
        }

        if (!MR::isExistMapCollision(mFrontFloorPos - mFrontVec * cNum_checkHang_f2 - getAirGravityVec() * cNum_checkHang_f3, mFrontVec * cNum_checkHang_f4)) {
            u8 started = 1;
            if (isAnimationRun("落下")) {
                started = 0;
            }

            changeStatus(mHang);
            mVelocity.zero();
            stopWalk();
            stopJump();
            setTrans(mFrontFloorPos);
            setFrontVecKeepUp(-getWallNorm());
            mHang->recordWallPolygon(mFrontWallTriangle);
            mHang->mJustStarted = started;
        }
    } else if (back && isEnableBackHang()) {
        Vec3f position;
        getLastGroundPos(&position);
        if ((mPosition - position).dot(getAirGravityVec()) >= cNum_checkHang_f5) {
            return;
        }

        Vec3f direction(-mFrontVec);
        u32 edge = getLastGroundEdgeIndex(mActor->mCenterPos, direction);
        direction = mBackWallPos - mActor->mCenterPos;
        MR::normalizeOrZero(&direction);
        u32 positionEdge = getLastGroundEdgeIndex(mActor->mCenterPos, direction);
        direction = -*mBackWallTriangle->getNormal(0);
        u32 normalEdge = getLastGroundEdgeIndex(mActor->mCenterPos, direction);
        Vec3f normal;
        if (edge == positionEdge && edge == normalEdge) {
            normal = *getLastGroundEdgeNrm(edge);
        } else {
            return;
        }

        if (normal.dot(getAirGravityVec()) >= cNum_checkHang_f6) {
            return;
        }

        if (normal.dot(mHeadVec) >= cNum_checkHang_f7) {
            return;
        }

        direction = -*mBackWallTriangle->getNormal(0);
        if (MR::diffAngleAbsHorizontal(normal, -direction, getAirGravityVec()) > cNum_checkHang_f8) {
            return;
        }

        Vec3f horizontal;
        f32 distance = 0.0f;
        switch (edge) {
        case 0:
        case 1:
            distance = MR::vecKillElement(*mLastGroundPolygon->calcAndGetPos(0) - position, normal, &horizontal);
            position += normal * distance;
            break;
        case 2:
            distance = MR::vecKillElement(*mLastGroundPolygon->calcAndGetPos(1) - position, normal, &horizontal);
            position += normal * distance;
            break;
        }

        if (distance > cNum_checkHang_f9) {
            return;
        }

        MR::vecKillElement(normal, getAirGravityVec(), &normal);
        MR::normalizeOrZero(&normal);
        if (!MR::isExistMapCollision(position - direction * cNum_checkHang_f10 - getAirGravityVec() * cNum_checkHang_f11, direction * cNum_checkHang_f12)) {
            changeStatus(mHang);
            mVelocity.zero();
            stopWalk();
            stopJump();
            Vec3f hangDirection;
            bool found = fixHangDir(position, &hangDirection);
            setTrans(position);
            setFrontVecKeepUp(normal);
            mMovementStates.waitStickNeutral = true;
            if (!found) {
                mHang->recordWallPolygon(mBackWallTriangle);
            }

            mHang->recordHangNorm(normal);
            mHang->mJustStarted = 0;
        }
    } else if (side && isEnableSideHang()) {
        Vec3f position;
        getLastGroundPos(&position);
        if ((mPosition - position).dot(getAirGravityVec()) >= cNum_checkHang_f13) {
            return;
        }

        Vec3f direction(mSideWallPos - mActor->mCenterPos);
        MR::normalizeOrZero(&direction);
        u32 edge = getLastGroundEdgeIndex(mActor->mCenterPos, direction);
        Vec3f sideDirection(mSideVec);
        if (sideDirection.dot(direction) < 0.0f) {
            direction = -sideDirection;
        } else {
            direction = sideDirection;
        }

        u32 sideEdge = getLastGroundEdgeIndex(mActor->mCenterPos, direction);
        direction = -*mSideWallTriangle->getNormal(0);
        u32 normalEdge = getLastGroundEdgeIndex(mActor->mCenterPos, direction);
        Vec3f normal;
        if (edge == sideEdge && edge == normalEdge) {
            normal = *getLastGroundEdgeNrm(edge);
        } else {
            return;
        }

        if (normal.dot(getAirGravityVec()) >= cNum_checkHang_f14) {
            return;
        }

        if (normal.dot(mHeadVec) >= cNum_checkHang_f15) {
            return;
        }

        direction = -*mSideWallTriangle->getNormal(0);
        if (MR::diffAngleAbsHorizontal(normal, -direction, getAirGravityVec()) > cNum_checkHang_f16) {
            return;
        }

        MR::vecKillElement(normal, getAirGravityVec(), &normal);
        MR::normalizeOrZero(&normal);
        if (!MR::isExistMapCollision(position - direction * cNum_checkHang_f17 - getAirGravityVec() * cNum_checkHang_f18, direction * cNum_checkHang_f19)) {
            changeStatus(mHang);
            mVelocity.zero();
            stopWalk();
            stopJump();
            Vec3f hangDirection;
            bool found = fixHangDir(position, &hangDirection);
            setTrans(position);
            setFrontVecKeepUp(normal);
            mMovementStates.waitStickNeutral = true;
            if (!found) {
                mHang->recordWallPolygon(mSideWallTriangle);
            }

            mHang->recordHangNorm(normal);
            mHang->mJustStarted = 0;
        }
    }

    if (isStatusActive(MarioStatus_Hang)) {
        mLedgeFallTimer = 0;
        for (u32 i = 0; i < 10; i++) {
            updateGroundInfo();
            if (mMovementStates.onGround) {
                return;
            }

            addTrans(mFrontVec * 1.0f - getAirGravityVec() * 1.0f);
        }
    }
}

bool Mario::isEnableHang() {
    if (!mMovementStates.frontWall) {
        return false;
    }

    if (!mMovementStates.frontLedge) {
        return false;
    }

    if (mMovementStates.hipDrop) {
        return false;
    }

    if (mDrawStates.pressed) {
        return false;
    }

    if (isStatusActive(MarioStatus_Recovery)) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NotGrab, false)) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NoAction, false)) {
        return false;
    }

    const s32 wallCode = MR::getWallCodeIndex(mFrontFloorPolygon);
    if (wallCode == CollisionWallCode_NotGrab || wallCode == CollisionWallCode_NoAction) {
        return false;
    }

    Vec3f shadowPos(mShadowPos);
    Vec3f up(-getAirGravityVec());
    f32 slope = mShadowPolygon->getNormal(0)->dot(up);
    f32 height = mActor->getConst().getTable()->mWallHangGrHeight;
    if (slope > 0.0f) {
        if (slope > cNum_isEnableHang_f1) {
            slope = cNum_isEnableHang_f2;
        }

        height -= cNum_isEnableHang_f3 * (cNum_isEnableHang_f4 * slope);
    }

    if (getPlayer()->getShadowNorm().dot(*getGravityVec()) < -cNum_isEnableHang_f5) {
        if ((mFrontFloorPos - mShadowPos).dot(-*getGravityVec()) < height) {
            return false;
        }
    }

    f32 heightFromMario = (mFrontFloorPos - mPosition).dot(-*getGravityVec());
    if (heightFromMario > mActor->getConst().getTable()->mWallHangMyHeight) {
        return false;
    }

    return !isRising();
}

bool Mario::isEnableBackHang() {
    bool turn = false;
    if (!mLastGroundPolygon->isValid()) {
        return false;
    }

    if (mJumpKind == 6) {
        if (isRising()) {
            return false;
        }

        turn = true;
    } else if (!mMovementStates.hangBlocked) {
        return false;
    }

    if (!mMovementStates.backWall) {
        return false;
    }

    if (mMovementStates.hipDrop) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NotGrab, false)) {
        return false;
    }

    if (MR::getWallCodeIndex(mLastGroundPolygon) == CollisionWallCode_NotGrab) {
        return false;
    }

    if (isRising()) {
        return false;
    }

    if (mStickPos.z > mActor->getConst().getTable()->mWallBackHangStickPower) {
        return false;
    }

    if (mWalkSpeed > mActor->getConst().getTable()->mWallBackHangWalkSpeed) {
        return false;
    }

    Vec3f ground;
    Vec3f horizontal;
    if (getPlayer()->getShadowNorm().dot(*getGravityVec()) < -cNum_isEnableBackHang_f1) {
        if (turn) {
            ground = mPosition;
        } else {
            getLastGroundPos(&ground);
        }

        f32 height = MR::vecKillElement(mShadowPos - ground, *getGravityVec(), &horizontal);
        if (height < mActor->getConst().getTable()->mWallHangGrHeight) {
            return false;
        }
    }

    if (!turn) {
        if ((mBackWallPos - mGroundPos).dot(*getGravityVec()) < 0.0f) {
            return false;
        }
    } else {
        setFrontVecKeepUp(-mFrontVec);
        return false;
    }

    Vec3f distance;
    Vec3f position;
    getLastGroundPos(&position);
    MR::vecKillElement(position - mPosition, getAirGravityVec(), &distance);
    return !(distance.length() >= cNum_isEnableBackHang_f2);
}

bool Mario::isEnableSideHang() {
    if (mHitStates.gravityFlip || mHitStates.gravityFrontTurn) {
        return false;
    }

    if (!mMovementStates.hangBlocked) {
        return false;
    }

    if (!mMovementStates.sideWall) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NotGrab, false)) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NoAction, false)) {
        return false;
    }

    const s32 wallCode = MR::getWallCodeIndex(mLastGroundPolygon);
    if (wallCode == CollisionWallCode_NotGrab || wallCode == CollisionWallCode_NoAction) {
        return false;
    }

    if (isRising()) {
        return false;
    }

    if (mStickPos.z > mActor->getConst().getTable()->mWallBackHangStickPower) {
        return false;
    }

    if (mWalkSpeed > mActor->getConst().getTable()->mWallBackHangWalkSpeed) {
        return false;
    }

    Vec3f ground;
    Vec3f horizontal;
    if (getPlayer()->getShadowNorm().dot(*getGravityVec()) < -cNum_isEnableSideHang_f1) {
        getLastGroundPos(&ground);
        f32 height = MR::vecKillElement(mShadowPos - ground, *getGravityVec(), &horizontal);
        if (height < mActor->getConst().getTable()->mWallHangGrHeight) {
            return false;
        }
    }

    if (!mMovementStates.sideWallCorner) {
        return false;
    }

    if ((mSideWallPos - mGroundPos).dot(*getGravityVec()) < -cNum_isEnableSideHang_f2) {
        return false;
    }

    Vec3f distance;
    Vec3f position;
    getLastGroundPos(&position);
    MR::vecKillElement(position - mPosition, getAirGravityVec(), &distance);
    return !(distance.length() >= cNum_isEnableSideHang_f3);
}

void MarioHang::recordWallPolygon(const Triangle* pTriangle) {
    *mWallPolygon = *pTriangle;
    mWallSensor = pTriangle->mSensor;
    Pos3f matrix;
    MR::makeMtxWithoutScale(&matrix, *mWallPolygon->getBaseInvMtx());
    NebulaMtxMultVecSR(matrix.toMtxPtr(), mWallPolygon->getNormal(0), &mLocalNormal);
    mWallNormal = *mWallPolygon->getNormal(0);
}

void MarioHang::recordHangNorm(const Vec3f& rNormal) {
    mWallNormal = rNormal;
    Pos3f matrix;
    MR::makeMtxWithoutScale(&matrix, *mWallPolygon->getBaseInvMtx());
    NebulaMtxMultVecSR(matrix.toMtxPtr(), &rNormal, &mLocalNormal);
}

void MarioHang::forceDrop() {
    mLetGo = 1;
}

MarioHang::MarioHang(MarioActor* pActor) : MarioState(pActor, MarioStatus_Hang) {
    mClimbStep = 0;
    mTimer = 0;
    mSlipUpTimer = 0;
    mHangTimer = 0;
    mSlipUp = 0;
    mClimbQueued = 0;
    mLetGo = 0;
    mDropQueued = 0;
    mJustStarted = 0;
    mLocalNormal.zero();
    mWallNormal.zero();
    mWallSensor = nullptr;
    mWallPolygon = new Triangle;
}

bool MarioHang::start() {
    getPlayer()->cancelSquatMode();
    stopAnimationUpper(nullptr);
    changeAnimation("崖つかまり開始", "崖つかまり中");
    mActor->setBlendMtxTimer(mActor->getConst().getTable()->mHangBlendTime);
    mClimbStep = 0;
    mTimer = 0;
    mSlipUpTimer = 0;
    mClimbQueued = 0;
    mLetGo = 0;
    mDropQueued = 0;
    mSlipUp = 0;
    Mario* pPlayer = getPlayer();
    pPlayer->mCorrectionAngle = 0.0f;
    pPlayer->mCorrectionEaseTimer = 0;
    pPlayer->mCorrectionTurnTimer = 0;
    mActor->mBindOn = 0;
    return true;
}

bool MarioHang::update() {
    getPlayer()->mDrawStates.atEdge = false;
    Pos3f matrix;
    matrix.identity();
    MR::makeMtxWithoutScale(&matrix, *mWallPolygon->getBaseMtx());
    Vec3f normal;
    NebulaMtxMultVecSR(matrix.toMtxPtr(), &mLocalNormal, &normal);
    getPlayer()->setFrontVecKeepUp(-normal);
    mWallNormal = normal;
    mTimer++;
    if (mSlipUpTimer) {
        mSlipUpTimer--;
        if (!mSlipUpTimer) {
            mSlipUp = 1;
        } else {
            return true;
        }
    }

    if (mSlipUp) {
        return false;
    }

    if (MR::isSameMtx(mWallPolygon->getBaseMtx()->toMtxPtr(), mWallPolygon->getPrevBaseMtx()->toMtxPtr())) {
        if (getPlayer()->mRelativeSensor != nullptr) {
            addTrans(getPlayer()->mFrontVec * -cNum_update_f1);
            mActor->setBlendMtxTimer(16);
            stopAnimation("崖つかまり中", "基本");
            return false;
        }
    } else {
        if (getPlayer()->mMovementStates.sideWall && mTimer > 2) {
            Vec3f movement(mActor->getLastMove());
            if (movement.dot(getPlayer()->getSideWallNorm()) < 0.0f) {
                mLetGo = 1;
            }
        }

        Mario* pPlayer = getPlayer();
        if (pPlayer->mRelativeMove.dot(getFrontVec()) > cNum_update_f2 && getPlayer()->mFrontWallDist < cNum_update_f3 && !isAnimationRun("崖つかまり開始")) {
            mLetGo = 1;
        }
    }

    if (getPlayer()->mRelativeSensor != nullptr) {
        Mario* pPlayer = getPlayer();
        if (mWallSensor != pPlayer->mRelativeSensor) {
            addTrans(getPlayer()->mFrontVec * -cNum_update_f4);
            mActor->setBlendMtxTimer(16);
            stopAnimation("崖つかまり中", "基本");
            return false;
        }
    }

    if (mLetGo) {
        addTrans(getPlayer()->mFrontVec * -cNum_update_f5);
        mActor->setBlendMtxTimer(16);
        stopAnimation("崖つかまり中", "基本");
        return false;
    }

    switch (mClimbStep) {
    case 0:
        if (getPlayer()->mVerticalSpeed < cNum_update_f6) {
            if (getPlayer()->getShadowNorm().dot(getGravityVec()) < -cNum_update_f7) {
                addTrans(getPlayer()->mFrontVec * -cNum_update_f8);
                mActor->setBlendMtxTimer(16);
                stopAnimation("崖つかまり中", "基本");
                return false;
            }
        }

        if (!getPlayer()->mMovementStates.onGround) {
            mLetGo = 1;
        }

        if (getPlayer()->calcDistToCeilHead() < cNum_update_f9) {
            mLetGo = 1;
        }

        break;
    case 1:
        if (calcAngleD(getPlayer()->mGroundNorm) > cNum_update_f10) {
            changeAnimationNonStop("崖つかまり終了坂");
            changeAnimation(nullptr, "基本");
        } else {
            changeAnimation("崖つかまり終了", "基本");
        }

        playSound("声崖つかまり終了");
        mClimbStep++;
        return true;
    case 2:
        if (!getPlayer()->mMovementStates.onGround) {
            addVelocity(getFrontVec() * cNum_update_f11);
        }

        if (!isAnimationRun(nullptr) || isAnimationTerminate(nullptr)) {
            getPlayer()->mMovementStates.onGround = true;
            return false;
        }

        return true;
    }

    if (mLetGo) {
        addTrans(getPlayer()->mFrontVec * -cNum_update_f12);
        mActor->setBlendMtxTimer(16);
        stopAnimation("崖つかまり中", "基本");
        return false;
    }

    if (checkTrgA()) {
        tryClimb(true);
    }

    if (getPlayer()->mMovementStates.waitStickNeutral) {
        return true;
    }

    u8 direction = 0;
    if (!mSlipUpTimer) {
        direction = getPlayer()->checkStickFrontBack();
        if (getStickP() < cNum_update_f13) {
            direction = 0;
        }

        if (mDropQueued) {
            direction = 2;
        }
    }

    switch (direction) {
    case 1:
        if (mTimer > 18) {
            tryClimb(false);
        }

        break;
    case 2:
        if (isAnimationRun("崖つかまり開始") && getAnimator()->getFrame() < cNum_update_f14) {
            mDropQueued = 1;
        } else {
            addTrans(getPlayer()->mFrontVec * -cNum_update_f15);
            mActor->setBlendMtxTimer(16);
            Mario* pPlayer = getPlayer();
            pPlayer->mDropWaitTimer = 0;
            getPlayer()->mMovementStates.hangAllowed = false;
            getPlayer()->mMovementStates.onGround = false;
            getPlayer()->tryDrop();
            pPlayer = getPlayer();
            pPlayer->mBackJumpTimer = 120;
            return false;
        }

        break;
    }

    if (mClimbQueued && !mClimbStep && mTimer > 18) {
        mClimbStep = 1;
        mTimer = 0;
    }

    if (!mClimbStep && mTimer == 2) {
        playSound("声崖つかまり");
    }

    return true;
}

bool MarioHang::close() {
    stopAnimation("崖つかまり開始");
    stopAnimation("崖つかまり中");
    stopAnimation("崖つかまり終了");
    stopAnimation("崖つかまり終了坂");
    if (getPlayer()->mMovementStates.onGround) {
        changeAnimation(nullptr, "基本");
    } else {
        changeAnimation(nullptr, "落下");
    }

    getPlayer()->setWallCancel();
    mHangTimer = 120;
    if (mSlipUp) {
        getPlayer()->tryHangSlipUp();
    } else {
        getPlayer()->mActionStates.squatEnded = true;
    }

    mActor->mBindOn = 1;
    return true;
}

bool MarioHang::notice() {
    if (getNoticedStatus() == MarioStatus_Faint) {
        addTrans(getPlayer()->mFrontVec * -cNum_notice_f1);
        mActor->setBlendMtxTimer(16);
        stopAnimation("崖つかまり中", "基本");
    }

    return false;
}

void MarioHang::tryClimb(bool jump) {
    if (mClimbStep) {
        return;
    }

    if (getPlayer()->calcDistToCeil(false) < cNum_tryClimb_f1) {
        return;
    }

    Triangle frontTriangle;
    if (MR::getFirstPolyOnLineToMap(nullptr, &frontTriangle, getTrans() - getGravityVec() * cNum_tryClimb_f2, getFrontVec() * cNum_tryClimb_f3)) {
        return;
    }

    u32 i;
    for (i = 0; i < 3; i++) {
        Triangle sideTriangle;
        Mario* pPlayer = getPlayer();
        Mario* pSidePlayer = getPlayer();
        bool left = MR::getFirstPolyOnLineToMap(nullptr, &sideTriangle, getTrans() - getGravityVec() * cNum_tryClimb_f4 - pSidePlayer->mSideVec * cNum_tryClimb_f5,
                                                pPlayer->mSideVec * cNum_tryClimb_f6);
        bool right = MR::getFirstPolyOnLineToMap(nullptr, &sideTriangle, getTrans() - getGravityVec() * cNum_tryClimb_f7 + getPlayer()->mSideVec * cNum_tryClimb_f8,
                                                 getPlayer()->mSideVec * -cNum_tryClimb_f9);
        if (right && left) {
            mLetGo = 1;
            return;
        }

        if (left) {
            addTrans(getPlayer()->mSideVec * -cNum_tryClimb_f10);
        } else if (right) {
            addTrans(getPlayer()->mSideVec * cNum_tryClimb_f11);
        } else {
            break;
        }
    }

    if (i == 3) {
        mLetGo = 1;
        return;
    }

    if (isAnimationRun("崖つかまり開始")) {
        if (mJustStarted && jump && getAnimationFrame() < cNum_tryClimb_f12) {
            mSlipUpTimer = 5;
            changeAnimationNonStop("つかまりスリップアップ準備");
        }

        mClimbQueued = 1;
        return;
    }

    mClimbStep = 1;
    mTimer = 0;
}
bool MarioHang::postureCtrl(MtxPtr pMatrix) {
    Vec3f up;
    Vec3f direction(getPlayer()->mHeadVec);
    direction += mWallNormal;
    if (MR::isNearZero(direction)) {
        return false;
    }

    if (!mClimbStep) {
        if (MR::isSameDirection(getPlayer()->mGroundNorm, mWallNormal, cNum_postureCtrl_f1)) {
            return false;
        }

        MR::makeMtxFrontUp(pMatrix, -mWallNormal, getPlayer()->mGroundNorm);
    } else {
        f32 blend = MR::clamp(mTimer / cNum_postureCtrl_f2, 0.0f, 1.0f);
        MR::vecBlendSphere(getPlayer()->mGroundNorm, getPlayer()->mHeadVec, &up, blend);
        if (MR::isSameDirection(up, mWallNormal, cNum_postureCtrl_f3)) {
            return false;
        }

        MR::makeMtxFrontUp(pMatrix, -mWallNormal, up);
    }

    return true;
}
