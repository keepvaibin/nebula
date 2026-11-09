#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

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

    Vec3f vec(1627.0f, 783.0f, -2152.0f);
    Vec3f v10 = mPosition - vec;

    f32 v4 = MR::diffAngleAbs(getCamDirZ(), v10);
    f32 v5 = v4;
    if (v4 >= 1.5707964f) {
        v5 = (PI - v4);
    }

    f32 v7 = (400.0f + (300.0f * (MR::sin(v5) * MR::sin(v5))));
    MR::vecKillElement(v10, getAirGravityVec(), &v10);
    f32 length = v10.length();

    if (length >= v7) {
        if (length <= 700.0f) {
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
        Vec3f center(14760.0f, -10676.2f, 6770.0f);
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

    if ((mOnimasuSensor->mPosition - mPosition).length() >= 1000.0f) {
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
        if (previousPosition.y < -400.0f && localPosition.y > -400.0f) {
            if (MR::isInRange(localPosition.x, -400.0f, 400.0f) && MR::isInRange(localPosition.z, -400.0f, 400.0f)) {
                localPosition = mOnimasuLocalPos;
                Vec3f position;
                NebulaMtxMultVec(sensor->mHost->mCollisionParts->mBaseMatrix, &localPosition, &position);
                setTrans(position);
                mVelocity.zero();
            }
        } else if (previousPosition.y > 400.0f && localPosition.y <= 400.0f) {
            if (MR::isInRange(localPosition.x, -400.0f, 400.0f) && MR::isInRange(localPosition.z, -400.0f, 400.0f)) {
                mActionStates.onimasuWrapped = true;
                if (previousPosition.x < -320.0f) {
                    previousPosition.x = -320.0f;
                } else if (previousPosition.x > 320.0f) {
                    previousPosition.x = 320.0f;
                }

                if (previousPosition.z < -320.0f) {
                    previousPosition.z = -320.0f;
                } else if (previousPosition.z > 320.0f) {
                    previousPosition.z = 320.0f;
                }

                localPosition = previousPosition;
            }
        } else if (MR::isInRange(localPosition.x, -400.0f, 400.0f) && MR::isInRange(localPosition.y, -400.0f, 400.0f) &&
                   MR::isInRange(localPosition.z, -400.0f, 400.0f)) {
            mVelocity.zero();
            localPosition = mOnimasuLocalPos;
            Vec3f position;
            NebulaMtxMultVec(sensor->mHost->mCollisionParts->mBaseMatrix, &localPosition, &position);
            Vec3f displacement(position - mPosition);
            f32 height = 10.0f + mVerticalSpeed;
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
        f32 lower = -500.0f;
        f32 upper = -300.0f;

        if (absGravityX > absGravityY && absGravityX > absGravityZ) {
            if (gravityX > 0.0f) {
                lower = 300.0f;
                upper = 500.0f;
            }

            if (MR::isInRange(localPosition.x, lower, upper)) {
                bool insideY = MR::isInRange(localPosition.y, -320.0f, 320.0f);
                bool insideZ = MR::isInRange(localPosition.z, -320.0f, 320.0f);
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
                lower = 300.0f;
                upper = 500.0f;
            }

            if (MR::isInRange(localPosition.y, lower, upper)) {
                bool insideX = MR::isInRange(localPosition.x, -320.0f, 320.0f);
                bool insideZ = MR::isInRange(localPosition.z, -320.0f, 320.0f);
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
                lower = 300.0f;
                upper = 500.0f;
            }

            if (MR::isInRange(localPosition.z, lower, upper)) {
                bool insideX = MR::isInRange(localPosition.x, -320.0f, 320.0f);
                bool insideY = MR::isInRange(localPosition.y, -320.0f, 320.0f);
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
            if (localPosition.x > -400.0f && localPosition.x < -320.0f) {
                localPosition.x = -400.0f;
            } else if (localPosition.x < 400.0f && localPosition.x > 320.0f) {
                localPosition.x = 400.0f;
            }
        }

        if (correctY) {
            if (localPosition.y > -400.0f && localPosition.y < -320.0f) {
                localPosition.y = -400.0f;
            } else if (localPosition.y < 400.0f && localPosition.y > 320.0f) {
                localPosition.y = 400.0f;
            }
        }

        if (correctZ) {
            if (localPosition.z > -400.0f && localPosition.z < -320.0f) {
                localPosition.z = -400.0f;
            } else if (localPosition.z < 400.0f && localPosition.z > 320.0f) {
                localPosition.z = 400.0f;
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
    } else if (localPosition.y > 400.0f) {
        mActionStates.onimasuWrapped = false;
    } else {
        bool corrected = false;
        if (previousPosition.y >= -320.0f && localPosition.y < -320.0f) {
            localPosition.y = previousPosition.y;
            corrected = true;
        }

        if (previousPosition.x >= -320.0f && localPosition.x < -320.0f) {
            localPosition.x = previousPosition.x;
            corrected = true;
        }

        if (previousPosition.x <= 320.0f && localPosition.x > 320.0f) {
            localPosition.x = previousPosition.x;
            corrected = true;
        }

        if (previousPosition.z >= -320.0f && localPosition.z < -320.0f) {
            localPosition.z = previousPosition.z;
            corrected = true;
        }

        if (previousPosition.z <= 320.0f && localPosition.z > 320.0f) {
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
        if (MR::isInRange(mPosition.x, -7020.0f, -6920.0f)) {
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
