#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_isNotReflectGlassGround_f1, "isNotReflectGlassGround.f1", 1627.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isNotReflectGlassGround_f2, "isNotReflectGlassGround.f2", 783.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isNotReflectGlassGround_f3, "isNotReflectGlassGround.f3", 2152.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isNotReflectGlassGround_f4, "isNotReflectGlassGround.f4", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_isNotReflectGlassGround_f5, "isNotReflectGlassGround.f5", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isNotReflectGlassGround_f6, "isNotReflectGlassGround.f6", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isNotReflectGlassGround_f7, "isNotReflectGlassGround.f7", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isUseFooSpecialGravity_f1, "isUseFooSpecialGravity.f1", 14760.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isUseFooSpecialGravity_f2, "isUseFooSpecialGravity.f2", 10676.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_isUseFooSpecialGravity_f3, "isUseFooSpecialGravity.f3", 6770.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f1, "updateOnimasu.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f2, "updateOnimasu.f2", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f3, "updateOnimasu.f3", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f4, "updateOnimasu.f4", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f5, "updateOnimasu.f5", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f6, "updateOnimasu.f6", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f7, "updateOnimasu.f7", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f8, "updateOnimasu.f8", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f9, "updateOnimasu.f9", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f10, "updateOnimasu.f10", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f11, "updateOnimasu.f11", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f12, "updateOnimasu.f12", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f13, "updateOnimasu.f13", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f14, "updateOnimasu.f14", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f15, "updateOnimasu.f15", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f16, "updateOnimasu.f16", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f17, "updateOnimasu.f17", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f18, "updateOnimasu.f18", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f19, "updateOnimasu.f19", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f20, "updateOnimasu.f20", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f21, "updateOnimasu.f21", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f22, "updateOnimasu.f22", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f23, "updateOnimasu.f23", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f24, "updateOnimasu.f24", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f25, "updateOnimasu.f25", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f26, "updateOnimasu.f26", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f27, "updateOnimasu.f27", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f28, "updateOnimasu.f28", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f29, "updateOnimasu.f29", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f30, "updateOnimasu.f30", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f31, "updateOnimasu.f31", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f32, "updateOnimasu.f32", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f33, "updateOnimasu.f33", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f34, "updateOnimasu.f34", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f35, "updateOnimasu.f35", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f36, "updateOnimasu.f36", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f37, "updateOnimasu.f37", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f38, "updateOnimasu.f38", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f39, "updateOnimasu.f39", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f40, "updateOnimasu.f40", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f41, "updateOnimasu.f41", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f42, "updateOnimasu.f42", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f43, "updateOnimasu.f43", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f44, "updateOnimasu.f44", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f45, "updateOnimasu.f45", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f46, "updateOnimasu.f46", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f47, "updateOnimasu.f47", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f48, "updateOnimasu.f48", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f49, "updateOnimasu.f49", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f50, "updateOnimasu.f50", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f51, "updateOnimasu.f51", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f52, "updateOnimasu.f52", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f53, "updateOnimasu.f53", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f54, "updateOnimasu.f54", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f55, "updateOnimasu.f55", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f56, "updateOnimasu.f56", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f57, "updateOnimasu.f57", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f58, "updateOnimasu.f58", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f59, "updateOnimasu.f59", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f60, "updateOnimasu.f60", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f61, "updateOnimasu.f61", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f62, "updateOnimasu.f62", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f63, "updateOnimasu.f63", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f64, "updateOnimasu.f64", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f65, "updateOnimasu.f65", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f66, "updateOnimasu.f66", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f67, "updateOnimasu.f67", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f68, "updateOnimasu.f68", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f69, "updateOnimasu.f69", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f70, "updateOnimasu.f70", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f71, "updateOnimasu.f71", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f72, "updateOnimasu.f72", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f73, "updateOnimasu.f73", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f74, "updateOnimasu.f74", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f75, "updateOnimasu.f75", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f76, "updateOnimasu.f76", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnimasu_f77, "updateOnimasu.f77", 320.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isHeadPushEnableArea_f1, "isHeadPushEnableArea.f1", 7020.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isHeadPushEnableArea_f2, "isHeadPushEnableArea.f2", 6920.0f);

void Mario::checkOnimasu(const HitSensor* pSensor) {
    if (!MR::isEqualSubString(pSensor->mHost->mName, "オニマス")) {
        return;
    }

    if (mOnimasuSensor == nullptr) {
        mOnimasuSensor = pSensor;
        mOnimasuFound = 1;
    } else if (mOnimasuSensor != pSensor && (pSensor->mPosition - mPosition).length() < (mOnimasuSensor->mPosition - mPosition).length()) {
        mOnimasuSensor = pSensor;
        mOnimasuFound = 1;
    }
}

bool Mario::isDossun(const Triangle* pTriangle) const {
    if (!pTriangle->isValid()) {
        return false;
    }

    if (MR::isEqualSubString(pTriangle->mSensor->mHost->getName(), "ドッスン")) {
        return true;
    }

    return false;
}

bool Mario::isStageCameraRotate2D() const {
    return MR::isEqualStageName("HellProminenceGalaxy") && MR::getCurrentScenarioNo() == 3;
}

bool Mario::isNoWalkFallOnDossun() const {
    if (!MR::isEqualStageName("CannonFleetGalaxy")) {
        return false;
    }

    if (isDossun(mGroundPolygon) && isDossun(mShadowPolygon)) {
        return true;
    }

    return false;
}

bool Mario::isNotReflectGlassGround() const {
    if (!MR::isEqualStageName("AstroGalaxy")) {
        return false;
    }

    Vec3f vec(cNum_isNotReflectGlassGround_f1, cNum_isNotReflectGlassGround_f2, -cNum_isNotReflectGlassGround_f3);
    Vec3f v10 = mPosition - vec;

    f32 v4 = MR::diffAngleAbs(getCamDirZ(), v10);
    f32 v5 = v4;
    if (v4 >= cNum_isNotReflectGlassGround_f4) {
        v5 = (PI - v4);
    }

    f32 v7 = (cNum_isNotReflectGlassGround_f5 + (cNum_isNotReflectGlassGround_f6 * (MR::sin(v5) * MR::sin(v5))));
    MR::vecKillElement(v10, getAirGravityVec(), &v10);
    f32 length = v10.length();

    if (length >= v7) {
        if (length <= cNum_isNotReflectGlassGround_f7) {
            return true;
        }
    }

    return false;
}

bool Mario::isUseAnotherMovingPolygon() const {
    if (MR::isEqualStageName("BattleShipGalaxy")) {
        return true;
    }

    if (MR::isEqualStageName("SandClockGalaxy")) {
        return true;
    }

    if (MR::isEqualStageName("TriLegLv1Galaxy")) {
        return true;
    }

    return false;
}

bool Mario::isUseFooSpecialGravity(const Vec3f& rPos, Vec3f* pGravity) const {
    if (!isStatusActive(MarioStatus_Foo)) {
        return false;
    }

    if (MR::isEqualStageName("HeavensDoorGalaxy") && MR::getCurrentScenarioNo() == 2) {
        Vec3f center(cNum_isUseFooSpecialGravity_f1, -cNum_isUseFooSpecialGravity_f2, cNum_isUseFooSpecialGravity_f3);
        Vec3f toCenter = center - rPos;
        pGravity->set(toCenter);
        MR::normalizeOrZero(pGravity);
        return true;
    }

    return false;
}

void Mario::updateOnimasu() {
    if (!mOnimasuSensor) {
        return;
    }

    if ((mOnimasuSensor->mPosition - mPosition).length() >= cNum_updateOnimasu_f1) {
        mOnimasuSensor = nullptr;
        return;
    }

    const HitSensor* sensor = mOnimasuSensor;
    MtxPtr inverse = sensor->mHost->mCollisionParts->mInvBaseMatrix;
    Vec3f localPosition;
    Vec3f previousPosition;
    previousPosition = mOnimasuLocalPos;
    NebulaMtxMultVec(inverse, &mActor->mPosition, &localPosition);
    if (mOnimasuFound) {
        mOnimasuLocalPos = localPosition;
        mOnimasuFound = 0;
        return;
    }

    if (!mActionStates.onimasuWrapped) {
        if (previousPosition.y < -cNum_updateOnimasu_f2 && localPosition.y > -cNum_updateOnimasu_f3) {
            if (MR::isInRange(localPosition.x, -cNum_updateOnimasu_f4, cNum_updateOnimasu_f5) && MR::isInRange(localPosition.z, -cNum_updateOnimasu_f6, cNum_updateOnimasu_f7)) {
                localPosition = mOnimasuLocalPos;
                Vec3f position;
                NebulaMtxMultVec(sensor->mHost->mCollisionParts->mBaseMatrix, &localPosition, &position);
                setTrans(position);
                mVelocity.zero();
            }
        } else if (previousPosition.y > cNum_updateOnimasu_f8 && localPosition.y <= cNum_updateOnimasu_f9) {
            if (MR::isInRange(localPosition.x, -cNum_updateOnimasu_f10, cNum_updateOnimasu_f11) && MR::isInRange(localPosition.z, -cNum_updateOnimasu_f12, cNum_updateOnimasu_f13)) {
                mActionStates.onimasuWrapped = true;
                if (previousPosition.x < -cNum_updateOnimasu_f14) {
                    previousPosition.x = -cNum_updateOnimasu_f15;
                } else if (previousPosition.x > cNum_updateOnimasu_f16) {
                    previousPosition.x = cNum_updateOnimasu_f17;
                }

                if (previousPosition.z < -cNum_updateOnimasu_f18) {
                    previousPosition.z = -cNum_updateOnimasu_f19;
                } else if (previousPosition.z > cNum_updateOnimasu_f20) {
                    previousPosition.z = cNum_updateOnimasu_f21;
                }

                localPosition = previousPosition;
            }
        } else if (MR::isInRange(localPosition.x, -cNum_updateOnimasu_f22, cNum_updateOnimasu_f23) && MR::isInRange(localPosition.y, -cNum_updateOnimasu_f24, cNum_updateOnimasu_f25) &&
                   MR::isInRange(localPosition.z, -cNum_updateOnimasu_f26, cNum_updateOnimasu_f27)) {
            mVelocity.zero();
            localPosition = mOnimasuLocalPos;
            Vec3f position;
            NebulaMtxMultVec(sensor->mHost->mCollisionParts->mBaseMatrix, &localPosition, &position);
            Vec3f displacement(position - mPosition);
            f32 height = cNum_updateOnimasu_f28 + mVerticalSpeed;
            if (-displacement.dot(getShadowNorm()) > height) {
                MR::vecKillElement(displacement, getShadowNorm(), &displacement);
                displacement += -getShadowNorm() * mVerticalSpeed;
                position = mPosition + displacement;
            }

            setTrans(position);
        }

        bool correctX = false;
        bool correctY = false;
        bool correctZ = false;
        Vec3f axisX;
        Vec3f axisY;
        Vec3f axisZ;
        sensor->mHost->mCollisionParts->mBaseMatrix.getXDir(axisX);
        sensor->mHost->mCollisionParts->mBaseMatrix.getYDir(axisY);
        sensor->mHost->mCollisionParts->mBaseMatrix.getZDir(axisZ);
        f32 gravityX = axisX.dot(*getGravityVec());
        f32 absGravityX = MR::abs(gravityX);
        f32 gravityY = axisY.dot(*getGravityVec());
        f32 absGravityY = MR::abs(gravityY);
        f32 gravityZ = axisZ.dot(*getGravityVec());
        f32 absGravityZ = MR::abs(gravityZ);
        f32 lower = -cNum_updateOnimasu_f29;
        f32 upper = -cNum_updateOnimasu_f30;

        if (absGravityX > absGravityY && absGravityX > absGravityZ) {
            if (gravityX > 0.0f) {
                lower = cNum_updateOnimasu_f31;
                upper = cNum_updateOnimasu_f32;
            }

            if (MR::isInRange(localPosition.x, lower, upper)) {
                bool insideY = MR::isInRange(localPosition.y, -cNum_updateOnimasu_f33, cNum_updateOnimasu_f34);
                bool insideZ = MR::isInRange(localPosition.z, -cNum_updateOnimasu_f35, cNum_updateOnimasu_f36);
                if (insideY && insideZ) {
                    mActor->mPressAxis = axisX;
                    mActor->setPress(0, 0);
                } else {
                    if (insideZ) {
                        correctY = true;
                    }

                    if (insideY) {
                        correctZ = true;
                    }
                }
            }
        } else if (absGravityY > absGravityX && absGravityY > absGravityZ) {
            if (gravityY > 0.0f) {
                lower = cNum_updateOnimasu_f37;
                upper = cNum_updateOnimasu_f38;
            }

            if (MR::isInRange(localPosition.y, lower, upper)) {
                bool insideX = MR::isInRange(localPosition.x, -cNum_updateOnimasu_f39, cNum_updateOnimasu_f40);
                bool insideZ = MR::isInRange(localPosition.z, -cNum_updateOnimasu_f41, cNum_updateOnimasu_f42);
                if (insideX && insideZ) {
                    mActor->mPressAxis = axisY;
                    if (gravityY < 0.0f) {
                        mActor->setPress(0, 0);
                    }
                } else {
                    if (insideZ) {
                        correctX = true;
                    }

                    if (insideX) {
                        correctZ = true;
                    }
                }
            }
        } else {
            if (gravityZ > 0.0f) {
                lower = cNum_updateOnimasu_f43;
                upper = cNum_updateOnimasu_f44;
            }

            if (MR::isInRange(localPosition.z, lower, upper)) {
                bool insideX = MR::isInRange(localPosition.x, -cNum_updateOnimasu_f45, cNum_updateOnimasu_f46);
                bool insideY = MR::isInRange(localPosition.y, -cNum_updateOnimasu_f47, cNum_updateOnimasu_f48);
                if (insideX && insideY) {
                    mActor->mPressAxis = axisZ;
                    mActor->setPress(0, 0);
                } else {
                    if (insideY) {
                        correctX = true;
                    }

                    if (insideX) {
                        correctY = true;
                    }
                }
            }
        }

        if (correctX) {
            if (localPosition.x > -cNum_updateOnimasu_f49 && localPosition.x < -cNum_updateOnimasu_f50) {
                localPosition.x = -cNum_updateOnimasu_f51;
            } else if (localPosition.x < cNum_updateOnimasu_f52 && localPosition.x > cNum_updateOnimasu_f53) {
                localPosition.x = cNum_updateOnimasu_f54;
            }
        }

        if (correctY) {
            if (localPosition.y > -cNum_updateOnimasu_f55 && localPosition.y < -cNum_updateOnimasu_f56) {
                localPosition.y = -cNum_updateOnimasu_f57;
            } else if (localPosition.y < cNum_updateOnimasu_f58 && localPosition.y > cNum_updateOnimasu_f59) {
                localPosition.y = cNum_updateOnimasu_f60;
            }
        }

        if (correctZ) {
            if (localPosition.z > -cNum_updateOnimasu_f61 && localPosition.z < -cNum_updateOnimasu_f62) {
                localPosition.z = -cNum_updateOnimasu_f63;
            } else if (localPosition.z < cNum_updateOnimasu_f64 && localPosition.z > cNum_updateOnimasu_f65) {
                localPosition.z = cNum_updateOnimasu_f66;
            }
        }

        if (correctX | correctY | correctZ) {
            Vec3f position;
            NebulaMtxMultVec(sensor->mHost->mCollisionParts->mBaseMatrix, &localPosition, &position);
            if (mMovementStates.frontWall || mMovementStates.backWall || mMovementStates.sideWall) {
                Vec3f displacement(position - mPosition);
                if (getWallNorm().dot(displacement) < 0.0f) {
                    addVelocity(-displacement);
                } else {
                    setTrans(position);
                }
            }
        }
    } else if (localPosition.y > cNum_updateOnimasu_f67) {
        mActionStates.onimasuWrapped = false;
    } else {
        bool corrected = false;
        if (previousPosition.y >= -cNum_updateOnimasu_f68 && localPosition.y < -cNum_updateOnimasu_f69) {
            localPosition.y = previousPosition.y;
            corrected = true;
        }

        if (previousPosition.x >= -cNum_updateOnimasu_f70 && localPosition.x < -cNum_updateOnimasu_f71) {
            localPosition.x = previousPosition.x;
            corrected = true;
        }

        if (previousPosition.x <= cNum_updateOnimasu_f72 && localPosition.x > cNum_updateOnimasu_f73) {
            localPosition.x = previousPosition.x;
            corrected = true;
        }

        if (previousPosition.z >= -cNum_updateOnimasu_f74 && localPosition.z < -cNum_updateOnimasu_f75) {
            localPosition.z = previousPosition.z;
            corrected = true;
        }

        if (previousPosition.z <= cNum_updateOnimasu_f76 && localPosition.z > cNum_updateOnimasu_f77) {
            localPosition.z = previousPosition.z;
            corrected = true;
        }

        if (corrected) {
            Vec3f position;
            NebulaMtxMultVec(sensor->mHost->mCollisionParts->mBaseMatrix, &mOnimasuLocalPos, &position);
            setTrans(position);
            localPosition = mOnimasuLocalPos;
        }
    }

    mOnimasuLocalPos = localPosition;
}

bool Mario::isHeadPushEnableArea() const {
    if (!mMovementStates.mode2D) {
        return false;
    }

    if (MR::isEqualStageName("SandClockGalaxy")) {
        if (MR::isInRange(mPosition.x, -cNum_isHeadPushEnableArea_f1, -cNum_isHeadPushEnableArea_f2)) {
            return true;
        }
    }

    return false;
}

bool Mario::isOnimasuBinderPressSkip() const {
    if (isStatusActive(MarioStatus_SideStep)) {
        if (mFrontWallTriangle->mSensor != nullptr) {
            if (MR::isEqualSubString(mFrontWallTriangle->mSensor->mHost->mName, "オニマス")) {
                return true;
            }
        }
    }

    return false;
}
