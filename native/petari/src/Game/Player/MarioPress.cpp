#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioHang.hpp"
#include "Game/Player/MarioMapCode.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f1, "checkPressDamage.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f2, "checkPressDamage.f2", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f3, "checkPressDamage.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f4, "checkPressDamage.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f5, "checkPressDamage.f5", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f6, "checkPressDamage.f6", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f7, "checkPressDamage.f7", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f8, "checkPressDamage.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f9, "checkPressDamage.f9", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f10, "checkPressDamage.f10", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkPressDamage_f11, "checkPressDamage.f11", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVerticalPress_f1, "checkVerticalPress.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVerticalPress_f2, "checkVerticalPress.f2", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVerticalPress_f3, "checkVerticalPress.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVerticalPress_f4, "checkVerticalPress.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVerticalPress_f5, "checkVerticalPress.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVerticalPress_f6, "checkVerticalPress.f6", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkVerticalPress_f7, "checkVerticalPress.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePressPre_f1, "checkSidePressPre.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePressPre_f2, "checkSidePressPre.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePressPre_f3, "checkSidePressPre.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f1, "checkSidePress.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f2, "checkSidePress.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f3, "checkSidePress.f3", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f4, "checkSidePress.f4", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f5, "checkSidePress.f5", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f6, "checkSidePress.f6", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f7, "checkSidePress.f7", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f8, "checkSidePress.f8", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f9, "checkSidePress.f9", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f10, "checkSidePress.f10", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f11, "checkSidePress.f11", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f12, "checkSidePress.f12", 0.017453292f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f13, "checkSidePress.f13", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f14, "checkSidePress.f14", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f15, "checkSidePress.f15", 0.34906587f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f16, "checkSidePress.f16", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f17, "checkSidePress.f17", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f18, "checkSidePress.f18", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f19, "checkSidePress.f19", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f20, "checkSidePress.f20", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f21, "checkSidePress.f21", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f22, "checkSidePress.f22", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f23, "checkSidePress.f23", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f24, "checkSidePress.f24", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkSidePress_f25, "checkSidePress.f25", 80.0f);

bool Mario::checkPressDamage() {
    if (mOnimasuSensor) {
        return false;
    }
    if (isSwimming()) {
        return false;
    }
    if (checkVerticalPress(false)) {
        if (isStatusActive(MarioStatus_Hang)) {
            mHang->forceDrop();
        } else {
            mActor->setPress(0, 0);
            if (mActor->mPressTimer) {
                return true;
            }
            mPushVec.zero();
            mPushVelocity.zero();
            return true;
        }
    }
    if (checkSidePressPre()) {
        mActor->setPress(1, 0);
        return true;
    }
    if (checkSidePress()) {
        return true;
    }
    if (checkWallFloorCode(0x1D) && mMovementStates.onGround && getFloorCode() != 0x1D) {
        mActor->mPressAxis = mGroundNorm;
        mActor->setPress(4, 120);
        return true;
    }
    if (mCeilDist < cNum_checkPressDamage_f1) {
        if (mMovementStates.frontWall && (!mMovementStates.jumping || !(getFrontWallNorm().dot(getAirGravityVec()) > cNum_checkPressDamage_f2))) {
            if (mCeilDist < cNum_checkPressDamage_f3) {
                mCeilDist = cNum_checkPressDamage_f4;
            }
            addVelocity(getFrontWallNorm() * (mFrontWallDist * (cNum_checkPressDamage_f5 / mCeilDist) - mFrontWallDist));
        }
        if (mMovementStates.sideWall && (!mMovementStates.jumping || !(getSideWallNorm().dot(getAirGravityVec()) > cNum_checkPressDamage_f6))) {
            if (mCeilDist < cNum_checkPressDamage_f7) {
                mCeilDist = cNum_checkPressDamage_f8;
            }
            addVelocity(getSideWallNorm() * (cNum_checkPressDamage_f9 * (cNum_checkPressDamage_f10 / mCeilDist) - cNum_checkPressDamage_f11));
        }
    }
    return false;
}

bool Mario::checkVerticalPress(bool atCurrentPosition) {
    Vec3f position;
    if (isStatusActive(MarioStatus_Hang)) {
        position = mActor->mCenterPos;
    } else {
        position = mPosition;
    }
    if (!atCurrentPosition && !mActor->mPressTimer) {
        position += mVelocity;
    }
    f32 height;
    if (mMovementStates.squat) {
        height = cNum_checkVerticalPress_f1;
    } else {
        height = cNum_checkVerticalPress_f2;
    }
    bool nearCeiling = false;
    Vec3f up(-*getGravityVec());
    f32 distance = 0.0f;
    Triangle ceiling;
    Triangle floor;
    Vec3f hitPosition;
    bool hit = MR::getFirstPolyOnLineToMap(&hitPosition, &ceiling, position - up * cNum_checkVerticalPress_f3, up * height);
    if (hit) {
        distance = up.dot(hitPosition - position);
        if (distance < cNum_checkVerticalPress_f4) {
            nearCeiling = true;
        }
    }
    if (hit) {
        Vec3f origin;
        if (nearCeiling) {
            origin = hitPosition;
        } else {
            origin = position;
        }
        if (MR::getFirstPolyOnLineToMap(&hitPosition, &floor, origin - *ceiling.getNormal(0) * cNum_checkVerticalPress_f5, *ceiling.getNormal(0) * height)) {
            if (ceiling.mSensor != floor.mSensor && (MR::isSensorPressObj(ceiling.mSensor) || MR::isSensorPressObj(floor.mSensor))) {
                if (!atCurrentPosition && !mActor->mPressTimer) {
                    if (!checkVerticalPress(true)) {
                        mVelocity.zero();
                        return false;
                    }
                    return true;
                }
                mActor->mPressAxis = mGroundNorm;
                return true;
            }
        } else if (!atCurrentPosition && MR::isSensorPressObj(ceiling.mSensor)) {
            mDrawStates.pressed = true;
            Vec3f displacement;
            if (distance < 0.0f) {
                displacement = hitPosition - position;
                displacement.setLength(height - distance);
            } else {
                displacement = position - hitPosition;
                displacement.setLength(cNum_checkVerticalPress_f6 - distance);
            }
            Vec3f pushDirection;
            bool blended = MR::vecBlendSphere(*ceiling.getNormal(0), *floor.getNormal(0), &pushDirection, cNum_checkVerticalPress_f7);
            if (!mActor->mPressTimer) {
                if (mMovementStates.onGround) {
                    if (mGroundPolygon->mSensor == ceiling.mSensor) {
                        if (blended) {
                            push(pushDirection * displacement.length());
                        }
                    } else {
                        mActor->mPressCeilingSensor = ceiling.mSensor;
                        mActor->mPressAxis = mGroundNorm;
                        return true;
                    }
                } else if (blended) {
                    push(pushDirection * displacement.length());
                }
            }
        }
    }
    return false;
}

bool Mario::checkSidePressPre() {
    if (mOnimasuSensor) {
        return false;
    }
    if (mMovementStates.frontWall && mMovementStates.backWall) {
        if ((mBackWallPos - mFrontWallPos).length() < cNum_checkSidePressPre_f1 && mFrontWallTriangle->mSensor != mBackWallTriangle->mSensor &&
            (MR::isSensorPressObj(mFrontWallTriangle->mSensor) || MR::isSensorPressObj(mBackWallTriangle->mSensor))) {
            if (isDossun(mFrontWallTriangle) || isDossun(mBackWallTriangle)) {
                return false;
            }
            if (mFrontWallTriangle->getNormal(0)->dot(*mBackWallTriangle->getNormal(0)) < -cNum_checkSidePressPre_f2) {
                mActor->mPressAxis = getWallNorm();
                return true;
            }
        }
    } else if (isStatusActive(MarioStatus_Hang) && mMovementStates.backWall && mRelativePolygon->isValid() && mRelativePolygon->mSensor != mBackWallTriangle->mSensor &&
               (MR::isSensorPressObj(mRelativePolygon->mSensor) || MR::isSensorPressObj(mBackWallTriangle->mSensor))) {
        if (isDossun(mRelativePolygon) || isDossun(mBackWallTriangle)) {
            return false;
        }
        Vec3f horizontal;
        MR::vecKillElement(mPosition - mBackWallPos, getAirGravityVec(), &horizontal);
        if (horizontal.length() < cNum_checkSidePressPre_f3) {
            mActor->mPressAxis = getWallNorm();
            return true;
        }
    }
    return false;
}

bool Mario::checkSidePress() {
    Vec3f position;
    if (isStatusActive(MarioStatus_Hang)) {
        position = mActor->mCenterPos;
    } else {
        position = mPosition;
    }
    f32 radius;
    f32 height;
    if (mMovementStates.squat) {
        radius = cNum_checkSidePress_f1;
        height = cNum_checkSidePress_f2;
    } else {
        radius = cNum_checkSidePress_f3;
        height = cNum_checkSidePress_f4;
    }
    f32 ceilingDistance = calcDistToCeil(false);
    Vec3f center(position + mHeadVec * height);
    u32 hitCount = Collision::checkStrikeBallToMap(center, radius, nullptr, nullptr);
    if (hitCount < 2) {
        return false;
    }
    for (u32 i = 0; i < hitCount; i++) {
        const HitInfo* first = Collision::getStrikeInfoMap(i);
        Vec3f firstNormal(*first->mParentTriangle.getNormal(0));
        for (u32 j = i + 1; j < hitCount; j++) {
            const HitInfo* second = Collision::getStrikeInfoMap(j);
            Vec3f secondNormal(*second->mParentTriangle.getNormal(0));
            if (firstNormal.dot(secondNormal) >= -cNum_checkSidePress_f5) {
                continue;
            }
            if ((first->mHitPos - center).dot(second->mHitPos - center) > 0.0f) {
                continue;
            }
            f32 separation = (first->mHitPos - second->mHitPos).length();
            Vec3f firstVelocity;
            Vec3f secondVelocity;
            MR::calcVelocityMovingPoint(&first->mParentTriangle, first->mHitPos, &firstVelocity);
            MR::calcVelocityMovingPoint(&second->mParentTriangle, second->mHitPos, &secondVelocity);
            if (!MR::isNearZero(firstVelocity) || !MR::isNearZero(secondVelocity)) {
                if (!MR::isNearZero(firstVelocity)) {
                    MR::normalize(&firstVelocity);
                    if (firstVelocity.dot(firstNormal) < cNum_checkSidePress_f6 || firstVelocity.dot(secondNormal) > -cNum_checkSidePress_f7) {
                        continue;
                    }
                }
                if (!MR::isNearZero(secondVelocity)) {
                    MR::normalize(&secondVelocity);
                    if (secondVelocity.dot(secondNormal) < cNum_checkSidePress_f8 || secondVelocity.dot(firstNormal) > -cNum_checkSidePress_f9) {
                        continue;
                    }
                }
                if (first->mParentTriangle.getSensor() != second->mParentTriangle.getSensor() &&
                    (MR::isSensorPressObj(first->mParentTriangle.getSensor()) || MR::isSensorPressObj(second->mParentTriangle.getSensor()))) {
                    if (isStatusActive(MarioStatus_Hang)) {
                        mMovementStates.onGround = false;
                        closeStatus(mHang);
                    }
                    if (!mMovementStates.onGround && MR::abs(firstNormal.dot(*getGravityVec())) < cNum_checkSidePress_f10) {
                        if (!first->isCollisionAtFace()) {
                            Vec3f horizontal;
                            f32 vertical = MR::vecKillElement(first->mHitPos - center, *getGravityVec(), &horizontal);
                            if (vertical != 0.0f) {
                                addVelocity(*getGravityVec() * -vertical);
                                mDrawStates.pressed = true;
                                continue;
                            }
                        }
                        if (!second->isCollisionAtFace()) {
                            Vec3f horizontal;
                            f32 vertical = MR::vecKillElement(second->mHitPos - center, *getGravityVec(), &horizontal);
                            if (vertical != 0.0f) {
                                addVelocity(*getGravityVec() * -vertical);
                                mDrawStates.pressed = true;
                                continue;
                            }
                        }
                    }
                    if (!isStatusActive(MarioStatus_Bury) && !mSinkTimer && ceilingDistance >= cNum_checkSidePress_f11) {
                        Vec3f firstOffset(first->mHitPos - center);
                        Vec3f secondOffset(center - second->mHitPos);
                        if (!MR::isNearZero(MR::diffAngleAbs(firstOffset, secondOffset), cNum_checkSidePress_f12)) {
                            MR::normalizeOrZero(&firstOffset);
                            MR::normalizeOrZero(&secondOffset);
                            addVelocity(-firstOffset * cNum_checkSidePress_f13);
                            addVelocity(secondOffset * cNum_checkSidePress_f14);
                            continue;
                        }
                    }
                    f32 angle = Angle::acosRadian(-firstNormal.dot(secondNormal));
                    if (mActor->selectTiltPress(first->mParentTriangle.getSensor()) || mActor->selectTiltPress(second->mParentTriangle.getSensor())) {
                        angle = 0.0f;
                    }
                    if (!isStatusActive(MarioStatus_Bury) && !mSinkTimer && angle > cNum_checkSidePress_f15) {
                        f32 distance = (cNum_checkSidePress_f16 - ceilingDistance) / Angle::sinRadian(angle);
                        if (!(distance < 0.0f)) {
                            Vec3f firstHorizontal;
                            Vec3f secondHorizontal;
                            MR::vecKillElement(firstNormal, *getGravityVec(), &firstHorizontal);
                            MR::vecKillElement(secondNormal, *getGravityVec(), &secondHorizontal);
                            if (firstHorizontal.length() > secondHorizontal.length()) {
                                firstHorizontal.setLength(distance);
                                addVelocity(firstHorizontal);
                            } else {
                                secondHorizontal.setLength(distance);
                                addVelocity(secondHorizontal);
                            }
                        }
                        continue;
                    }
                    s32 previousFloorCode = mFloorCodeReader->getCode(mShadowPolygon);
                    if (previousFloorCode != mFloorCodeReader->getCode(mGroundPolygon) && MR::isSensorPressObj(mGroundPolygon->mSensor) &&
                        mVerticalSpeed >= cNum_checkSidePress_f17) {
                        mDrawStates.atEdge = true;
                        Vec3f horizontal;
                        MR::vecKillElement(mShadowPos - mGroundPos, getAirGravityVec(), &horizontal);
                        horizontal.setLength(cNum_checkSidePress_f18);
                        addVelocity(horizontal);
                        return false;
                    }
                    if (MR::abs(firstNormal.dot(mHeadVec)) > cNum_checkSidePress_f19) {
                        mActor->setPress(0, 0);
                    } else {
                        mActor->setPress(1, 0);
                    }
                    mActor->mPressAxis = firstNormal;
                    return true;
                }
            }
            if (getPlayer()->mMovementStates.jumping) {
                MR::diffAngleAbsHorizontal(firstNormal, secondNormal, getAirGravityVec());
                f32 normalDot = firstNormal.dot(secondNormal);
                Vec3f tangent;
                NebulaVecCrossProduct(&firstNormal, &getAirGravityVec(), &tangent);
                MR::normalizeOrZero(&tangent);
                Vec3f firstUp = tangent.cross(firstNormal);
                MR::normalizeOrZero(&firstUp);
                NebulaVecCrossProduct(&secondNormal, &getAirGravityVec(), &tangent);
                MR::normalizeOrZero(&tangent);
                Vec3f secondUp = tangent.cross(secondNormal);
                MR::normalizeOrZero(&secondUp);
                Vec3f firstPosition;
                Vec3f secondPosition;
                firstPosition = first->mHitPos + firstUp * height;
                secondPosition = second->mHitPos + secondUp * height;
                f32 distance = (firstPosition - secondPosition).length();
                if (isRising()) {
                    if (normalDot < -cNum_checkSidePress_f20 && distance < cNum_checkSidePress_f21 && separation < cNum_checkSidePress_f22) {
                        cutGravityElementFromJumpVec(true);
                    }
                } else if (normalDot < -cNum_checkSidePress_f23 && separation < cNum_checkSidePress_f24 && distance < cNum_checkSidePress_f25) {
                    cutGravityElementFromJumpVec(true);
                    mMovementStates.onGround = true;
                    stopJump();
                }
            }
        }
    }
    return false;
}
