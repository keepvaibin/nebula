#include "Game/AreaObj/CubeCamera.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Map/SunshadeMapHolder.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioDamage.hpp"
#include "Game/Player/MarioHang.hpp"
#include "Game/Player/MarioMapCode.hpp"
#include "Game/Player/MarioRecovery.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_math.h>

bool Mario::isIgnoreTriangle(const Triangle* pTriangle) {
    return MR::isNearZero(pTriangle->getNormal(0)->dot(*getGravityVec()));
}

void Mario::checkBaseTransBall() {
    Vec3f center = mPosition + mVelocity + mHeadVec * 50.0f;
    u32 count = Collision::checkStrikeBallToMap(center, 50.0f, nullptr, nullptr);

    for (u32 i = 0; i < count; i++) {
        doSwimmingHitCheck(Collision::getStrikeInfoMap(i), 0);
    }

    center = mPosition + mVelocity - mHeadVec * 40.0f;
    count = Collision::checkStrikeBallToMap(center, 50.0f, nullptr, nullptr);

    for (u32 i = 0; i < count; i++) {
        doSwimmingHitCheck(Collision::getStrikeInfoMap(i), 1);
    }

    center = mPosition + mVelocity;
    count = Collision::checkStrikeBallToMap(center, 40.0f, nullptr, nullptr);

    for (u32 i = 0; i < count; i++) {
        doSwimmingHitCheck(Collision::getStrikeInfoMap(i), 2);
    }
}

void Mario::createAtField(bool force, f32 radius) {
    u32 hits;
    mTouchedWalls.clear();
    f32 checkRadius = 50.0f;
    f32 offset = 50.0f;
    u32 count = 2;

    if (isSwimming()) {
        checkRadius = 40.0f;
        offset = 0.0f;
    }

    if (mMovementStates.squat) {
        checkRadius = 40.0f;
        count = 1;
        offset = 40.0f;
    }

    if (force) {
        checkRadius = radius;

        if (radius < 40.0f) {
            checkRadius = 40.0f;
        }

        offset = 0.0f;
    }

    f32 minAngle = 60.0f;
    f32 maxAngle = 120.0f;

    for (u32 i = 0; i < count; i++) {
        Vec3f center = mPosition - getAirGravityVec() * offset * (i + 1);

        if (force && mMovementStates.tornado && mTornadoTimer > 1) {
            hits = Collision::checkStrikeBallToMap(center, checkRadius, nullptr, nullptr);
        } else {
            hits = Collision::checkStrikeBallToMapWithThickness(center, checkRadius, checkRadius, nullptr, nullptr);
        }

        for (u32 j = 0; j < hits; j++) {
            const HitInfo* hit = Collision::getStrikeInfoMap(j);
            const Triangle* triangle = &hit->mParentTriangle;

            if (force && mMovementStates.tornado && mTornadoTimer > 1) {
                if (calcAngleD(*triangle->getNormal(0)) < minAngle) {
                    continue;
                }

                if (calcAngleD(*triangle->getNormal(0)) > maxAngle) {
                    continue;
                }

                Vec3f horizontal;

                if (MR::abs(MR::vecKillElement(hit->mHitPos - center, getAirGravityVec(), &horizontal)) > 80.0f) {
                    continue;
                }

                if (hit->isCollisionAtEdge() && MR::diffAngleAbsHorizontal(mJumpVec, -*triangle->getNormal(0), getAirGravityVec()) < 0.7853982f) {
                    continue;
                }
            }

            if (!force && !isSwimming()) {
                if (triangle->mSensor != nullptr && mActor->selectPushOff(triangle->mSensor)) {
                    continue;
                }

                if (isThroughWall(triangle)) {
                    continue;
                }

                const Vec3f* normal = triangle->getNormal(0);
                f32 angle = mFloorCodeReader->getCode(triangle) == CollisionFloorCode_NoSlip ? 60.0f : 45.0f;

                if (mMovementStates.mode2D) {
                    if (!MR::isSameMtx(*triangle->getBaseMtx(), *triangle->getPrevBaseMtx()) && mGroundPolygon->mSensor != triangle->mSensor) {
                        angle = -1.0f;
                        push(*normal * hit->mDepth);
                    }
                }

                if (calcAngleD(*normal) < angle) {
                    continue;
                }
            }

            mTouchedWalls.push_back(*triangle);
        }
    }
}

void Mario::doSwimmingHitCheck(const HitInfo* pHit, u32 type) {
    const Triangle* triangle = &pHit->mParentTriangle;

    if (MR::isThroughPolygon(triangle)) {
        return;
    }

    if (isThroughWall(triangle)) {
        return;
    }

    if (damagePolygonCheck(triangle)) {
        return;
    }

    Vec3f velocity(mVelocity);
    const Vec3f* normal = triangle->getNormal(0);

    if (velocity.dot(-*normal) < 0.0f) {
        return;
    }

    mSwim->addVelocity(*normal, pHit->mDepth);

    if (type == 0) {
        mCurrentState->hitWall(*normal, triangle->mSensor);
    } else {
        mCurrentState->hitPoly(type, *normal, triangle->mSensor);
    }
}

namespace {
    MR::GrowBuffer< Triangle > sPunchTriangles;
    MR::GrowBuffer< Triangle > sHeadHitTriangles;
}  // namespace

void Mario::doSpinPunchAroundPolygons() {
    if (!mActor->isPunching()) {
        return;
    }

    if (!mActor->isInPunchTimerRange()) {
        return;
    }

    Vec3f center = mPosition + mVelocity + mHeadVec * 80.0f;
    const Vec3f start = center + mFrontVec * 120.0f + mSideVec * 120.0f + mHeadVec * 120.0f;
    const Vec3f end = center - mFrontVec * 120.0f - mSideVec * 120.0f - mHeadVec * 120.0f;
    u32 room = 256;
    u32 count = MR::createAreaPolygonList(sPunchTriangles.room(room), room, start, end);
    while (count >= room) {  // full: the box may hold more
        room *= 2;
        count = MR::createAreaPolygonList(sPunchTriangles.room(room), room, start, end);
    }
    Triangle* const triangles = sPunchTriangles.mItems;

    for (u32 i = 0; i < count; i++) {
        sendPunch(triangles[i].mSensor, true);
    }
}

void Mario::checkMap() {
    calcShadowPos();

    if (isStatusActive(MarioStatus_Swim)) {
        mGroundPos = mShadowPos;
    }

    Vec3f gravity(*getGravityVec());

    if (mMovementStates.onGround && isSlipPolygon(mGroundPolygon)) {
        gravity = mGroundDown;
    }

    if (isUseSimpleGroundCheck()) {
        if (!mMovementStates.groundMissing) {
            mVerticalSpeed = (mGroundPos - mPosition).dot(gravity);
        } else {
            mVerticalSpeed = (mShadowPos - mPosition).dot(gravity);
        }

        if (!mPrevMovementStates.simpleGroundCheck) {
            mLookOfsBody.zero();
        }

    } else {
        mVerticalSpeed = (mShadowPos - mPosition).dot(gravity);
    }

    if (mVerticalSpeed < 0.0f) {
        mVerticalSpeed = 0.0f;
    }

    if (mMovementStates.slopeSlide && calcAngleD(mGroundNorm) >= 30.0f) {
        Vec3f start = mPosition + mGroundNorm * 100.0f;
        mMovementStates.slopeFloorFound = MR::getFirstPolyOnLineToMap(&mSlopeFloorPos, mSlopeFloorPolygon, start, -mSlopeDownDir * 150.0f);
    } else {
        mMovementStates.slopeFloorFound = false;
    }
}

f32 Mario::calcDistToCeil(bool saveSensor) {
    Triangle triangle;
    Vec3f hitPos;
    f32 offset = 30.0f;

    if (saveSensor) {
        mCeilSensor = nullptr;
    }

    if (isStatusActive(MarioStatus_Hang)) {
        offset = 0.0f;
    }

    while (true) {
        Vec3f start = mPosition + getAirGravityVec() * offset;
        bool hit = MR::getFirstPolyOnLineToMap(&hitPos, &triangle, start, -getAirGravityVec() * (200.0f + offset));

        if (isThroughWall(&triangle)) {
            hit = false;
        }

        if (!hit) {
            break;
        }

        bool pressGround = false;

        if (mGroundPolygon->mSensor != nullptr && MR::isSensorPressObj(mGroundPolygon->mSensor)) {
            pressGround = true;
        }

        if (!MR::isSensorPressObj(triangle.mSensor) && mFloorCodeReader->getCode(&triangle) != CollisionFloorCode_Press && !pressGround &&
            mFloorCode != CollisionFloorCode_Press) {
            if ((hitPos - mPosition).dot(*getGravityVec()) >= 0.0f) {
                if (offset <= 1.0f) {
                    offset = -10.0f;
                    continue;
                }

                return 200.0f;
            }
        }

        mCeilDistNorm = *triangle.getNormal(0);

        if (saveSensor) {
            mCeilSensor = triangle.mSensor;
        }

        f32 distance = (hitPos - mPosition).length();

        if (!mCeilPolygon->isValid()) {
            *mCeilPolygon = triangle;
        }

        return distance;
    }

    return 200.0f;
}

f32 Mario::calcDistToCeilOnPress() {
    if (!mPressFloorPolygon->isValid() || !mPressCeilPolygon->isValid()) {
        return 0.0f;
    }

    Vec3f horizontal;
    f32 distance1 = MR::vecKillElement(mPosition - *mPressFloorPolygon->calcAndGetPos(0), *mPressFloorPolygon->calcAndGetNormal(0), &horizontal);
    f32 distance2 = MR::vecKillElement(mPosition - *mPressCeilPolygon->calcAndGetPos(0), *mPressCeilPolygon->calcAndGetNormal(0), &horizontal);

    if (mPressFloorPolygon->getNormal(0)->dot(mAirGravityVec) < 0.0f) {
        f32 distance = MR::vecKillElement(*mPressFloorPolygon->calcAndGetPos(0) - mPosition, *mPressFloorPolygon->calcAndGetNormal(0), &horizontal);
        mPosition += *mPressFloorPolygon->getNormal(0) * distance;
    } else {
        f32 distance = MR::vecKillElement(*mPressCeilPolygon->calcAndGetPos(0) - mPosition, *mPressCeilPolygon->calcAndGetNormal(0), &horizontal);
        mPosition += *mPressCeilPolygon->getNormal(0) * distance;
    }

    mActor->mPosition.set(mPosition);

    if (distance1 > distance2) {
        return distance2;
    }

    return distance1;
}

f32 Mario::calcDistToCeilHead() {
    Triangle triangle;
    Vec3f hitPos;
    Vec3f start = mActor->mHeadPos - mFrontVec * 40.0f;

    if (MR::getFirstPolyOnLineToMap(&hitPos, &triangle, start, -getAirGravityVec() * 80.0f)) {
        *getTmpPolygon() = triangle;
        return (hitPos - mPosition).length();
    }

    return 80.0f;
}

void Mario::fixTransBetweenWall(const Vec3f& rFirst, const Vec3f& rSecond) {
    Vec3f center = (rFirst + rSecond) * 0.5f;
    MR::vecKillElement(center - mPosition, *getGravityVec(), &center);
    setTrans(mPosition + center);
}

f32 Mario::calcDistWidth() {
    Triangle triangle;
    Vec3f hitPos;

    if (mMovementStates.frontWall) {
        if (MR::getFirstPolyOnLineToMap(&hitPos, &triangle, mFrontWallPos, *mFrontWallTriangle->getNormal(0) * 100.0f)) {
            fixTransBetweenWall(hitPos, mFrontWallPos);
            return (hitPos - mFrontWallPos).length();
        }
    }

    if (mMovementStates.backWall) {
        if (MR::getFirstPolyOnLineToMap(&hitPos, &triangle, mBackWallPos, *mBackWallTriangle->getNormal(0) * 100.0f)) {
            fixTransBetweenWall(hitPos, mBackWallPos);
            return (hitPos - mBackWallPos).length();
        }
    }

    if (mMovementStates.sideWall) {
        if (MR::getFirstPolyOnLineToMap(&hitPos, &triangle, mSideWallPos, *mSideWallTriangle->getNormal(0) * 100.0f)) {
            fixTransBetweenWall(hitPos, mSideWallPos);
            return (hitPos - mSideWallPos).length();
        }
    }

    Vec3f center = mPosition + mHeadVec * 80.0f;
    Vec3f firstPos;
    Vec3f secondPos;
    u32 count = Collision::checkStrikeBallToMap(center, 30.0f, nullptr, nullptr);

    if (count < 2) {
        return 100.0f;
    }

    f32 width = 100.0f;

    for (u32 i = 0; i < count; i++) {
        const HitInfo* first = Collision::getStrikeInfoMap(i);
        Vec3f firstNormal(*first->mParentTriangle.getNormal(0));

        for (u32 j = i + 1; j < count; j++) {
            const HitInfo* second = Collision::getStrikeInfoMap(j);
            Vec3f secondNormal(*second->mParentTriangle.getNormal(0));

            if (firstNormal.dot(secondNormal) >= -0.707f) {
                continue;
            }

            if ((first->mHitPos - center).dot(second->mHitPos - center) > 0.0f) {
                continue;
            }

            f32 distance = (first->mHitPos - second->mHitPos).length();

            if (distance < width) {
                width = distance;
                firstPos = first->mHitPos;
                secondPos = second->mHitPos;
            }
        }
    }

    if (width < 100.0f) {
        fixTransBetweenWall(firstPos, secondPos);
    }

    return width;
}

void Mario::checkAllWall(const Vec3f& rPosition, f32 radius) {
    Vec3f center(rPosition);
    Vec3f front(mFrontVec);
    mStepPolygon->mIdx = -1;
    mBumpPolygon->mIdx = -1;

    if (mMovementStates.tornado) {
        Vec3f direction;
        mActor->getLastMove(&direction);
        MR::vecKillElement(direction, *getGravityVec(), &direction);

        if (!MR::isNearZero(direction)) {
            MR::normalize(&direction);
            front = direction;
        }
    }

    const HitInfo* walls[3];

    for (s32 i = 0; i < 3; i++) {
        walls[i] = nullptr;
    }

    s32 count;
    bool forwardCheck = false;

    if (getPlayerMode() == PlayerMode_Teresa) {
        radius = 110.0f;
        count = Collision::checkStrikeBallToMapWithThickness(center, radius, radius, nullptr, nullptr);

        if (!count) {
            forwardCheck = true;
            center += mFrontVec * 120.0f;
            count = Collision::checkStrikeBallToMapWithThickness(center, 25.0f, 25.0f, nullptr, nullptr);
        }

    } else {
        count = Collision::checkStrikeBallToMap(center, radius, nullptr, nullptr);
    }

    bool through = false;

    for (u32 i = 0; i < count; i++) {
        const HitInfo* hit = Collision::getStrikeInfoMap(i);
        const Triangle* triangle = &hit->mParentTriangle;
        Vec3f normal(*triangle->getNormal(0));
        Vec3f direction(hit->mHitPos);
        direction -= center;
        MR::normalizeOrZero(&direction);

        if (mMovementStates.mode2D && MR::abs(normal.dot(m2DPlaneNormal)) > 0.707f) {
            continue;
        }

        f32 angle = 180.0f * (marioAcos(-getGravityVec()->dot(normal)) / 3.14159f);
        Vec3f fromBase(hit->mHitPos);
        fromBase -= mPosition;
        MR::normalizeOrZero(&fromBase);

        if (angle >= mActor->getConst().getTable()->mFlatAngle && direction.dot(mFrontVec) > 0.0f && fromBase.dot(mFrontVec) > 0.0f) {
            if (calcAngleD(normal) <= mActor->getConst().getTable()->mSlipAngle && MR::diffAngleAbs(mGroundNorm, normal) > 30.0f) {
                *mBumpPolygon = *triangle;
                mBumpHitPos = hit->mHitPos;
            }
        }

        if (MR::abs(normal.dot(mHeadVec)) > 0.5f) {
            if (direction.dot(*getGravityVec()) > 0.0f && direction.dot(mFrontVec) > 0.707f) {
                *mStepPolygon = *triangle;
                mStepHitPos = hit->mHitPos;
            }

            continue;
        }

        f32 facing = normal.dot(front);
        s32 side;

        if (facing >= mActor->getConst().getTable()->mWallBackAngleRange) {
            side = 2;
        } else if (facing <= -mActor->getConst().getTable()->mWallFrontAngleRange) {
            side = 0;
        } else {
            side = 1;
        }

        if (walls[side] != nullptr && walls[side]->mDepth > hit->mDepth) {
            continue;
        }

        if (isThroughWall(triangle)) {
            through = true;
            mActor->mBindOn = false;
        } else {
            walls[side] = hit;
        }
    }

    if (!through && getPlayerMode() == PlayerMode_Teresa && !isStatusActive(MarioStatus_Recovery)) {
        mActor->mBindOn = true;
    }

    if (count >= 1U) {
        f32 minAngle = 180.0f;
        f32 maxAngle = 0.0f;
        u32 minIndex = 0;
        u32 maxIndex = 0;

        for (u32 i = 0; i < count; i++) {
            const HitInfo* hit = Collision::getStrikeInfoMap(i);
            const Triangle* triangle = &hit->mParentTriangle;

            if (mMovementStates.mode2D && MR::abs(triangle->getNormal(0)->dot(m2DPlaneNormal)) > 0.707f) {
                continue;
            }

            f32 angle = calcAngleD(*triangle->getNormal(0));

            if (angle < minAngle) {
                minAngle = angle;
                minIndex = i;
            }

            if (angle > maxAngle) {
                maxAngle = angle;
                maxIndex = i;
            }
        }

        f32 groundAngle = 0.0f;

        if (mMovementStates.onGround) {
            groundAngle = calcAngleD(mGroundNorm);

            if (groundAngle < minAngle) {
                minAngle = groundAngle;
            }

            if (groundAngle > maxAngle) {
                maxAngle = groundAngle;
            }
        }

        if (maxAngle - minAngle < 60.0f && maxAngle - minAngle > 5.0f && maxAngle < 80.0f) {
            mDrawStates.wallGravity = true;

            if (maxAngle > groundAngle) {
                mWallNorm = *Collision::getStrikeInfoMap(maxIndex)->mParentTriangle.getNormal(0);
            } else if (minAngle < groundAngle) {
                mWallNorm = *Collision::getStrikeInfoMap(minIndex)->mParentTriangle.getNormal(0);
            }
        }

        if (mMovementStates.onGround && maxAngle > 60.0f && maxAngle != groundAngle && maxAngle < 80.0f) {
            Vec3f hitPos(Collision::getStrikeInfoMap(maxIndex)->mHitPos);
            Vec3f direction(center);
            direction -= hitPos;

            if (!MR::normalizeOrZero(&direction)) {
                mDrawStates.wallSlope = true;
                Vec3f side = Collision::getStrikeInfoMap(maxIndex)->mParentTriangle.getNormal(0)->cross(getAirGravityVec());
                MR::normalize(&side);
                Vec3f slope = mGroundNorm.cross(side);
                MR::normalize(&slope);
                mWallSlope = slope;
            }
        }
    }

    HitInfo pointHit;

    if (MR::checkStrikePointToMap(center, &pointHit)) {
        Vec3f normal(*pointHit.mParentTriangle.getNormal(0));

        if (MR::abs(normal.dot(mHeadVec)) <= 0.5f) {
            f32 facing = normal.dot(front);
            s32 side;

            if (facing >= 0.707f) {
                side = 2;
            } else if (facing <= -0.707f) {
                side = 0;
            } else {
                side = 1;
            }

            walls[side] = &pointHit;
        }
    }

    mMovementStates.frontWall = false;
    mMovementStates.backWall = false;
    mMovementStates.sideWall = false;
    mMovementStates.frontWallGlancing = false;
    mMovementStates.sideWallCorner = false;

    if (walls[0]) {
        Vec3f direction(walls[0]->mHitPos);
        direction -= center;
        MR::normalizeOrZero(&direction);
        f32 dot = direction.dot(*walls[0]->mParentTriangle.getNormal(0));
        *mFrontWallTriangle = walls[0]->mParentTriangle;
        mFrontWallPos = walls[0]->mHitPos;
        f32 threshold = -0.999f;

        if (isStatusActive(MarioStatus_Wall)) {
            threshold = -0.9f;
        }

        if (dot < threshold) {
            mMovementStates.frontWall = true;
        } else {
            mMovementStates.frontWallGlancing = true;
        }

        if (mMovementStates.onGround && getMovementStates().frontLedge && mMovementStates.frontFloorFound) {
            if ((mFrontFloorPos - mFrontWallPos).dot(*getGravityVec()) >= 0.0f) {
                mMovementStates.frontWall = false;
            }
        }

        if (!mCameraTurnHold && !mMovementStates.frontWall) {
            if (calcAngleD(*mFrontWallTriangle->getNormal(0)) < mActor->getConst().getTable()->mSlipAngle) {
                bool noSlip = !isSlipFloorCode(mFloorCodeReader->getCode(mFrontWallTriangle));

                if (noSlip && walls[0]->mParentTriangle.getNormal(0)->dot(mFrontVec) < 0.0f && direction.dot(mFrontVec) > 0.0f &&
                    direction.dot(*walls[0]->mParentTriangle.getNormal(0)) < 0.0f) {
                    mDrawStates.wallGravity = true;
                    mWallNorm = *mFrontWallTriangle->getNormal(0);
                }
            }
        }
    }

    if (!forwardCheck) {
        if (walls[1]) {
            bool valid = true;

            if (isPlayerModeTeresa()) {
                if (walls[1]->mParentTriangle.getNormal(0)->dot(walls[1]->mHitPos - center) >= 0.0f) {
                    valid = false;
                }
            }

            if (valid) {
                *mSideWallTriangle = walls[1]->mParentTriangle;
                mSideWallPos = walls[1]->mHitPos;
                mMovementStates.sideWall = true;

                if (walls[1]->isCollisionAtCorner() || walls[1]->isCollisionAtEdge()) {
                    mMovementStates.sideWallCorner = true;
                }
            }
        }

        if (walls[2]) {
            bool valid = true;

            if (mMovementStates.frontWall) {
                Vec3f backDistance = mBackWallPos - center;
                Vec3f frontDistance = mFrontWallPos - center;

                if (backDistance.dot(frontDistance) >= 0.0f && backDistance.length() >= frontDistance.length()) {
                    valid = false;
                }
            }

            if (isPlayerModeTeresa()) {
                if (walls[2]->mParentTriangle.getNormal(0)->dot(walls[2]->mHitPos - center) >= 0.0f) {
                    valid = false;
                }
            }

            if (valid) {
                *mBackWallTriangle = walls[2]->mParentTriangle;
                mBackWallPos = walls[2]->mHitPos;
                mMovementStates.backWall = true;
            }
        }
    }

    updateWallFloorCode();
    mWallGravityVec = -mWallNorm;

}

void Mario::calcFrontFloor() {
    Triangle wall;
    Triangle floor;
    Vec3f start;
    Vec3f wallPos;
    Vec3f floorPos;
    Vec3f direction;
    Triangle* pFloor = mFrontFloorPolygon;
    mMovementStates.frontLedge = false;
    mMovementStates.frontFloorFound = false;
    pFloor->mIdx = -1;
    mFrontFloorHeight = 0.0f;
    bool hit;

    if (mMovementStates.frontWall) {
        wallPos = mFrontWallPos;
        start = mActor->mCenterPos;
        hit = true;
    } else {
        direction = mFrontVec * 100.0f;
        start = mPosition - *getGravityVec() * 8.0f;

        if (mMovementStates.tornado && !MR::isNearZero(mMoveFrontVec)) {
            MR::vecKillElement(mMoveFrontVec, *getGravityVec(), &direction);
            direction.setLength(200.0f);
        }

        hit = MR::getFirstPolyOnLineToMap(&wallPos, &wall, start, direction);
    }

    if (hit) {
        mFrontWallDist = (wallPos - start).dot(mFrontVec);
        Vec3f normal(*wall.getNormal(0));
        f32 dot = normal.dot(getAirGravityVec());

        if (MR::abs(dot) < 0.1f) {
            f32 angle = marioAcos(-dot);
            start = wallPos - getAirGravityVec() * 200.0f;
            start += mFrontVec * 20.0f;
            direction = getAirGravityVec() * 210.0f;
            hit = MR::getFirstPolyOnLineBFast(start, direction, &floorPos, &floor);

            if (hit && floor.getNormal(0)->dot(getAirGravityVec()) > -0.9f) {
                hit = false;
            }

            if (hit) {
                mFrontFloorHeight = (wallPos - floorPos).dot(getAirGravityVec());

                if (mFrontFloorHeight > 0.0f) {
                    mMovementStates.frontLedge = true;
                }

                f32 offset = 0.0f;

                if (angle < 1.5707964f && angle != 0.0f) {
                    offset = mFrontFloorHeight / (Angle::sinRadian(angle) / Angle::cosRadian(angle));
                }

                Vec3f originalPos(floorPos);
                floorPos -= mFrontVec * (15.0f + offset);

                if (!MR::isExistMapCollision(floorPos - getAirGravityVec() * 5.0f, *getGravityVec() * 10.0f)) {
                    floorPos = originalPos;
                } else {
                    mFrontFloorPos = floorPos;
                    mMovementStates.frontFloorFound = true;
                    *mFrontFloorPolygon = floor;
                }

            } else {
                mFrontFloorHeight = 400.0f;
            }
        }

    } else {
        mFrontFloorHeight = 0.0f;
    }

    if (mMovementStates.onGround && calcAngleD(mGroundNorm) >= 25.0f) {
        mMovementStates.frontFloorFound = false;
    }
}

const Vec3f& Mario::getWallNorm() const {
    if (mMovementStates.frontWall) {
        return *mFrontWallTriangle->getNormal(0);
    }

    if (mMovementStates.backWall) {
        return *mBackWallTriangle->getNormal(0);
    }

    if (mMovementStates.sideWall) {
        return *mSideWallTriangle->getNormal(0);
    }

    if (mFrontWallTriangle->isValid()) {
        return *mFrontWallTriangle->getNormal(0);
    }

    return mFrontVec;
}

// No wall: a zero normal.
static const Vec3f sZeroNormal(0.0f, 0.0f, 0.0f);

const Vec3f& Mario::getSideWallNorm() const {
    if (mMovementStates.sideWall) {
        return *mSideWallTriangle->getNormal(0);
    }

    return sZeroNormal;
}

const Vec3f& Mario::getFrontWallNorm() const {
    if (mMovementStates.frontWall) {
        return *mFrontWallTriangle->getNormal(0);
    }

    return sZeroNormal;
}

const Vec3f& Mario::getBackWallNorm() const {
    if (mMovementStates.backWall) {
        return *mBackWallTriangle->getNormal(0);
    }

    return sZeroNormal;
}

const Vec3f& Mario::getWallPos() const {
    if (mMovementStates.frontWall) {
        return mFrontWallPos;
    }

    if (mMovementStates.backWall) {
        return mBackWallPos;
    }

    if (mMovementStates.sideWall) {
        return mSideWallPos;
    }

    return mActor->mCenterPos;
}

const Triangle* Mario::getWallPolygon() const {
    if (mMovementStates.frontWall) {
        return mFrontWallTriangle;
    }

    if (mMovementStates.backWall) {
        return mBackWallTriangle;
    }

    if (mMovementStates.sideWall) {
        return mSideWallTriangle;
    }

    if (mFrontWallTriangle->isValid()) {
        return mFrontWallTriangle;
    }

    return nullptr;
}

const Triangle* Mario::getGroundPolygon() const {
    return mGroundPolygon;
}

void Mario::updateFloorCode() {
    s32 code = mFloorCodeReader->getCode(mGroundPolygon);

    if (code != -1) {
        mFloorCode = code;
    }

    code = mFloorCodeReader->getCode(mShadowPolygon);

    if (code != -1) {
        mPolygonFloorCode = code;
    }

    if (mActor->mBeeWallWalk) {
        mSlipGraceTimer = 15;
        return;
    }

    f32 angle = calcPolygonAngleD(mGroundPolygon);

    if (angle > mActor->getConst().getTable()->mSlipAngle) {
        if (angle > mActor->getConst().getTable()->mForceWallAngle) {
            mSlipGraceTimer = 0;
        }

        if (mSlipGraceTimer) {
            mSlipGraceTimer--;
        }

        if (!mSlipGraceTimer) {
            mFloorCode = 0x80;
        }

    } else {
        bool noSlip = !isSlipFloorCode(mFloorCode);

        if (noSlip) {
            mSlipGraceTimer = 15;
        }
    }
}

void Mario::updateWallFloorCode() {
    if (mMovementStates.frontWall || mMovementStates.frontWallGlancing) {
        s32 code = mFloorCodeReader->getCode(mFrontWallTriangle);

        if (code != -1) {
            mWallFloorCodes[0] = code;
        }
    }

    if (mMovementStates.backWall) {
        s32 code = mFloorCodeReader->getCode(mBackWallTriangle);

        if (code != -1) {
            mWallFloorCodes[1] = code;
        }
    }

    if (mMovementStates.sideWall) {
        s32 code = mFloorCodeReader->getCode(mSideWallTriangle);

        if (code != -1) {
            mWallFloorCodes[2] = code;
        }
    }
}

void Mario::saveLastSafetyTrans() {
    if (isStatusActive(MarioStatus_FireDamage)) {
        return;
    }

    if (isStatusActive(MarioStatus_Recovery)) {
        return;
    }

    if (!mHitStates.groundAllProbes) {
        return;
    }

    if (mNotSafetyTimer) {
        mNotSafetyTimer--;
        return;
    }

    switch (mFloorCode) {
    case CollisionFloorCode_Normal:
    case CollisionFloorCode_NoSlip:
    case CollisionFloorCode_Sand:
    case CollisionFloorCode_Glass:
    case CollisionFloorCode_WaterBottomH:
    case CollisionFloorCode_WaterBottomM:
    case CollisionFloorCode_WaterBottomL:
    case CollisionFloorCode_Wet:
    case CollisionFloorCode_NoStampSand:
        break;
    default:
        return;
    }

    if (mGroundPolygon->mSensor->isType(0x48)) {
        return;
    }

    if (mMovementStates.frontWall) {
        return;
    }

    if (mMovementStates.frontWallGlancing) {
        return;
    }

    if (mMovementStates.backWall) {
        return;
    }

    if (mMovementStates.sideWall) {
        return;
    }

    if (mDrawStates.onSlipFloor) {
        return;
    }

    if (mHitStates.ceilingNear) {
        return;
    }

    if (!(mVerticalSpeed < 10.0f)) {
        return;
    }

    if (!(calcAngleD(*mGroundPolygon->getNormal(0)) < 15.0f)) {
        return;
    }

    if (!MR::isSameMtx(*mGroundPolygon->getBaseMtx(), *mGroundPolygon->getPrevBaseMtx())) {
        return;
    }

    if (mSafetyPolygon->isValid() && MR::isSameMtx(*mSafetyPolygon->getBaseMtx(), mSafetyBaseMtx) && MR::isSameMtx(*mSafetyPolygon->getBaseMtx(), *mSafetyPolygon->getPrevBaseMtx())) {
        mLastSafetyPos = mSafetyPos;
        *mLastSafetyPolygon = *mSafetyPolygon;
        NebulaMtxCopy(mSafetyBaseMtx, mLastSafetyBaseMtx);
    }

    *mSafetyPolygon = *mGroundPolygon;
    NebulaMtxCopy(*mGroundPolygon->getBaseMtx(), mSafetyBaseMtx);
    mSafetyPos = (mPosition * 5.0f + *mGroundPolygon->getPos(0) + *mGroundPolygon->getPos(1) + *mGroundPolygon->getPos(2)) * 0.125f;

    if ((mSafetyPos - mPosition).length() > 50.0f) {
        Vec3f direction = mSafetyPos - mPosition;
        direction.setLength(50.0f);
        mSafetyPos = mPosition + direction;
    }

    mHitStates.safetySaved = true;
}

void Mario::setNotSafetyTimer() {
    mNotSafetyTimer = 2;

    if (mHitStates.safetySaved) {
        mSafetyPolygon->mIdx = -1;
    }
}

const Vec3f* Mario::getLastSafetyTrans(Vec3f* pNormal) const {
    if (pNormal != nullptr) {
        pNormal->set(-getAirGravityVec());
    }

    if (mSafetyPolygon->isValid() && MR::isSameMtx(*mSafetyPolygon->getBaseMtx(), const_cast< Mtx34f& >(mSafetyBaseMtx))) {
        if (pNormal != nullptr) {
            pNormal->set(*mSafetyPolygon->calcAndGetNormal(0));
        }

        return &mSafetyPos;
    }

    if (mLastSafetyPolygon->isValid() && pNormal) {
        pNormal->set(*mLastSafetyPolygon->calcAndGetNormal(0));
    }

    return &mLastSafetyPos;
}

bool Mario::checkCurrentFloorCodeSevere(u32 code) const {
    if (mFloorCode != code) {
        return false;
    }

    Vec3f horizontal;

    if (MR::vecKillElement(mShadowPos - mGroundPos, getAirGravityVec(), &horizontal) > 30.0f) {
        return code == mFloorCodeReader->getCode(mGroundPolygon);
    }

    u32 shadowCode = mFloorCodeReader->getCode(mShadowPolygon);

    if (shadowCode != code) {
        return false;
    }

    return shadowCode == mFloorCodeReader->getCode(mGroundPolygon);
}

bool Mario::isCurrentFloorSink() const {
    return checkCurrentFloorCodeSevere(CollisionFloorCode_Sink) || checkCurrentFloorCodeSevere(CollisionFloorCode_SinkDeathMud) ||
           checkCurrentFloorCodeSevere(CollisionFloorCode_SinkPoison) || checkCurrentFloorCodeSevere(CollisionFloorCode_SinkDeath);
}

bool Mario::isCurrentFloorSand() const {
    if (getPlayer()->mDrawStates.inWater) {
        return false;
    }

    if (getPlayer()->mDrawStates.inShallowWater) {
        return false;
    }

    return checkCurrentFloorCodeSevere(CollisionFloorCode_Sand) || checkCurrentFloorCodeSevere(CollisionFloorCode_NoStampSand);
}

bool Mario::isCurrentShadowFloorDangerAction() const {
    s32 code = mFloorCodeReader->getCode(mShadowPolygon);

    if (code == mFloorCodeReader->getCode(mGroundPolygon)) {
        return false;
    }

    if (isPlayerModeTeresa()) {
        return false;
    }

    switch (code) {
    case CollisionFloorCode_DamageFire:
        if (isPlayerModeIce()) {
            return false;
        }

    case CollisionFloorCode_DamageNormal:
    case CollisionFloorCode_JumpLow:
    case CollisionFloorCode_JumpMiddle:
    case CollisionFloorCode_JumpHigh:
    case CollisionFloorCode_JumpNormal:
    case CollisionFloorCode_DamageElectric:
    case CollisionFloorCode_Sink:
    case CollisionFloorCode_SinkPoison:
    case CollisionFloorCode_Needle:
    case CollisionFloorCode_SinkDeath:
    case CollisionFloorCode_RailMove:
    case CollisionFloorCode_AreaMove:
    case CollisionFloorCode_SinkDeathMud:
    case CollisionFloorCode_JumpParasol:
        return true;
    default:
        return false;
    }
}

void Mario::checkBaseTransPoint() {
    bool inside = MR::checkStrikePointToMap(mPosition - *getGravityVec() * 30.0f, nullptr);
    u32 count = Collision::checkStrikeBallToMap(mPosition, 1.0f, nullptr, nullptr);

    for (u32 i = 0; i < count; i++) {
        const HitInfo* hit = Collision::getStrikeInfoMap(i);
        const Triangle* triangle = &hit->mParentTriangle;

        if (MR::isThroughPolygon(triangle) || isThroughWall(triangle)) {
            continue;
        }

        if (hit->isCollisionAtEdge() || hit->isCollisionAtCorner()) {
            mDrawStates.baseTransPoint = true;
            continue;
        }

        const Vec3f* normal = triangle->getNormal(0);

        if (MR::isNearZero(normal->dot(mGroundNorm), 0.3f) && !inside) {
            continue;
        }

        Vec3f direction(mPrevVelocity);
        MR::normalizeOrZero(&direction);

        if (mPrevVelocity.dot(-*normal) < 0.707f) {
            continue;
        }

        if (MR::abs(mPrevVelocity.dot(*normal)) < hit->mDepth || MR::isNearZero(hit->mDepth, 1.01f)) {
            continue;
        }

        if ((hit->mHitPos - mPosition).dot(mHeadVec) < 0.0f) {
            continue;
        }

        addTrans(*normal * (hit->mDepth - 1.0f));

        if (!mMovementStates.onGround && mMovementStates.jumping && !isRising() && normal->dot(*getGravityVec()) < -0.99f) {
            addVelocity(mFrontVec, 5.0f);
        }
    }
}

void Mario::checkHeadPoint() {
    f32 radius = 40.0f;
    f32 height = 110.0f;
    bool damaging = false;

    if (mMovementStates.squat) {
        height = 50.0f;
    }

    Vec3f center = mPosition - *getGravityVec() * height;

    if (isStatusActive(MarioStatus_Damage)) {
        mActor->calcHeadPos();
        center = mActor->mHeadPos;

        if (!mDamage->mStep) {
            radius = 50.0f;
        }

        damaging = true;
    }

    if (mMovementStates.launchKind) {
        mActor->calcHeadPos();
        center = mActor->mHeadPos + mPrevVelocity * 2.0f;
    }

    if (getPlayerMode() == PlayerMode_Bee && !getPlayer()->mMovementStates.slopeSlide && !getPlayer()->mMovementStates.squat &&
        !isStatusActive(MarioStatus_Wait) && !isStatusActive(MarioStatus_SideStep) && !isStatusActive(MarioStatus_Bury) && !mActor->mBeeWallWalk) {
        if (isStatusActive(MarioStatus_Stick)) {
            return;
        }

        center = mActor->mHeadPos;
    }

    if (isSwimming()) {
        center = mPosition + mHeadVec * 60.0f;
    }

    u32 count = Collision::checkStrikeBallToMap(center, radius, nullptr, nullptr);
    Triangle* const triangles = sHeadHitTriangles.room(count);
    u32 triangleCount = 0;
    Vec3f reaction;
    reaction.zero();

    for (u32 i = 0; i < count; i++) {
        const HitInfo* hit = Collision::getStrikeInfoMap(i);
        const Triangle* triangle = &hit->mParentTriangle;

        if (MR::isThroughPolygon(triangle) || isThroughWall(triangle)) {
            continue;
        }

        const Vec3f* normal = triangle->getNormal(0);

        if (!isSwimming() && !mMovementStates.launchKind) {
            if (damaging) {
                if (normal->dot(*getGravityVec()) < -0.707f) {
                    continue;
                }

            } else if (mMovementStates.jumping) {
                if (normal->dot(*getGravityVec()) < 0.0f || normal->dot(*getGravityVec()) < -0.707f) {
                    continue;
                }
            }
        }

        Vec3f horizontal;

        if (MR::vecKillElement(reaction, *normal, &horizontal) < hit->mDepth) {
            reaction = horizontal + *normal * hit->mDepth;

            if (isHeadPushEnableArea()) {
                addVelocity(*normal, hit->mDepth);
            }

            if (isSwimming()) {
                mSwim->hitHead(hit);
                addVelocity(*triangle->getNormal(0), radius - (hit->mHitPos - center).length());
            }

            if (MR::getWallCodeIndex(triangle) == CollisionWallCode_Fur) {
                mDrawStates.headHit = true;
            }

            if (damaging) {
                mDamage->stopHead(*triangle->getNormal(0));
            }

            triangles[triangleCount] = *triangle;
            triangleCount++;
        }
    }

    addVelocity(reaction);

    if (!MR::isNearZero(reaction) && mMovementStates.launchKind) {
        Vec3f normal(reaction);
        MR::normalizeOrZero(&normal);

        if (calcAngleD(normal) > 45.0f) {
            blown(reaction * 0.2f);
            mMovementStates.spinBlocked = true;
            mAirWalkTimer = 0;
            mAirControlInhibitTimer = 60;
            mMovementStates.launchKind = 0;
        } else if (mPostureUpVec.dot(reaction) < 0.0f) {
            mMovementStates.onGround = true;
            mJumpVec.zero();
            changeAnimation("空中一回転");
        }
    }

    bool swimSpin = false;

    if (isSwimming()) {
        if (mSwim->isSpinning()) {
            swimSpin = true;
        }
    }

    if ((mMovementStates.jumping && isRising()) || swimSpin) {
        for (u32 i = 0; i < triangleCount; i++) {
            if (triangles[i].getNormal(0)->dot(*getGravityVec()) > 0.707f) {
                mActor->sendMsgUpperPunch(triangles[i].mSensor);
            }
        }
    }
}

void Mario::calcShadowPos() {
    f32 offset = 100.0f;
    Vec3f start = mPosition - *getGravityVec() * offset;

    if ((getCurrentStatus() == MarioStatus_Hang && mHang->mClimbStep < 2) || mActor->mInDemo) {
        mActor->getRealPos("Spine1", &start);
    }

    f32 previousDistance = (mShadowPos - mPosition).length();
    Vec3f point(start);
    Vec3f direction(*getGravityVec());
    direction.scale(400.0f);
    u32 count = 6;

    if (isStatusActive(MarioStatus_Recovery)) {
        count = 30;
    }

    for (u32 i = 0; i < count; i++) {
        mMovementStates.shadowFound = MR::getFirstPolyOnLineToMap(&mShadowPos, mShadowPolygon, point, direction, nullptr, mShadowFilter);

        if (mMovementStates.shadowFound) {
            break;
        }

        point += direction;
    }

    f32 distance = 10.0f + (mShadowPos - mPosition).length();
    bool changed = false;

    if (mMovementStates.shadowFound && MR::abs(previousDistance - distance) > 100.0f) {
        changed = true;
    }

    if (!mMovementStates.shadowFound || changed) {
        direction.setLength(100.0f + (50.0f + previousDistance));
        point = start + (mSideVec + mFrontVec) * 0.1f;
        mMovementStates.shadowFound = MR::getFirstPolyOnLineToMap(&mShadowPos, mShadowPolygon, point, direction, nullptr, mShadowFilter);

        if (changed) {
            mMovementStates.shadowFound = true;
        }
    }

    if (!mMovementStates.shadowFound) {
        mShadowPos = mPosition + *getGravityVec() * 2500.0f;
    }
}

bool Mario::updateBinderInfo() {
    s32 count;
    bool firstCeiling = true;
    bool firstGround = true;
    const Vec3f* pLastMove;
    mCeilNorm.zero();
    mCeilPolygon->mIdx = -1;
    Binder* binder = mActor->mBinder;

    if (binder == nullptr) {
        return false;
    }

    count = binder->mPlaneNum;

    if (!count) {
        return false;
    }

    for (u32 i = 0; i < count; i++) {
        const HitInfo* hit = binder->getPlane(i);
        const Triangle* triangle = &hit->mParentTriangle;
        Vec3f normal(*triangle->getNormal(0));

        if (MR::isThroughPolygon(triangle) || isThroughWall(triangle)) {
            continue;
        }

        f32 dot = normal.dot(*getGravityVec());
        f32 ceilingThreshold = 0.1f;

        if (mJumpKind == 10) {
            ceilingThreshold = 0.707f;
        }

        if (dot < kFloorDotLimit) {
            mDrawStates.binderFloorHit = true;

            if (firstGround && mMovementStates.jumping && mVerticalSpeed > 30.0f) {
                bool oblique = false;
                Vec3f fromHit = mActor->mCenterPos - hit->mHitPos;

                if (MR::diffAngleAbs(fromHit, *triangle->getNormal(0)) > 0.10471976f) {
                    oblique = true;
                }

                if (hit->isCollisionAtCorner() || hit->isCollisionAtEdge() || oblique) {
                    Vec3f pushDirection;

                    if (hit->isCollisionAtEdge()) {
                        pushDirection = *triangle->getNormal(hit->mHitPart - 1);
                    } else if (!hit->isCollisionAtCorner()) {
                        pushDirection = mPosition - hit->mHitPos;
                    } else {
                        pushDirection = mPosition - hit->mHitPos;
                    }

                    Vec3f direction(fromHit);
                    MR::normalizeOrZero(&direction);
                    f32 facing = direction.dot(*triangle->getNormal(0));
                    f32 scale = facing;

                    if (facing >= 0.707f) {
                        if (hit->isCollisionAtEdge()) {
                            MR::vecKillElement(mVelocity, *triangle->getNormal(0), &mVelocity);
                            scale = 1.0f;
                        }

                    } else if (facing <= -0.707f) {
                        scale = 0.0f;
                    } else {
                        scale = (0.707f - MR::abs(facing)) / 0.707f;
                    }

                    MR::vecKillElement(pushDirection, getAirGravityVec(), &pushDirection);

                    if (MR::normalizeOrZero(&pushDirection)) {
                        MR::vecKillElement(fromHit, getAirGravityVec(), &pushDirection);
                        MR::normalizeOrZero(&pushDirection);
                    }

                    if (mMovementStates.groundMissing) {
                        pLastMove = &mActor->mLastPushMove;

                        if (MR::diffAngleAbsHorizontal(*pLastMove, pushDirection, getAirGravityVec()) >= 2.3561945f) {
                            if (mShadowPolygon->isValid()) {
                                if (triangle->getNormal(0)->dot(*mShadowPolygon->getNormal(0)) <= 0.17f) {
                                    f32 component = MR::vecKillElement(mJumpVec, *triangle->getNormal(0), &mJumpVec);
                                    mJumpVec += *triangle->getNormal(0) * component * 0.5f;
                                    mJumpVec += *mShadowPolygon->getNormal(0);
                                } else {
                                    if (mMovementStates.sideWall && (MR::diffAngleAbs(pushDirection, getAirGravityVec()) >= 1.4959966f ||
                                                                MR::diffAngleAbsHorizontal(mJumpVec, pushDirection, getAirGravityVec()) < 0.0f)) {
                                        pushDirection = -pushDirection;
                                    }

                                    mMovementStates.groundMissing = false;
                                }

                            } else {
                                if (mMovementStates.sideWall && (MR::diffAngleAbs(pushDirection, getAirGravityVec()) >= 1.4959966f ||
                                                            MR::diffAngleAbsHorizontal(mJumpVec, pushDirection, getAirGravityVec()) < 0.0f)) {
                                    pushDirection = -pushDirection;
                                }

                                mMovementStates.groundMissing = false;
                            }
                        }

                        if (mMovementStates.jumping && !isRising()) {
                            Vec3f horizontal;
                            pLastMove = &mActor->mMovementMove;

                            if (MR::isNearZero(MR::vecKillElement(*pLastMove, getAirGravityVec(), &horizontal)) &&
                                (mMovementStates.frontWall || mMovementStates.backWall || mMovementStates.sideWall)) {
                                cutGravityElementFromJumpVec(true);
                                mJumpVec += -getAirGravityVec() * 5.0f + getWallNorm() * 2.0f;

                                if (!isCeiling()) {
                                    addTrans(getAirGravityVec() * -30.0f);
                                }

                                pushDirection.zero();
                            }
                        }
                    }

                    push(pushDirection * hit->mDepth * scale);
                    firstGround = false;

                    if (getPlayerMode() == PlayerMode_Teresa) {
                        mBinderHitPos = hit->mHitPos;
                        mBinderHitNorm = *triangle->getNormal(0);
                        doTeresaReflection(fromHit, true);
                    }

                    mHitStates.binderPushed = true;
                }
            }

            if (mMovementStates.jumping && mMovementStates.hipDrop) {
                Vec3f lastMove;
                mActor->getLastMove(&lastMove);

                if (MR::isNearZero(lastMove.dot(*getGravityVec()))) {
                    mActor->sendMsgToSensor(triangle->mSensor, 0xB4);

                    if (!mActor->sendMsgToSensor(triangle->mSensor, 3)) {
                        Vec3f direction = mPosition - hit->mHitPos;
                        Vec3f originalDirection(direction);
                        Vec3f velocity(mPreSlopeVelocity);
                        MR::normalizeOrZero(&velocity);
                        f32 blend = 1.0f;

                        if (!MR::isNearZero(velocity)) {
                            blend = velocity.dot(*triangle->getNormal(0));
                        }

                        if (blend < 0.0f) {
                            blend = 0.0f;
                        } else if (blend > 1.0f) {
                            blend = 1.0f;
                        }

                        MR::vecBlendSphere(mPreSlopeVelocity, direction, &direction, blend);
                        MR::vecKillElement(direction, *getGravityVec(), &direction);
                        MR::normalizeOrZero(&direction);

                        if (MR::isNearZero(direction)) {
                            direction = mFrontVec;
                        }

                        f32 distance = hit->mDepth;

                        if (distance == 0.0f) {
                            distance = 1.0f;
                        }

                        push(direction * distance);
                        firstGround = false;
                    }
                }
            }

        } else if (!(dot < ceilingThreshold)) {
            Vec3f horizontal;

            if (firstCeiling && (MR::vecKillElement(mJumpVec, normal, &horizontal) < 0.0f || mPrevDrawStates.pressed) && calcAngleD(normal) > 100.0f) {
                if (!MR::isEqualString(triangle->mSensor->mHost->mName, "マンホールのふた(クッパ船)")) {
                    mJumpVec = horizontal;
                }

                startPadVib(0u);
                firstCeiling = false;
                *mCeilPolygon = *triangle;
            }

            mCeilNorm += normal;
        }
    }

    MR::normalizeOrZero(&mCeilNorm);
    return true;
}

bool Mario::isThroughWall(const Triangle* pTriangle) const {
    if (getPlayerMode() == PlayerMode_Teresa) {
        if (!mTeresaWallThroughTimer) {
            return false;
        }

        if (MR::getWallCodeIndex(pTriangle) == CollisionWallCode_GhostThroughCode) {
            return true;
        }
    }

    return false;
}

bool Mario::checkGround() {
    if (isStatusActive(MarioStatus_Recovery) || isStatusActive(MarioStatus_Warp)) {
        return false;
    }

    if (mMovementStates.groundMissing) {
        if (mMovementStates.simpleGroundCheck) {
            bool ground = checkGroundOnSlope();

            if (ground) {
                mHitStates.groundSameSensor = true;
                mHitStates.groundEvenSlope = true;
            }

            return ground;
        }

    } else {
        if (isUseSimpleGroundCheck()) {
            if (!mMovementStates.simpleGroundCheck) {
                if (MR::isNearZero(getGravityVec()->y - 1.0f)) {
                    mActor->setBlendMtxTimer(4);
                }

                mMovementStates.simpleGroundCheck = true;
            }

            bool ground = checkGroundOnSlope();

            if (ground) {
                mHitStates.groundSameSensor = true;
                mHitStates.groundEvenSlope = true;
            }

            return ground;
        }

        if (mMovementStates.simpleGroundCheck) {
            if (MR::isNearZero(getGravityVec()->y - 1.0f)) {
                mActor->setBlendMtxTimer(4);
            }

            mMovementStates.simpleGroundCheck = false;
        }
    }

    Vec3f normal;

    if (isAnimationRun("崖ふんばり")) {
        normal = -*getGravityVec();
    } else {
        normal = *mShadowPolygon->getNormal(0);
    }

    Vec3f offset;
    MR::vecKillElement(mFrontVec, normal, &offset);

    if (MR::isNearZero(offset)) {
        return false;
    }

    offset.setLength(50.0f);
    Vec3f axis;
    axis = normal;

    if (MR::isNearZero(axis)) {
        return false;
    }

    Mtx34f rotation;
    NebulaMtxRotAxisRad(rotation, &axis, 2.0943952f);
    u8 hit[4];
    u8 tooFar[4] = {0, 0, 0, 0};
    Triangle triangles[4];
    Vec3f positions[4];
    bool noGround = false;
    mMovementStates.groundMissing = false;
    f32 maxDistance;

    if (mMovementStates.jumping && isRising()) {
        maxDistance = 10.0f;
    } else {
        maxDistance = 30.0f;
    }

    if (getCurrentStatus() == MarioStatus_Slider) {
        maxDistance = 100.0f;
    }

    if (getCurrentStatus() == MarioStatus_Freeze) {
        maxDistance = 5.0f;
    }

    s32 groundCount = 0;
    Vec3f sum;
    sum.zero();
    Vec3f selectedPos;
    bool select = true;
    u32 count = 3;

    if (!mTargetWalkSpeedIndex && mMovementStates.onGround && !mMovementStates.jumping && !mMovementStates.slopeSlide) {
        count = 4;
    }

    if (isAnimationRun("壁押し", MarioIndexedAnime_WallPush)) {
        count = 4;
    }

    for (u32 i = 0; i < count; i++) {
        Vec3f start = mPosition + offset - *getGravityVec() * 30.0f;

        if (i == 3) {
            start = mPosition - *getGravityVec() * 30.0f;
        }

        hit[i] = MR::getFirstPolyOnLineBFast(start, *getGravityVec() * 100.0f, &positions[i], &triangles[i]);

        if (i == 3) {
            break;
        }

        if (hit[i]) {
            if (mLedgeFallTimer && MR::abs((mPosition - positions[i]).dot(*getGravityVec())) > 0.0f) {
                hit[i] = 0;
                tooFar[i] = 1;
            }

            if (MR::abs((mPosition - positions[i]).dot(*getGravityVec())) > maxDistance) {
                hit[i] = 0;
                tooFar[i] = 1;
            }

            if (calcAngleD(*triangles[i].getNormal(0)) >= 80.0f) {
                hit[i] = 0;
            }

            f32 dot = getGravityVec()->dot(*triangles[i].getNormal(0));

            if (getCurrentStatus() != MarioStatus_Slider && dot > kFloorDotLimit) {
                hit[i] = 0;
            }

            if (isStatusActive(MarioStatus_Recovery) && MR::isThroughPolygon(&triangles[i])) {
                hit[i] = 0;
            }

            if (hit[i]) {
                sum += positions[i];
                groundCount++;

                if (mMovementStates.slopeSlide && !select) {
                    Vec3f direction(mPrevVelocity);
                    MR::normalizeOrZero(&direction);

                    if ((positions[i] - mPosition).dot(direction) > (selectedPos - mPosition).dot(direction)) {
                        select = true;
                    }
                }

                if (select) {
                    setGroundNorm(*triangles[i].getNormal(0));
                    *mGroundPolygon = triangles[i];
                    select = false;
                    selectedPos = positions[i];
                    mGroundPos = selectedPos;
                    recordLastGround();
                }

                if (MR::getWallCodeIndex(&triangles[i]) == CollisionWallCode_Fur) {
                    mDrawStates.onFur = true;
                }
            }
        }

        NebulaMtxMultVec(rotation, &offset, &offset);
    }

    u32 i;

    for (i = 0; i < count; i++) {
        if (!hit[i]) {
            break;
        }
    }

    if (i == count) {
        mHitStates.groundAllProbes = true;
    }

    if (!isNoWalkFallOnDossun() && !isStatusActive(MarioStatus_Faint) && !isStatusActive(MarioStatus_Damage) && !isStatusActive(MarioStatus_Talk) &&
        !isStatusActive(MarioStatus_Hang) && (!mMovementStates.baseRotated || mActionStates.baseRotatedAboutUp)) {
        while ((count == 4 && !hit[3]) || mFloorCode == CollisionFloorCode_Slide) {
            if (!(hit[0] | hit[1] | hit[2])) {
                return false;
            }

            if (!hit[0]) {
                bool moved = false;

                if (!mMovementStates.frontWall) {
                    addTrans(mFrontVec * 6.0f);
                    moved = true;
                }

                if (hit[1] && hit[2]) {
                    if (!moved) {
                        break;
                    }

                } else if (!hit[1]) {
                    if ((mMovementStates.sideWall || mMovementStates.frontWall || mMovementStates.backWall) && mSideVec.dot(getWallNorm()) < 0.0f) {
                        if (moved) {
                            addTrans(-mSideVec * 3.0f - mFrontVec * 6.0f);
                        }

                        break;
                    } else {
                        addTrans(mSideVec * 3.0f);
                        moved = true;
                    }

                } else {
                    if ((mMovementStates.sideWall || mMovementStates.frontWall || mMovementStates.backWall) && -mSideVec.dot(getWallNorm()) < 0.0f) {
                        if (moved) {
                            addTrans(mSideVec * 3.0f - mFrontVec * 6.0f);
                        }

                        break;
                    } else {
                        addTrans(-mSideVec * 3.0f);
                        moved = true;
                    }
                }

                if (moved) {
                    mDrawStates.atEdge = true;
                }

                return true;
            } else if (!hit[1] && !hit[2]) {
                stopWalk();

                if (mLandTimer < 24 && mJumpVec.dot(mFrontVec) >= 0.0f && mFloorCode != CollisionFloorCode_Slide) {
                    if (!mMovementStates.frontWall && !mMovementStates.frontWallGlancing) {
                        addTrans(mFrontVec * 3.0f);
                        mDrawStates.atEdge = true;
                    }

                } else {
                    if (!mMovementStates.backWall) {
                        mDrawStates.edgePushBack = true;
                        addTrans(mFrontVec * -6.0f);
                        mDrawStates.atEdge = true;
                    }

                    if (mEdgePushTimer > 8) {
                        addTrans(mSideVec * 5.0f);
                    }

                    return true;
                }

            } else if (mFloorCode == CollisionFloorCode_Slide) {
                if (!hit[2]) {
                    addTrans(-mSideVec * 5.0f);
                } else if (!hit[1]) {
                    addTrans(mSideVec * 5.0f);
                } else {
                    addTrans(mFrontVec * 5.0f);
                }

                mDrawStates.atEdge = true;
            }

            break;
        }
    }

    if (!groundCount) {
        noGround = true;
    }

    s32 sameSensor = 0;
    s32 sameSlope = 0;
    Vec3f groundHorizontal;
    MR::vecKillElement(*mGroundPolygon->getNormal(0), getAirGravityVec(), &groundHorizontal);

    for (u32 i = 0; i < groundCount; i++) {
        if (hit[i]) {
            if (triangles[i].mSensor == mGroundPolygon->mSensor) {
                sameSensor++;
            } else {
                sameSensor--;
            }

            Vec3f horizontal;
            MR::vecKillElement(*triangles[i].getNormal(0), getAirGravityVec(), &horizontal);

            if (horizontal.dot(groundHorizontal) >= 0.0f) {
                sameSlope++;
            }
        }
    }

    if (sameSensor > 0) {
        mHitStates.groundSameSensor = true;
    }

    if (sameSlope >= 3) {
        mHitStates.groundEvenSlope = true;
    }


    if (mMovementStates.forceGroundOnce) {
        mMovementStates.forceGroundOnce = false;
        Vec3f direction = mGroundPos - mPosition;
        f32 distance = MR::vecKillElement(direction, *getGravityVec(), &direction);
        addTrans(*getGravityVec() * distance);
        return true;
    }

    if (groundCount) {
        f32 distance = 0.0f;
        Vec3f horizontal;

        if (calcAngleD(*mShadowPolygon->getNormal(0)) < 55.0f) {
            distance = MR::vecKillElement(mShadowPos - mPosition, *getGravityVec(), &horizontal);
        }

        if (MR::abs(distance) > 30.0f) {
            distance = MR::vecKillElement(mGroundPos - mPosition, *getGravityVec(), &horizontal);
        }

        if (MR::abs(distance) < 30.0f && MR::abs(distance) > 1.0f && mMovementStates.onGround) {
            f32 dot = getGravityVec()->dot(-mGroundNorm);

            if (dot > 0.99f) {
                if (!mDrawStates.baseTransPoint && !mStepPolygon->isValid()) {
                    if (!isStatusActive(MarioStatus_Talk)) {
                        addTrans(*getGravityVec() * distance);
                    }

                    return true;
                }

            } else if (dot > 0.0f && !mDrawStates.baseTransPoint && mMovementStates.slopeSlide) {
                addTrans(-mGroundNorm * distance * dot);
                return true;
            }
        }
    }

    if (mMovementStates.jumping && isRising()) {
        return false;
    }

    if (!isStatusActive(MarioStatus_Bury) && count == 4 && noGround) {
        if (hit[3] == 1 && MR::abs((mPosition - positions[3]).dot(*getGravityVec())) < maxDistance) {
            noGround = false;
        }

        bool moveFront = true;
        bool moveBack = true;

        if (mMovementStates.frontWall) {
            moveFront = false;
        }

        if (mMovementStates.backWall) {
            moveBack = false;
        }

        if (mActor->mLastPushMove.dot(mFrontVec) < 0.0f) {
            if (moveBack) {
                addTrans(-mFrontVec * 6.0f);
            }

        } else if (moveFront) {
            addTrans(mFrontVec * 6.0f);
        }
    }

    if (noGround) {
        mMovementStates.groundMissing = true;
        return false;
    }

    if (!mActor->mInDemo && getCurrentStatus() != MarioStatus_Hang) {
        if ((mMovementStates.jumping && groundCount) || (mMovementStates.onGround && mVerticalSpeed >= 5.0f)) {
            Vec3f direction;
            f32 distance = MR::vecKillElement(mGroundPos - mShadowPos, mGroundNorm, &direction);
            f32 maxMove = 5.0f;
            direction = mShadowPos - mPosition;

            if (mMovementStates.frontWall && direction.dot(getFrontWallNorm()) < -0.01f) {
                maxMove = 0.0f;
            }

            if (mMovementStates.sideWall && direction.dot(getSideWallNorm()) < -0.01f) {
                maxMove = 0.0f;
            }

            if (mMovementStates.frontWall && direction.dot(getBackWallNorm()) < -0.01f) {
                maxMove = 0.0f;
            }

            if (maxMove != 0.0f) {
                if (direction.length() > maxMove) {
                    mShadowPos = mShadowPos - mPosition;
                    mShadowPos.setLength(maxMove);
                    mShadowPos += mPosition;
                }

                if (distance < maxDistance) {
                    setTrans(mShadowPos);
                }
            }
        }

        if (!groundCount && MR::abs((mShadowPos - mPosition).dot(*getGravityVec())) < maxDistance) {
            Vec3f direction = mShadowPos - mPosition;
            f32 distance = MR::vecKillElement(direction, *getGravityVec(), &direction);

            if (!MR::isNearZero(distance, 1.0f) || !mMovementStates.onGround) {
                mVelocity += *getGravityVec() * distance;
                mDrawStates.snappedToGround = true;
            }

            return true;
        }
    }

    if (!groundCount && MR::abs((mGroundPos - mPosition).dot(*getGravityVec())) < maxDistance) {
        Vec3f direction = mGroundPos - mPosition;
        f32 distance = MR::vecKillElement(direction, *getGravityVec(), &direction);

        if (!MR::isNearZero(distance, 1.0f)) {
            mVelocity += *getGravityVec() * distance;
            mDrawStates.snappedToGround = true;
        }

        return true;
    }

    if (mVerticalSpeed < 5.0f) {
        return true;
    }

    if (mDrawStates.binderFloorHit) {
        return true;
    }

    if (groundCount) {
        return true;
    }

    return mDrawStates.snappedToGround;
}

CubeCameraArea* Mario::getCameraCubeCode() const {
    if ((isSwimming() && mSwim->isOnWaterSurface()) || (mMovementStates.jumping && isRising())) {
        return static_cast< CubeCameraArea* >(MR::getAreaObj("CubeCamera", mPosition - *getGravityVec() * 100.0f));
    }

    return static_cast< CubeCameraArea* >(MR::getAreaObj("CubeCamera", mPosition));
}

void Mario::updateCubeCode() {
    AreaObj* tower;
    AreaObj* raster;
    s32 first;
    CubeCameraArea* camera;

    if ((isSwimming() && mSwim->isOnWaterSurface()) || (mMovementStates.jumping && isRising())) {
        camera = static_cast< CubeCameraArea* >(MR::getAreaObj("CubeCamera", mPosition - *getGravityVec() * 100.0f));
    } else {
        camera = static_cast< CubeCameraArea* >(MR::getAreaObj("CubeCamera", mPosition));
    }


    if (camera != nullptr) {
        switch (MR::getAreaObjArg(camera, 1)) {
        case 1:
            mDrawStates.wallInhibited = true;
            break;
        case 2:
            mDrawStates.wallJumpForbidden = true;
            break;
        }

    } else {
    }

    MR::tryToUpdatePlayerRestartIdInfo(mPosition);
    AreaObj* pullBack = MR::getAreaObj("PullBackCube", mPosition);

    if (pullBack == nullptr) {
        pullBack = MR::getAreaObj("PullBackCylinder", mPosition);

        if (pullBack != nullptr && MR::getAreaObjArg(pullBack, 0) != 1) {
            Vec3f position;
            Vec3f up;
            MR::calcCylinderPos(&position, pullBack);
            MR::calcCylinderUpVec(&up, pullBack);
            MarioRecovery* recovery = mRecovery;
            recovery->mPullBackCenter = position;
            recovery->mPullBackUp = up;
            recovery->mPullBack = true;
        }
    }

    if (pullBack != nullptr) {
        doRecovery();
    }

    mActionStates.planeCollision = false;

    if (MR::getAreaObj("PlaneCollisionCube", mPosition) != nullptr) {
        mActionStates.planeCollision = true;
    }

    mActionStates.inTower = false;
    tower = MR::getAreaObj("TowerModeCylinder", mPosition);

    if (tower != nullptr) {
        mActionStates.inTower = true;
        MR::calcCylinderCenterPos(&mTowerCenter, tower);
        MR::calcCylinderUpVec(&mTowerAxis, tower);
        mTowerRadius = MR::getCylinderRadius(tower);
    }

    if (MR::getAreaObj("ForbidTriangleJumpCube", mPosition) != nullptr) {
        mDrawStates.wallJumpForbidden = true;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        if (MR::getAreaObj("GlaringLightArea", mPosition) != nullptr) {
            mActor->setPlayerMode(PlayerMode_Normal, true);
        }

        if (!MR::isInShadeFromTheSun(mPosition, 2000.0f)) {
            mActor->setPlayerMode(PlayerMode_Normal, true);
        }
    }

    if (MR::getAreaObj("FallsCube", mActor->mHeadPos) != nullptr) {
        touchWater();
        playEffectRTZ("水壁ヒット", mHeadVec, mActor->mHeadPos);
    }

    if (MR::getAreaObj("HeavySteeringCube", mPosition) != nullptr) {
        mActionStates.heavySteeringStick = true;
        mActionStates.heavySteeringTurn = true;
    } else {
        mActionStates.heavySteeringStick = false;
        mActionStates.heavySteeringTurn = false;
    }

    if (MR::getAreaObj("DashChargeCylinder", mPosition) != nullptr) {
        mItemDashTimer = mActor->getConst().getTable()->mItemDashTimer;
    }

    raster = MR::getAreaObj("RasterScrollCube", mPosition);

    if (raster != nullptr) {
        first = MR::getAreaObjArg(raster, 0);
        s32 second = MR::getAreaObjArg(raster, 1);
        mActor->setRasterScroll(first, second, MR::getAreaObjArg(raster, 2));
    }

    if (MR::getAreaObj("ForbidJumpCube", mPosition) != nullptr) {
        mHitStates.jumpForbidden = true;
    }

    if (!isStatusActive(MarioStatus_DarkDamage)) {
        if (MR::getAreaObj("DarkMatterCube", mPosition) != nullptr) {
            mActor->forceKill(4);
        }

        mVelocity.zero();
    }
}
