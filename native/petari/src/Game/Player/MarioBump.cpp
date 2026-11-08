#include "Game/Player/MarioBump.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"

void Mario::checkBump() {
    if (!mMovementStates.jumping && !mMovementStates._A && mMovementStates._1) {
        if (!isStatusActive(MarioStatus_Hang) && !isStatusActive(MarioStatus_Wall) && !isStatusActive(MarioStatus_Swim) &&
            !isStatusActive(MarioStatus_Climb)) {
            if (!isDamaging() && getPlayerMode() != 5 && !isSwimming()) {
                if (isStickOn()) {
                    if (!mCorrectionEaseTimer && (mMovementStates._23 == 0)) {
                        if (mBumpPolygon->isValid()) {
                            const char* floor = MR::getFloorCodeString(mBumpPolygon);

                            if (!isSlipPolygon(mBumpPolygon) || !strcmp(floor, "NoSlip") || !strcmp(floor, "Sand")) {
                                startBump(mBumpHitPos);
                            }
                        }
                    }
                }
            }
        }
    }
}

void Mario::startBump(const Vec3f& rVec) {
    if ((getTrans() - rVec).length() >= 100.0f) {
        return;
    }

    Vec3f v21;
    const Vec3f* grav = &getAirGravityVec();
    const Vec3f* norm = mBumpPolygon->getNormal(0);
    MR::vecKillElement(*norm, *grav, &v21);

    if (!MR::normalizeOrZero(&v21) && MR::diffAngleAbsHorizontal(mFrontVec, -v21, getAirGravityVec()) > 0.52359879f) {
        Vec3f v20;
        if (MR::vecKillElement(getTrans() - rVec, getAirGravityVec(), &v20) > 0.0f) {
            return;
        }
    }

    if (getCurrentStatus() == 30) {
        closeStatus(mBump);
    }

    setTrans(rVec, "段差");
    changeStatus(mBump);
    mVelocity.zero();
    mTurnSlipTimer = 0;
    mMovementStates._10 = 0;
    mBumpGravityVec = -(*mBumpPolygon->getNormal(0));
    Vec3f v19;
    MR::vecKillElement(-(*mBumpPolygon->getNormal(0)), mSideVec, &v19);

    if (!MR::normalizeOrZero(&v19)) {
        mBumpGravityVec = v19;
    }
}

MarioBump::MarioBump(MarioActor* pActor) : MarioState(pActor, MarioStatus_Bump) {
    _12 = 0;
    _14 = 0.0f;
    _18.zero();
}

bool MarioBump::start() {
    _14 = getPlayer()->mWalkSpeed;

    MR::clamp(_14, 0.0f, 1.0f);

    s32 blendTimer = 0x14 - static_cast< s32 >(16.0f * _14);

    if (blendTimer < 4) {
        blendTimer = 4;
    }

    mActor->setBlendMtxTimer(static_cast< u16 >(blendTimer));
    _12 = static_cast< u16 >(blendTimer);

    if (isAnimationRun(nullptr)) {
        stopAnimation(nullptr);
    }

    _18 = getFrontVec();
    return true;
}

bool MarioBump::update() {
    if (mActor->isRequestJump()) {
        getPlayer()->tryJump();
        return false;
    } else {
        clearVelocity();
        f32 val = getPlayer()->mWalkSpeed;

        if (val < 0.1f) {
            val = 0.1f;
        }

        getPlayer()->mWalkSpeed = val;
        getPlayer()->updateWalkSpeed();

        if (_12) {
            --_12;
        }
    }

    if (_12 == 0) {
        return false;
    }

    return true;
}

bool MarioBump::close() {
    return true;
}
