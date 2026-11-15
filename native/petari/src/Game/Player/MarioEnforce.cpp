#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioHang.hpp"
#include "Game/Player/MarioModule.hpp"
#include "Game/Player/MarioSukekiyo.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/RailUtil.hpp"

void Mario::checkEnforceMove() {
    checkEnforceMoveInner();
    updateOnimasu();
}

void Mario::checkEnforceMoveInner() {
    mMovementStates.baseRotated = 0;
    Vec3f v15(mRelativeMove);
    if (mMovementStates.onGround != 0 || mMovementStates.jumping != 0) {
        mRelativeMove.zero();
    }

    mRelativeSensor = nullptr;
    u32 relativeMove = moveRelativePosition(0);
    bool same = relativeMove | moveRelativePositionWall();
    if (same) {
        if (MR::isSameMtx(*mShadowPolygon->getBaseMtx(), *mShadowPolygon->getPrevBaseMtx())) {

            if (mVelocity.dot(mGroundNorm) < 0.0f) {
                f32 element = MR::vecKillElement(mVelocity, mGroundNorm, &mVelocity);
                f32 v8 = element;

                if (!mMovementStates.onGround) {
                    if (element < 0.0f) {
                        if (-element > mVerticalSpeed) {
                            v8 = -mVerticalSpeed;
                        }
                    }

                    mVelocity += mGroundNorm * v8;
                }
            }
        }

        mDrawStates.enforcedMove = 1;

        if (!mActor->mBindOn) {
            addTrans(mVelocity);
        } else {
            bool v11 = false;
            Vec3f v14(mPosition);

            if (mMovementStates.squat == 0) {
                if (200.0f == mCeilDist) {
                    v11 = true;
                }
            }

            addTrans(mVelocity);

            if (v11) {
                if (calcDistToCeil(false) < 200.0f) {
                    mPosition = v14;
                    mHitStates.enforcedMove = 1;
                }
            }

            mActor->mBinder->clear();
        }

        mVelocity.zero();
        MarioActor* actor = mActor;
        actor->mPosition = mPosition;
        actor->calcCenterPos();
    }
}

void Mario::recordRelativePosition() {
    const Triangle* sameSensorFloor = nullptr;
    const Vec3f* pPosition;
    Vec3f strikeCenter(mActor->mCenterPos);
    mRelativeWallPolygons[0]->mIdx = -1;
    mRelativeWallPolygons[1]->mIdx = -1;

    f32 strikeRadius = 85.0f;
    if (mMovementStates.onGround && mShadowPolygon->isValid() && MR::isSameMtx(*mShadowPolygon->getBaseMtx(), *mShadowPolygon->getPrevBaseMtx())) {
        strikeRadius = 81.0f;
    }

    if (mMovementStates.squat) {
        strikeRadius = 51.0f;
    }

    if (mActor->isJumpRising()) {
        invalidateRelativePosition();
        return;
    }

    const u32 strikeNum = static_cast< u32 >(Collision::checkStrikeBallToMap(strikeCenter, strikeRadius, nullptr, nullptr));
    const f32 floorDotThreshold = 0.707f;
    const f32 wallNormalDotThreshold = 0.9f;

    for (u32 i = 0; i < strikeNum; i++) {
        const HitInfo* hitInfo = Collision::getStrikeInfoMap(i);
        const Triangle* tri = &hitInfo->mParentTriangle;

        if (tri->mSensor == nullptr) {
            continue;
        }

        if (MR::isSameMtx(*tri->getBaseMtx(), *tri->getPrevBaseMtx())) {
            continue;
        }

        if (tri->getNormal(0)->dot(-*getGravityVec()) < floorDotThreshold) {
            if (!mRelativeWallPolygons[0]->isValid()) {
                *mRelativeWallPolygons[0] = *tri;
                continue;
            }

            if (tri->getNormal(0)->dot(*mRelativeWallPolygons[0]->getNormal(0)) > wallNormalDotThreshold) {
                continue;
            }

            if (!mRelativeWallPolygons[1]->isValid()) {
                *mRelativeWallPolygons[1] = *tri;
                continue;
            }

            const f32 candidateDot = tri->getNormal(0)->dot(*mRelativeWallPolygons[0]->getNormal(0));
            const f32 currentDot = mRelativeWallPolygons[0]->getNormal(0)->dot(*mRelativeWallPolygons[1]->getNormal(0));
            if (currentDot > candidateDot) {
                *mRelativeWallPolygons[1] = *tri;
            }

            continue;
        }

        sameSensorFloor = tri;
        if (tri->mSensor != mHang->mWallSensor) {
            break;
        }
    }

    if (sameSensorFloor == nullptr) {
        if (isStatusActive(MarioStatus_Wall)) {
            *mRelativePolygon = *mFrontWallTriangle;
        } else if (mMovementStates.onGround) {
            if (mHitStates.groundSameSensor) {
                *mRelativePolygon = *mGroundPolygon;
            } else if (isUseAnotherMovingPolygon()) {
                *mRelativePolygon = *mGroundPolygon;
            } else {
                invalidateRelativePosition();
            }
        } else {
            invalidateRelativePosition();
        }
    } else if (mHitStates.groundSameSensor) {
        if (sameSensorFloor->mSensor == mGroundPolygon->mSensor) {
            *mRelativePolygon = *sameSensorFloor;
        } else if (isUseAnotherMovingPolygon()) {
            *mRelativePolygon = *sameSensorFloor;
        } else {
            invalidateRelativePosition();
        }
    } else if (isUseAnotherMovingPolygon()) {
        *mRelativePolygon = *sameSensorFloor;
    } else {
        invalidateRelativePosition();
    }

    if (mRelativePolygon->isValid()) {
        if (mActor->selectInvalidMovingCollision(mRelativePolygon->mSensor->mHost->mName)) {
            invalidateRelativePosition();
        } else {
            pPosition = &mActor->mPosition;
            NebulaMtxMultVec(*mRelativePolygon->getBaseInvMtx(), pPosition, &mRelativePos);
        }
    }

    if (mRelativeWallPolygons[0]->isValid()) {
        if (mActor->selectInvalidMovingCollision(mRelativeWallPolygons[0]->mSensor->mHost->mName)) {
            mRelativeWallPolygons[0]->mIdx = -1;
        } else {
            pPosition = &mActor->mPosition;
            NebulaMtxMultVec(*mRelativeWallPolygons[0]->getBaseInvMtx(), pPosition, &mRelativeWallPos[0]);
        }
    }

    if (mRelativeWallPolygons[1]->isValid()) {
        if (mActor->selectInvalidMovingCollision(mRelativeWallPolygons[1]->mSensor->mHost->mName)) {
            mRelativeWallPolygons[1]->mIdx = -1;
        } else {
            pPosition = &mActor->mPosition;
            NebulaMtxMultVec(*mRelativeWallPolygons[1]->getBaseInvMtx(), pPosition, &mRelativeWallPos[1]);
        }
    }
}

void Mario::invalidateRelativePosition() {
    mRelativePolygon->mIdx = -1;
    mRelativeWallPolygons[0]->mIdx = -1;
    mRelativeWallPolygons[1]->mIdx = -1;
}

Pos3f* Mario::getMoveBaseMtx() const {
    if (mRelativePolygon->isValid()) {
        return mRelativePolygon->getBaseMtx();
    }

    return nullptr;
}

u32 Mario::moveRelativePosition(u32) {
    Triangle* rTri = mRelativePolygon;
    if (!rTri->isValid()) {
        return 0;
    }

    if (MR::isSameMtx(*rTri->getBaseMtx(), *rTri->getPrevBaseMtx())) {
        return 0;
    }

    if (mActor->mInDemo && !isStatusActive(MarioStatus_Talk)) {
        invalidateRelativePosition();
        return 0;
    }

    if (rTri->mSensor != nullptr && MR::isDead(rTri->mSensor->mHost)) {
        invalidateRelativePosition();
        return 0;
    }

    Vec3f movePos;
    NebulaMtxMultVec(*rTri->getBaseMtx(), &mRelativePos, &movePos);
    mRelativeMove = movePos - mPosition;

    if (isSwimming()) {
        Vec3f dist(mActor->mCenterPos);
        dist -= movePos;

        Vec3f normal(*rTri->getNormal(0));
        if (dist.dot(normal) > 0.0f && dist.dot(mRelativeMove) > 0.0f) {
            addVelocity(mRelativeMove);
        }

        return 0;
    }

    addVelocity(mRelativeMove);

    if (mRelativeSensor == nullptr || rTri->mSensor != mHang->mWallSensor) {
        mRelativeSensor = rTri->mSensor;
    }

    if (!MR::isSameMtxRot(*rTri->getBaseMtx(), *rTri->getPrevBaseMtx())) {
        mMovementStates.baseRotated = 1;

        if (MR::isRotAxisY(*rTri->getBaseMtx(), *rTri->getPrevBaseMtx())) {
            mActionStates.baseRotatedAboutUp = 1;
        }

        Vec3f rotAxis;
        MR::calcMtxRotAxis(&rotAxis, *rTri->getBaseMtx(), *rTri->getPrevBaseMtx());
        if (MR::isSameDirection(rotAxis, getAirGravityVec())) {
            mActionStates.baseRotatedAboutUp = 1;
        }
    }

    return 1;
}

u32 Mario::moveRelativePositionWall() {
    u32 moved = 0;

    for (u32 i = 0; i < 2; i++) {
        Triangle* tri = mRelativeWallPolygons[i];
        if (!tri->isValid()) {
            continue;
        }

        if (MR::isSameMtx(*tri->getBaseMtx(), *tri->getPrevBaseMtx())) {
            continue;
        }

        if (tri->mSensor != nullptr && !tri->mSensor->isValid()) {
            tri->mIdx = -1;
            continue;
        }

        const Vec3f* relPos = &mRelativeWallPos[i];
        Vec3f movePos;
        NebulaMtxMultVec(*tri->getBaseMtx(), relPos, &movePos);

        Vec3f add(movePos);
        add -= mPosition;

        const f32 elem = MR::vecKillElement(add, *tri->getNormal(0), &add);
        if (elem > 0.0f) {
            addVelocity(*tri->getNormal(0) * elem);
        }

        if (!MR::isSameMtxRot(*tri->getBaseMtx(), *tri->getPrevBaseMtx())) {
            mMovementStates.baseRotated = 1;
        }

        if (mRelativeSensor == nullptr || tri->mSensor != mHang->mWallSensor) {
            mRelativeSensor = tri->mSensor;
        }

        moved = 1;
    }

    return moved;
}

void Mario::recordLastGround() {
    *mLastGroundPolygon = *mGroundPolygon;
    NebulaMtxMultVec(*mLastGroundPolygon->getBaseInvMtx(), &mGroundPos, &mLastGroundLocalPos);
}

void Mario::getLastGroundPos(Vec3f* pPos) const {
    if (mLastGroundPolygon->isValid()) {
        NebulaMtxMultVec(*mLastGroundPolygon->getBaseMtx(), &mLastGroundLocalPos, pPos);
    } else {
        pPos->zero();
    }
}

const Vec3f* Mario::getLastGroundEdgeNrm(u32 idx) const {
    return mLastGroundPolygon->getEdgeNormal(idx);
}

u32 Mario::getLastGroundEdgeIndex(const Vec3f& rPos, const Vec3f& rDir) const {
    const Vec3f* edge0 = mLastGroundPolygon->calcAndGetEdgeNormal(0);
    const Vec3f* edge1 = mLastGroundPolygon->calcAndGetEdgeNormal(1);
    const Vec3f* edge2 = mLastGroundPolygon->calcAndGetEdgeNormal(2);

    f32 score0 = rDir.dot(*edge0);
    f32 score1 = rDir.dot(*edge1);
    f32 score2 = rDir.dot(*edge2);

    Vec3f centerAvg(*mLastGroundPolygon->calcAndGetPos(0) + *mLastGroundPolygon->calcAndGetPos(1) + *mLastGroundPolygon->calcAndGetPos(2));

    centerAvg *= 1.0f / 3.0f;

    Vec3f outDir(centerAvg - rPos);
    MR::normalizeOrZero(&outDir);

    const f32 add0 = outDir.dot(*edge0);
    const f32 add1 = outDir.dot(*edge1);
    const f32 add2 = outDir.dot(*edge2);
    if (add0 > 0.0f) {
        score0 += add0;
    }

    if (add1 > 0.0f) {
        score1 += add1;
    }

    if (add2 > 0.0f) {
        score2 += add2;
    }

    if (score0 < score1 && score0 < score2) {
        return 0;
    }

    if (score1 < score0 && score1 < score2) {
        return 1;
    }

    return 2;
}

void Mario::pushedByReaction() {
    addVelocity(mReactionVec);
    mReactionVec *= kReactionDecay;
}

void Mario::addReaction(const Vec3f& rReact) {
    mReactionVec += rReact;
}

void Mario::tryPushToVelocity() {
    pushedByWind();
    pushedByReaction();

    if (!MR::isNearZero(mPushVec)) {
        if (isSwimming()) {
            Vec3f pushVec(mPushVec);
            pushVec += mPushVelocity;
            mPushVec.zero();
            mPushVelocity.zero();
            createAtField(true, pushVec.length());

            for (u32 i = 0; i < mTouchedWalls.size(); i++) {
                Vec3f projected;
                if (MR::vecKillElement(pushVec, *mTouchedWalls[i].getNormal(0), &projected) < 0.0f) {
                    pushVec = projected;
                }
            }

            mVelocity += pushVec;
        } else if (!isStatusActive(MarioStatus_Hang)) {
            if (isStatusActive(MarioStatus_Bury)) {
                closeStatus(mBury);
            } else {
                if (mMovementStates.jumping && !mMovementStates.onGround) {
                    Vec3f pushDir(mPushVec);
                    MR::normalizeOrZero(&pushDir);

                    if (mJumpVec.dot(mPushVec) < 0.0f) {
                        cutVecElementFromJumpVec(pushDir);
                    }

                    if (mVelocity.dot(mPushVec) < 0.0f) {
                        MR::vecKillElement(mVelocity, pushDir, &mVelocity);
                    }
                } else if (isDamaging() && mMovementStates.onGround) {
                    MR::vecKillElement(mPushVec, *getGravityVec(), &mPushVec);
                }

                mVelocity += mPushVec;
            }
        }
    }

    mVelocity += mPushVelocity;
    mPushVec.zero();
    mPushVelocity.zero();
}

void Mario::push(const Vec3f& rVec) {
    mPushVec += rVec;
}

void Mario::push2(const Vec3f& rVec) {
    mPushVelocity += rVec;
}

void Mario::powerAreaMove() {
    mAreaMoveVelocity.zero();

    if (mMovementStates.onGround && !mMovementStates.jumping && mFloorCode == 28 && MR::calcAreaMoveVelocity(&mAreaMoveVelocity, mPosition)) {
        if (mMovementStates.frontWall || mMovementStates.backWall || mMovementStates.sideWall) {
            const Vec3f* norm = &getWallNorm();
            f32 dot = norm->dot(mAreaMoveVelocity);

            if (dot < 0.0f) {
                MR::vecKillElement(mAreaMoveVelocity, *norm, &mAreaMoveVelocity);
                MR::vecKillElement(mAreaMoveVelocity, *getGravityVec(), &mAreaMoveVelocity);
            }
        }

        addVelocity(mAreaMoveVelocity);
    }
}

void Mario::powerRailMove() {
    mRailMoveVelocity.zero();

    if (mMovementStates.onGround && !mMovementStates.jumping && mFloorCode == 0x1B) {
        const LiveActor* host = getGroundPolygon()->mSensor->mHost;
        if (host == nullptr) {
            return;
        }

        Vec3f railDir;
        MR::calcNearestRailDirection(&railDir, host, mPosition);

        s32 arg = -1;
        MR::getRailArg3NoInit(host, &arg);
        s32 speed = arg;
        if (speed == -1) {
            speed = 10;
        }

        MR::normalizeOrZero(&railDir);

        if (mMovementStates.frontWall || mMovementStates.backWall || mMovementStates.sideWall) {
            const Vec3f* wallNorm = &getWallNorm();
            if (wallNorm->dot(railDir) < 0.0f) {
                MR::vecKillElement(railDir, *wallNorm, &railDir);
                MR::vecKillElement(railDir, *getGravityVec(), &railDir);
            }
        }

        Vec3f velocity(railDir);
        velocity.scale(speed);
        mRailMoveVelocity = velocity;
        addVelocity(mRailMoveVelocity);
    }
}

void Mario::recordJumpEnforceMove() {
    mActionStates.jumpOnEnforcer = 1;
    mJumpEnforceSensor = mRelativeSensor;
    mEnforceJumpVec = mRelativeMove;
    *mJumpEnforcePolygon = *mRelativePolygon;
}

void Mario::doEnforceJump(f32 param) {
    if (mJumpVec.dot(mRelativeMove) < 0.0f) {
        mDrawStates.keepJumpParam = 1;
        initActiveJumpVec();
    }

    mEnforceJumpVec = mRelativeMove * param;

    f32 element = MR::vecKillElement(mEnforceJumpVec, getAirGravityVec(), &mEnforceJumpVec);

    mJumpVec = mEnforceJumpVec + mJumpVec;

    if (element < 0.0f) {
        mJumpVec += getAirGravityVec() * element;
    }

    mVelocity -= mRelativeMove;
    invalidateRelativePosition();
}

void Mario::pushedByWind() {
    f32 windMag = mWindVec.length();

    Vec3f windDir(mWindVec);
    MR::normalizeOrZero(&windDir);

    if (MR::isNearZero(windDir)) {
        if (isAnimationRun("向かい風ふんばり")) {
            stopAnimation("向かい風ふんばり");
        }

        if (isAnimationRun("向かい風走り")) {
            stopAnimation("向かい風走り");
        }

        return;
    }

    if (mMovementStates.jumping) {
        const MarioConstTable* table = mActor->getConst().getTable();
        mJumpVec += mWindVec * table->mWindJumpingFriction;
        return;
    }

    if (mTargetWalkSpeedIndex == 0) {
        const MarioConstTable* table = mActor->getConst().getTable();
        if (windMag > table->mWindSlideLimit) {
            windMag -= table->mWindSlideLimit;
            windMag *= table->mWindSlideFriction;
            mPushVec += windDir * windMag;
            changeAnimation("向かい風ふんばり");
        }
        return;
    }

    Vec3f scaled(windDir * windMag);

    if (mVelocity.dot(windDir) > 0.0f) {
        const MarioConstTable* table = mActor->getConst().getTable();
        mPushVec += scaled * table->mWindForwardFriction;
        stopAnimation("向かい風ふんばり");
        stopAnimation("向かい風走り");
        return;
    }

    Vec3f killed;
    f32 element = MR::vecKillElement(scaled + mVelocity, windDir, &killed);

    const MarioConstTable* table = mActor->getConst().getTable();
    if (element > table->mWindSlideLimit) {
        element -= table->mWindSlideLimit;
        element *= table->mWindSlideFrictionAgainst;
    } else if (element > 0.0f) {
        element = 0.0f;
    }

    clearVelocity();

    scaled = killed + windDir * element;

    changeAnimation("向かい風走り");
    mPushVec += scaled;
}
