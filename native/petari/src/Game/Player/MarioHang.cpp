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

bool Mario::isHanging() const {
    return getCurrentStatus() == MarioStatus_Hang;
}

bool Mario::fixHangDir(const Vec3f& rPosition, Vec3f* pDirection) {
    Vec3f horizontal(rPosition - mPosition);
    MR::vecKillElement(horizontal, *getGravityVec(), &horizontal);
    MR::normalizeOrZero(&horizontal);
    Triangle pTriangle;
    if (MR::getFirstPolyOnLineToMap(nullptr, &pTriangle, mPosition, horizontal * 150.0f)) {
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
        if (getFrontWallNorm().dot(direction) >= -0.8f) {
            return;
        }

        if (!MR::isExistMapCollision(mFrontFloorPos - mFrontVec * 50.0f - getAirGravityVec() * 5.0f, mFrontVec * 60.0f)) {
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
        if ((mPosition - position).dot(getAirGravityVec()) >= 160.0f) {
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

        if (normal.dot(getAirGravityVec()) >= 0.1f) {
            return;
        }

        if (normal.dot(mHeadVec) >= 0.1f) {
            return;
        }

        direction = -*mBackWallTriangle->getNormal(0);
        if (MR::diffAngleAbsHorizontal(normal, -direction, getAirGravityVec()) > 0.08726647f) {
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

        if (distance > 100.0f) {
            return;
        }

        MR::vecKillElement(normal, getAirGravityVec(), &normal);
        MR::normalizeOrZero(&normal);
        if (!MR::isExistMapCollision(position - direction * 50.0f - getAirGravityVec() * 5.0f, direction * 60.0f)) {
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
        if ((mPosition - position).dot(getAirGravityVec()) >= 160.0f) {
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

        if (normal.dot(getAirGravityVec()) >= 0.1f) {
            return;
        }

        if (normal.dot(mHeadVec) >= 0.1f) {
            return;
        }

        direction = -*mSideWallTriangle->getNormal(0);
        if (MR::diffAngleAbsHorizontal(normal, -direction, getAirGravityVec()) > 0.08726647f) {
            return;
        }

        MR::vecKillElement(normal, getAirGravityVec(), &normal);
        MR::normalizeOrZero(&normal);
        if (!MR::isExistMapCollision(position - direction * 50.0f - getAirGravityVec() * 5.0f, direction * 60.0f)) {
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
        if (slope > 0.2f) {
            slope = 0.2f;
        }

        height -= 30.0f * (5.0f * slope);
    }

    if (getPlayer()->getShadowNorm().dot(*getGravityVec()) < -0.707f) {
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
    if (getPlayer()->getShadowNorm().dot(*getGravityVec()) < -0.707f) {
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
    return !(distance.length() >= 84.0f);
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
    if (getPlayer()->getShadowNorm().dot(*getGravityVec()) < -0.707f) {
        getLastGroundPos(&ground);
        f32 height = MR::vecKillElement(mShadowPos - ground, *getGravityVec(), &horizontal);
        if (height < mActor->getConst().getTable()->mWallHangGrHeight) {
            return false;
        }
    }

    if (!mMovementStates.sideWallCorner) {
        return false;
    }

    if ((mSideWallPos - mGroundPos).dot(*getGravityVec()) < -20.0f) {
        return false;
    }

    Vec3f distance;
    Vec3f position;
    getLastGroundPos(&position);
    MR::vecKillElement(position - mPosition, getAirGravityVec(), &distance);
    return !(distance.length() >= 84.0f);
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
            addTrans(getPlayer()->mFrontVec * -100.0f);
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
        if (pPlayer->mRelativeMove.dot(getFrontVec()) > 0.707f && getPlayer()->mFrontWallDist < 45.0f && !isAnimationRun("崖つかまり開始")) {
            mLetGo = 1;
        }
    }

    if (getPlayer()->mRelativeSensor != nullptr) {
        Mario* pPlayer = getPlayer();
        if (mWallSensor != pPlayer->mRelativeSensor) {
            addTrans(getPlayer()->mFrontVec * -100.0f);
            mActor->setBlendMtxTimer(16);
            stopAnimation("崖つかまり中", "基本");
            return false;
        }
    }

    if (mLetGo) {
        addTrans(getPlayer()->mFrontVec * -100.0f);
        mActor->setBlendMtxTimer(16);
        stopAnimation("崖つかまり中", "基本");
        return false;
    }

    switch (mClimbStep) {
    case 0:
        if (getPlayer()->mVerticalSpeed < 160.0f) {
            if (getPlayer()->getShadowNorm().dot(getGravityVec()) < -0.707f) {
                addTrans(getPlayer()->mFrontVec * -100.0f);
                mActor->setBlendMtxTimer(16);
                stopAnimation("崖つかまり中", "基本");
                return false;
            }
        }

        if (!getPlayer()->mMovementStates.onGround) {
            mLetGo = 1;
        }

        if (getPlayer()->calcDistToCeilHead() < 80.0f) {
            mLetGo = 1;
        }

        break;
    case 1:
        if (calcAngleD(getPlayer()->mGroundNorm) > 5.0f) {
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
            addVelocity(getFrontVec() * 6.0f);
        }

        if (!isAnimationRun(nullptr) || isAnimationTerminate(nullptr)) {
            getPlayer()->mMovementStates.onGround = true;
            return false;
        }

        return true;
    }

    if (mLetGo) {
        addTrans(getPlayer()->mFrontVec * -100.0f);
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
        if (getStickP() < 0.95f) {
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
        if (isAnimationRun("崖つかまり開始") && getAnimator()->getFrame() < 20.0f) {
            mDropQueued = 1;
        } else {
            addTrans(getPlayer()->mFrontVec * -100.0f);
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
        addTrans(getPlayer()->mFrontVec * -150.0f);
        mActor->setBlendMtxTimer(16);
        stopAnimation("崖つかまり中", "基本");
    }

    return false;
}

void MarioHang::tryClimb(bool jump) {
    if (mClimbStep) {
        return;
    }

    if (getPlayer()->calcDistToCeil(false) < 160.0f) {
        return;
    }

    Triangle frontTriangle;
    if (MR::getFirstPolyOnLineToMap(nullptr, &frontTriangle, getTrans() - getGravityVec() * 30.0f, getFrontVec() * 60.0f)) {
        return;
    }

    u32 i;
    for (i = 0; i < 3; i++) {
        Triangle sideTriangle;
        Mario* pPlayer = getPlayer();
        Mario* pSidePlayer = getPlayer();
        bool left = MR::getFirstPolyOnLineToMap(nullptr, &sideTriangle, getTrans() - getGravityVec() * 30.0f - pSidePlayer->mSideVec * 15.0f,
                                                pPlayer->mSideVec * 55.0f);
        bool right = MR::getFirstPolyOnLineToMap(nullptr, &sideTriangle, getTrans() - getGravityVec() * 30.0f + getPlayer()->mSideVec * 15.0f,
                                                 getPlayer()->mSideVec * -55.0f);
        if (right && left) {
            mLetGo = 1;
            return;
        }

        if (left) {
            addTrans(getPlayer()->mSideVec * -15.0f);
        } else if (right) {
            addTrans(getPlayer()->mSideVec * 15.0f);
        } else {
            break;
        }
    }

    if (i == 3) {
        mLetGo = 1;
        return;
    }

    if (isAnimationRun("崖つかまり開始")) {
        if (mJustStarted && jump && getAnimationFrame() < 30.0f) {
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
        if (MR::isSameDirection(getPlayer()->mGroundNorm, mWallNormal, 0.01f)) {
            return false;
        }

        MR::makeMtxFrontUp(pMatrix, -mWallNormal, getPlayer()->mGroundNorm);
    } else {
        f32 blend = MR::clamp(mTimer / 30.0f, 0.0f, 1.0f);
        MR::vecBlendSphere(getPlayer()->mGroundNorm, getPlayer()->mHeadVec, &up, blend);
        if (MR::isSameDirection(up, mWallNormal, 0.01f)) {
            return false;
        }

        MR::makeMtxFrontUp(pMatrix, -mWallNormal, up);
    }

    return true;
}
