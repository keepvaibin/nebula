#include "Game/Player/MarioBump.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_startBump_f1, "startBump.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startBump_f2, "startBump.f2", 0.52359879f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 16.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 0.1f);

void Mario::checkBump() {
    if (!mMovementStates.jumping && !mMovementStates.squat && mMovementStates.onGround) {
        if (!isStatusActive(MarioStatus_Hang) && !isStatusActive(MarioStatus_Wall) && !isStatusActive(MarioStatus_Swim) &&
            !isStatusActive(MarioStatus_Climb)) {
            if (!isDamaging() && getPlayerMode() != 5 && !isSwimming()) {
                if (isStickOn()) {
                    if (!mCorrectionEaseTimer && (mMovementStates.slopeSlide == 0)) {
                        if (mBumpPolygon->isValid()) {
                            const s32 floor = MR::getFloorCodeIndex(mBumpPolygon);

                            if (!isSlipPolygon(mBumpPolygon) || floor == CollisionFloorCode_NoSlip || floor == CollisionFloorCode_Sand) {
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
    if ((getTrans() - rVec).length() >= cNum_startBump_f1) {
        return;
    }

    Vec3f v21;
    const Vec3f* grav = &getAirGravityVec();
    const Vec3f* norm = mBumpPolygon->getNormal(0);
    MR::vecKillElement(*norm, *grav, &v21);

    if (!MR::normalizeOrZero(&v21) && MR::diffAngleAbsHorizontal(mFrontVec, -v21, getAirGravityVec()) > cNum_startBump_f2) {
        Vec3f v20;
        if (MR::vecKillElement(getTrans() - rVec, getAirGravityVec(), &v20) > 0.0f) {
            return;
        }
    }

    if (getCurrentStatus() == 30) {
        closeStatus(mBump);
    }

    setTrans(rVec);
    changeStatus(mBump);
    mVelocity.zero();
    mTurnSlipTimer = 0;
    mMovementStates.turnStickNeutral = 0;
    mBumpGravityVec = -(*mBumpPolygon->getNormal(0));
    Vec3f v19;
    MR::vecKillElement(-(*mBumpPolygon->getNormal(0)), mSideVec, &v19);

    if (!MR::normalizeOrZero(&v19)) {
        mBumpGravityVec = v19;
    }
}

MarioBump::MarioBump(MarioActor* pActor) : MarioState(pActor, MarioStatus_Bump) {
    mBlendTimer = 0;
    mStartSpeed = 0.0f;
}

bool MarioBump::start() {
    mStartSpeed = getPlayer()->mWalkSpeed;

    MR::clamp(mStartSpeed, 0.0f, 1.0f);

    s32 blendTimer = 0x14 - static_cast< s32 >(cNum_start_f1 * mStartSpeed);

    if (blendTimer < 4) {
        blendTimer = 4;
    }

    mActor->setBlendMtxTimer(static_cast< u16 >(blendTimer));
    mBlendTimer = static_cast< u16 >(blendTimer);

    if (isAnimationRun(nullptr)) {
        stopAnimation(nullptr);
    }

    return true;
}

bool MarioBump::update() {
    if (mActor->isRequestJump()) {
        getPlayer()->tryJump();
        return false;
    } else {
        clearVelocity();
        f32 val = getPlayer()->mWalkSpeed;

        if (val < cNum_update_f1) {
            val = cNum_update_f2;
        }

        getPlayer()->mWalkSpeed = val;
        getPlayer()->updateWalkSpeed();

        if (mBlendTimer) {
            --mBlendTimer;
        }
    }

    if (mBlendTimer == 0) {
        return false;
    }

    return true;
}

bool MarioBump::close() {
    return true;
}
