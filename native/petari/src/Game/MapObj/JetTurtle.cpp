#include "Game/MapObj/JetTurtle.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init2_f1, "init2.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f2, "init2.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f3, "init2.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f4, "init2.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f5, "init2.f5", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f6, "init2.f6", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init2_f7, "init2.f7", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appear_f1, "appear.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appear_f2, "appear.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_boundDrop_f1, "boundDrop.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_boundDrop_f2, "boundDrop.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_boundDrop_f3, "boundDrop.f3", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_boundDrop_f4, "boundDrop.f4", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThrowing_f1, "exeThrowing.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThrowing_f2, "exeThrowing.f2", 57.295776f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTakenReserve_f1, "exeTakenReserve.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTakenReserve_f2, "exeTakenReserve.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTakenReserve_f3, "exeTakenReserve.f3", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrop_f1, "exeDrop.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrop_f2, "exeDrop.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 0.2f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrop_addVelocityToGravity_1, "exeDrop.addVelocityToGravity.1", 1.0f);

namespace {
    // By mSpeedType.
    NEBULA_TUNABLE_LIST(f32, sThrowSpdStraight, , {30.0f, 20.0f, 30.0f});
    NEBULA_TUNABLE_LIST(f32, sThrowSpdHoming, , {24.0f, 20.0f, 24.0f});
    NEBULA_TUNABLE_LIST(f32, sGravityLevel, , {0.017f, 0.01f, 0.0055f});
    NEBULA_TUNABLE_LIST(u16, sResetStep, , {300, 480, 300});

    // Share of the into-surface part of the flight direction a rebound keeps.
    NEBULA_TUNABLE(f32, sBounceRateFloor, 0.8f);  // ground and roof
    NEBULA_TUNABLE(f32, sBounceRateWall, 0.9f);

    NEBULA_TUNABLE(f32, sHomingAngle, 0.5235988f);  // 30 degrees
    NEBULA_TUNABLE(f32, sResetFarDistance, 5000.0f);  // past this from the throw, the reset shows a star
    NEBULA_TUNABLE(f32, sFlightMaxDistance, 10000.0f);
    NEBULA_TUNABLE(s32, sReviveStep, 45);
    NEBULA_TUNABLE(s32, sReviveEndStep, 105);
};  // namespace

namespace NrvJetTurtle {
    NEW_NERVE_ONEND(JetTurtleNrvWait, JetTurtle, Wait, Wait);
    NEW_NERVE_ONEND(JetTurtleNrvWait2, JetTurtle, Wait, Wait);
    NEW_NERVE(JetTurtleNrvThrowWait, JetTurtle, ThrowWait);
    NEW_NERVE(JetTurtleNrvThrowing, JetTurtle, Throwing);
    NEW_NERVE(JetTurtleNrvTakenReserve, JetTurtle, Wait);
    NEW_NERVE(JetTurtleNrvTakenReserveD, JetTurtle, TakenReserve);
    NEW_NERVE(JetTurtleNrvTakenStart, JetTurtle, TakenStart);
    NEW_NERVE(JetTurtleNrvRestart, JetTurtle, Restart);
    NEW_NERVE(JetTurtleNrvDrop, JetTurtle, Drop);
};  // namespace NrvJetTurtle

JetTurtle::JetTurtle(const char* pName) : LiveActor(pName), mIsResetRequested(false) {
    lumacore::setClass(this, lumacore::kClassId< JetTurtle >);
}

void JetTurtle::init(const TableIter& rIter) {
    init2(rIter, false);
}

// obj_arg0 shell type, arg1 speed type - 1, arg2 1 = no revive, arg3 1 = revive light, arg7 shadow drop length.
void JetTurtle::init2(const TableIter& rIter, bool isGolden) {
    MR::initDefaultPos(this, rIter);

    s32 shellType = SHELL_GREEN;
    s32 speedType = -1;

    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg0NoInit(rIter, &shellType);
        MR::getPlacementArg1NoInit(rIter, &speedType);

        speedType++;
        if (speedType < 0 || speedType >= ARRAY_SIZE(::sResetStep)) {
            speedType = 0;
        }
    } else {
        speedType = 0;
    }

    mSpeedType = speedType;
    mHasReviveLight = false;
    mIsRevive = true;

    f32 shadowDropLength = cNum_init2_f1;

    if (MR::isValidInfo(rIter)) {
        s32 noRevive = -1;
        MR::getPlacementArg2NoInit(rIter, &noRevive);

        if (noRevive == 1) {
            mIsRevive = false;
        }

        s32 reviveLight = -1;
        MR::getPlacementArg3NoInit(rIter, &reviveLight);

        if (reviveLight == 1) {
            mHasReviveLight = true;
        }

        s32 shadowLength = -1;
        MR::getPlacementArg7NoInit(rIter, &shadowLength);

        if (shadowLength != -1) {
            shadowDropLength = shadowLength;
        }
    }

    if (shellType < SHELL_GREEN || shellType > SHELL_GOLD) {
        shellType = SHELL_GREEN;
    }

    if (isGolden) {
        shellType = SHELL_GOLD;
    }

    switch (shellType) {
    case SHELL_GREEN:
    case SHELL_RED:
        initModelManagerWithAnm("Koura", nullptr, false);
        break;
    case SHELL_GOLD:
        initModelManagerWithAnm("KouraShine", nullptr, false);
        break;
    }

    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    initHitSensor(2);
    MR::addHitSensor(this, "body", ATYPE_JET_TURTLE, 8, cNum_init2_f2, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEye(this, "eye", 8, cNum_init2_f3, Vec3f(0.0f, 0.0f, 0.0f));

    if (shellType < SHELL_GOLD) {
        MR::startBrk(this, "Koura");
        MR::setBrkFrameAndStop(this, 1.0f);
    }

    if (shellType == SHELL_GREEN) {
        becomeSlowType();
    } else if (shellType != SHELL_RED) {
        getSensor("body")->setType(ATYPE_JET_TURTLE_SLOW);
    }

    mShellType = shellType;
    mThrowSpeedBonus = 0.0f;

    initNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait));

    MR::initShadowVolumeCylinder(this, cNum_init2_f4);
    MR::setShadowDropLength(this, nullptr, shadowDropLength);
    MR::setShadowVolumeEndDropOffset(this, nullptr, cNum_init2_f5);
    MR::onShadowVolumeCutDropLength(this, nullptr);

    initSound(6, false);
    initBinder(cNum_init2_f6, cNum_init2_f7, 8);
    initEffectKeeper(8, "Koura");
    MR::addEffectHitNormal(this, nullptr);
    MR::validateShadow(this, nullptr);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();

        mIsAppeared = false;
    } else {
        mIsAppeared = true;

        makeActorAppeared();
    }

    MR::offBind(this);
}

void JetTurtle::initAfterPlacement() {
    mInitPos = mPosition;
    mInitRotation = mRotation;
}

void JetTurtle::becomeSlowType() {
    getSensor("body")->setType(ATYPE_JET_TURTLE_SLOW);
    MR::startBrk(this, "Koura");
    MR::setBrkFrameAndStop(this, 0.0f);
    mShellType = SHELL_GREEN;
}

void JetTurtle::becomeFastType() {
    getSensor("body")->setType(ATYPE_JET_TURTLE);
    MR::startBrk(this, "Koura");
    MR::setBrkFrameAndStop(this, 1.0f);
    mShellType = SHELL_RED;
}

void JetTurtle::appearAndTryTaken() {
    appear();
    MR::validateHitSensors(this);
    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve));
    MR::tryPlayerPullActor(getSensor("body"));
}

void JetTurtle::control() {
    if (mIsResetRequested) {
        reset(0);
    }
}

void JetTurtle::resetPosition() {
    MR::invalidateClipping(this);
    MR::invalidateHitSensors(this);
    MR::forceDeleteEffectAll(this);
    MR::stopSound(this, "SE_OJ_TURTLE_JET_LAUNCH_W");
    MR::stopSound(this, "SE_OJ_TURTLE_JET_LAUNCH_L");
    mVelocity.zero();
    mRotation.zero();
    mTakenSensor = nullptr;
    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvRestart));
    MR::hideModel(this);
    mHomingTarget = nullptr;
}

void JetTurtle::resetPositionAndVanish() {
    resetPosition();
    MR::deleteEffect(this, "BrakeLamp");

    switch (mShellType) {
    default:
        MR::emitEffect(this, "Vanish");
        break;
    case SHELL_RED:
        MR::emitEffect(this, "VanishRed");
        break;
    case SHELL_GOLD:
        MR::emitEffect(this, "VanishGold");
        break;
    }

    if (!mIsRevive) {
        kill();
    }
}

// Also TakenReserve: there the shell spins and flies to the player, the player's way blended in after 120 steps.
void JetTurtle::exeWait() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve))) {
            switch (mShellType) {
            default:
                MR::emitEffect(this, "SpinBlur");
                break;
            case SHELL_RED:
                MR::emitEffect(this, "SpinBlurRed");
                break;
            case SHELL_GOLD:
                MR::emitEffect(this, "SpinBlurGold");
                break;
            }

            MR::invalidateClipping(this);
            return;
        }

        if (mShellType == SHELL_GOLD) {
            MR::emitEffect(this, "Glow");
        }

        MR::calcGravity(this);
        MR::onCalcShadowOneTime(this, nullptr);
        MR::validateShadow(this, nullptr);
        MR::validateHitSensors(this);
        getSensor("eye")->invalidate();
        MR::startBck(this, "Wait");
    }

    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve))) {
        if (getNerveStep() >= NebulaFrameClock::stepsOfSeconds(2)) {
            Vec3f direction = *MR::getPlayerCenterPos() - mPosition;
            f32 rate = MR::clamp((getNerveStep() - 120) / cNum_exeWait_f1, 0.0f, 1.0f);
            direction.setLength(mVelocity.length());
            MR::vecBlendSphere(mVelocity, direction, &mVelocity, rate);
        }

        if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(5))) {
            reset(0);
        }
    }
}

void JetTurtle::exeThrowWait() {
    if (MR::isFirstStep(this)) {
        MR::onCalcGravity(this);
    }

    MR::invalidateHitSensors(this);

    if (mShellType == SHELL_GOLD) {
        if (MR::isHiddenModel(this)) {
            MR::deleteEffect(this, "HandyGlow");
        } else {
            MR::emitEffect(this, "HandyGlow");
        }
    }

    if (MR::isPlayerInWaterMode()) {
        MR::tryStartBck(this, "SwimFlutterboard");

        if (mShellType != SHELL_RED) {
            MR::emitEffect(this, "SwimBubble");
        } else {
            MR::emitEffect(this, "SwimBubbleRed");
        }

        if (mShellType == SHELL_GOLD) {
            MR::startLevelSound(this, "SE_OJ_LV_JET_TURTLE_G_LIGHT");
        }
    } else {
        MR::tryStartBck(this, "CarryWait");
        MR::deleteEffect(this, "SwimBubble");
        MR::deleteEffect(this, "SwimBubbleRed");
    }
}

// A shell placed visible (not by a switch) waits as Wait2 and starts where it appears; speed type 0 becomes 1 where the
// gravity 100 ahead differs from here.
void JetTurtle::appear() {
    LiveActor::appear();
    mHomingTarget = nullptr;
    mTakenSensor = nullptr;
    MR::showModel(this);

    if (!mIsAppeared) {
        setNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait));
        return;
    }

    if (mSpeedType == 0) {
        Vec3f gravityHere;
        Vec3f gravityAhead;
        Vec3f front;
        MR::calcGravityVectorOrZero(this, mPosition, &gravityHere, nullptr, 0);
        MR::calcFrontVec(&front, this);
        MR::calcGravityVectorOrZero(this, mPosition + front * cNum_appear_f1, &gravityAhead, nullptr, 0);

        if (!MR::isNearZero(gravityHere - gravityAhead)) {
            mSpeedType = 1;
        }
    }

    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2));
    getSensor("body")->mRadius = cNum_appear_f2;
    initAfterPlacement();
}

// The flight direction leaves each touched rebound surface, keeping part of its into-surface share.
void JetTurtle::bound() {
    if (MR::isBindedGround(this)) {
        Vec3f along;
        f32 into = MR::vecKillElement(mThrowDir, *MR::getGroundNormal(this), &along);

        if (into < 0.0f) {
            mThrowDir = along - *MR::getGroundNormal(this) * into * sBounceRateFloor;
        }
    }

    if (MR::isBindedWall(this)) {
        Vec3f along;
        f32 into = MR::vecKillElement(mThrowDir, *MR::getWallNormal(this), &along);

        if (into < 0.0f) {
            mThrowDir = along - *MR::getWallNormal(this) * into * sBounceRateWall;
        }
    }

    if (MR::isBindedRoof(this)) {
        Vec3f along;
        f32 into = MR::vecKillElement(mThrowDir, *MR::getRoofNormal(this), &along);

        if (into < 0.0f) {
            mThrowDir = along - *MR::getRoofNormal(this) * into * sBounceRateFloor;
        }
    }
}

inline void JetTurtle::endWait() {
    if (mShellType == SHELL_GOLD) {
        MR::deleteEffect(this, "Glow");
    }
}

// reason != 0: the shell went far, so a star shows instead of the explosion.
void JetTurtle::reset(u32 reason) {
    mIsResetRequested = false;
    MR::invalidateHitSensors(this);
    MR::forceDeleteEffectAll(this);
    MR::stopSound(this, "SE_OJ_TURTLE_JET_LAUNCH_W");
    MR::stopSound(this, "SE_OJ_TURTLE_JET_LAUNCH_L");

    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        MR::emitEffect(this, "WaterExplosion");
    } else if (reason != 0) {
        MR::emitEffect(this, "Star");
    } else {
        switch (mShellType) {
        default:
            MR::emitEffect(this, "Explosion");
            break;
        case SHELL_RED:
            MR::emitEffect(this, "ExplosionRed");
            break;
        case SHELL_GOLD:
            MR::emitEffect(this, "ExplosionGold");
            break;
        }
    }

    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        MR::startSound(this, "SE_OJ_TURTLE_JET_BREAK_W");
        MR::releaseSoundHandle(this, "SE_OJ_TURTLE_JET_BREAK_W");
    } else {
        MR::startSound(this, "SE_OJ_TURTLE_JET_BREAK_L");
        MR::releaseSoundHandle(this, "SE_OJ_TURTLE_JET_BREAK_L");
    }

    mVelocity.zero();
    mRotation.zero();
    mTakenSensor = nullptr;
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvRestart));
    MR::hideModel(this);
    mHomingTarget = nullptr;

    if (!mIsRevive) {
        kill();
    }

    MR::offBind(this);
}

void JetTurtle::boundDrop() {
    Vec3f gravity;
    MR::calcGravityVector(this, &gravity, nullptr, 0);
    Vec3f horizontal;
    f32 vertical = MR::vecKillElement(mVelocity, gravity, &horizontal);

    if (MR::isBindedGround(this) && vertical > 0.0f) {
        if (vertical < cNum_boundDrop_f1) {
            mVelocity.zero();
        } else {
            horizontal.scale(cNum_boundDrop_f2);
            mVelocity = horizontal - gravity * vertical * cNum_boundDrop_f3;
        }
    }

    if (MR::isBindedWall(this)) {
        horizontal.scale(-cNum_boundDrop_f4);
        mVelocity = horizontal + gravity * vertical;
    }

    if (MR::isBindedRoof(this)) {
        mVelocity.zero();
    }
}

// Flies along mThrowDir (toward mHomingTarget when one is set), bending toward gravity; a hit, a non-rebound surface, no
// gravity, the speed type's step count or sFlightMaxDistance resets it.
void JetTurtle::exeThrowing() {
    if (mIsResetRequested) {
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::onBind(this);
        MR::onCalcShadow(this, nullptr);
        MR::validateHitSensors(this);
        MR::emitEffect(this, "Ignition");

        if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::startSound(this, "SE_OJ_TURTLE_JET_LAUNCH_W");
            MR::startBck(this, "BulletWater");
            MR::emitEffect(this, "WaterBlur");
        } else {
            MR::startSound(this, "SE_OJ_TURTLE_JET_LAUNCH_L");
            MR::startSoundPlayer("SE_PV_THROW");
            MR::startBck(this, "Bullet");

            switch (mShellType) {
            default:
                MR::emitEffect(this, "Blur");
                break;
            case SHELL_RED:
                MR::emitEffect(this, "BlurRed");
                break;
            case SHELL_GOLD:
                MR::emitEffect(this, "BlurGold");
                break;
            }
        }
    }

    if (MR::isStep(this, sResetStep[mSpeedType])) {
        reset((mThrowStartPos - mPosition).length() > sResetFarDistance ? 1 : 0);
        return;
    }

    Vec3f velocity;

    if (mHomingTarget != nullptr) {
        if (!mHomingTarget->mValidByHost) {
            mHomingTarget = nullptr;
        } else {
            Vec3f toTarget = mHomingTarget->mPosition - mPosition;
            MR::normalizeOrZero(&toTarget);
            MR::vecBlendSphere(mThrowDir, toTarget, &mThrowDir, cNum_exeThrowing_f1);

            if (MR::isBckOneTimeAndStopped(this)) {
                if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
                    MR::startBck(this, "BulletWater");
                } else {
                    MR::startBck(this, "Bullet");
                }
            }

            velocity = mThrowDir;
            velocity.setLength(mThrowSpeedBonus + sThrowSpdHoming[mSpeedType]);
            mVelocity = velocity;
        }
    }

    if (mHomingTarget == nullptr) {
        velocity = mThrowDir;
        velocity.setLength(mThrowSpeedBonus + sThrowSpdStraight[mSpeedType]);
        mVelocity = velocity;
    }

    if ((mThrowStartPos - mPosition).length() > sFlightMaxDistance) {
        reset(1);
    } else {
        if (MR::isBindedWall(this) || MR::isBindedRoof(this)) {
            for (u32 i = 0; i < MR::getBindedPlaneNum(this); i++) {
                MR::sendArbitraryMsg(ACTMES_JET_TURTLE_ATTACK, MR::getBindedPlaneSensor(this, i), getSensor("body"));
            }

            if (MR::isBindedWall(this) && MR::isWallCodeRebound(&mBinder->mWallInfo.mParentTriangle)) {
                bound();
            } else if (MR::isBindedRoof(this) && MR::isWallCodeRebound(&mBinder->mRoofInfo.mParentTriangle)) {
                bound();
            } else {
                MR::shakeCameraWeak();
                reset(0);
                return;
            }
        }

        if (MR::isBindedGround(this)) {
            if (MR::sendMsgToGroundSensor(ACTMES_JET_TURTLE_ATTACK, getSensor("body"))) {
                MR::shakeCameraWeak();
                reset(0);
                return;
            }

            if (MR::isWallCodeRebound(&mBinder->mGroundInfo.mParentTriangle)) {
                bound();
            }
        }
    }

    MtxPtr jointMtx = MR::getJointMtx(this, 0);
    Vec3f up(jointMtx[0][1], jointMtx[1][1], jointMtx[2][1]);
    Vec3f gravity;
    MR::calcGravityVector(this, &gravity, nullptr, 0);

    if (MR::isNearZero(gravity)) {
        MR::shakeCameraWeak();
        reset(0);
        return;
    }

    if (!MR::isSameDirection(gravity, mVelocity)) {
        up = -gravity;
    }

    if (mHomingTarget == nullptr) {
        if (!(gravity == mLastGravity)) {
            MR::vecBlendSphere(mThrowDir, gravity, &mThrowDir, sGravityLevel[mSpeedType]);
        }

        mLastGravity = gravity;
    }

    Vec3f side = mVelocity.cross(up);
    up.cross(side, mVelocity);

    if (!MR::isNearZero(mVelocity)) {
        Pos3f frontUp;
        MR::makeMtxFrontUp(&frontUp, mVelocity, up);
        frontUp.getEulerXYZ(mRotation);
        mRotation.mult(cNum_exeThrowing_f2);
    }
}

// Moves toward the player's hand: 8 steps at 1/4 a step after a pull, 2 at 1/2 after a grab (TakenReserveD).
void JetTurtle::exeTakenReserve() {
    if (MR::isFirstStep(this)) {
        getSensor("body")->mRadius = cNum_exeTakenReserve_f1;
        mVelocity.zero();

        if (MR::isPlayerInWaterMode()) {
            if (mShellType != SHELL_RED) {
                MR::emitEffect(this, "SwimBubble");
            } else {
                MR::emitEffect(this, "SwimBubbleRed");
            }
        }
    }

    const bool isGrab = isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserveD));
    f32 followRate = isGrab ? cNum_exeTakenReserve_f2 : cNum_exeTakenReserve_f3;

    Vec3f takePos;
    MR::getPlayerTakePos(&takePos);
    mPosition = mPosition * (1.0f - followRate) + takePos * followRate;

    if (MR::isStep(this, isGrab ? 2 : 8)) {
        setNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenStart));
    }
}

void JetTurtle::exeTakenStart() {
    if (MR::sendMsgTaken(mTakenSensor, getSensor("body"))) {
        MR::startSystemSE("SE_SY_GET_TURTLE_JET");

        if (!MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::startSoundPlayer("SE_PV_CATCH");
        }

        MR::deleteEffect(this, "SpinBlur");
        MR::deleteEffect(this, "SpinBlurRed");
        MR::deleteEffect(this, "SpinBlurGold");

        if (mShellType == SHELL_GOLD) {
            MR::deleteEffect(this, "Glow");
            MR::emitEffect(this, "HandyGlow");
        }

        setNerve(GET_NERVE(JetTurtle, JetTurtleNrvThrowWait));
        MR::invalidateShadow(this, nullptr);
    } else {
        reset(0);
    }
}

// Hidden for sReviveStep steps, then back at its start: blinking (or the revive light) until sReviveEndStep.
void JetTurtle::exeRestart() {
    if (MR::isStep(this, sReviveStep)) {
        mPosition = mInitPos;
        mRotation = mInitRotation;
        MR::calcGravity(this);
        MR::onCalcShadowOneTime(this, nullptr);
        MR::validateShadow(this, nullptr);
        MR::calcAnimDirect(this);
        MR::validateClipping(this);

        if (mHasReviveLight) {
            MR::emitEffect(this, "AppearLight");
        }
    }

    if (!MR::isGreaterStep(this, sReviveStep)) {
        return;
    }

    if (mHasReviveLight) {
        MR::startLevelSound(this, "SE_EM_LV_NOKOLAND_REVIVE_EFFECT");
    } else {
        switch (getNerveStep() & 0x3) {
        case 0:
            MR::showModel(this);
            break;
        case 2:
            MR::hideModel(this);
            break;
        }
    }

    if (MR::isStep(this, sReviveEndStep)) {
        MR::showModel(this);
        setNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait));
        MR::emitEffect(this, "KouraRevival");
        MR::startSound(this, "SE_EM_NOKOLAND_REVIVE_APPEAR");
    }
}

void JetTurtle::exeDrop() {
    MR::calcGravity(this);
    MR::deleteEffect(this, "BrakeLamp");

    if (MR::isFirstStep(this)) {
        Vec3f front;
        MR::calcFrontVec(&front, this);
        mVelocity = -mGravity * cNum_exeDrop_f1;
        mVelocity += front * cNum_exeDrop_f2;
    }

    boundDrop();
    MR::addVelocityToGravity(this, cNum_exeDrop_addVelocityToGravity_1);

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        mVelocity.zero();
        reset(0);
    }
}

void JetTurtle::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (isWait() && getNerveStep() >= 2 && !MR::isSensorEye(pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
        }

        return;
    }

    if (isWait()) {
        MR::sendMsgPush(pReceiver, pSender);
    }

    if (!isNerve(GET_NERVE(JetTurtle, JetTurtleNrvThrowing)) || MR::isSensorItem(pReceiver)) {
        return;
    }

    if (MR::isSensorEye(pSender)) {
        // A thrown red or gold shell turns toward a searchable target within sHomingAngle of its flight.
        if (mHomingTarget == nullptr && mShellType != SHELL_GREEN && MR::isExistInAttributeGroupSearchTurtle(pReceiver->mHost)) {
            Vec3f difference = pReceiver->mPosition - pSender->mPosition;

            if (MR::diffAngleAbs(mVelocity, difference) < sHomingAngle) {
                MR::normalizeOrZero(&difference);
                MR::vecBlendSphere(mThrowDir, difference, &mThrowDir, cNum_attackSensor_f1);
                mHomingTarget = pReceiver;
                MR::startBck(this, "BulletSearch");
            }
        }
    } else if (MR::sendArbitraryMsg(ACTMES_JET_TURTLE_ATTACK, pReceiver, pSender)) {
        MR::shakeCameraWeak();
        mIsResetRequested = true;
        reset(0);
    }
}

bool JetTurtle::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgItemGet(msg)) {
        if (mFlag.mIsDead) {
            return false;
        }

        if (isWait() || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve))) {
            mTakenSensor = pSender;
            setNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserveD));
            return true;
        }

        return false;
    }

    if (msg == ACTMES_DAMAGEDROP) {
        resetPositionAndVanish();
        return true;
    }

    if (msg == ACTMES_ATTACKDROP) {
        MR::shakeCameraWeak();
        reset(0);
        return true;
    }

    if (msg == ACTMES_RUSHDROP) {
        setNerve(GET_NERVE(JetTurtle, JetTurtleNrvDrop));
        MR::startBck(this, "Drop");
        return true;
    }

    if (msg == ACTMES_IS_PULL_ENABLE) {
        return isWait() || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve));
    }

    if (MR::isMsgItemPull(msg)) {
        if (isWait()) {
            MR::startBck(this, "YRotation");
            setNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve));
            MR::offBind(this);
        }

        return true;
    }

    return false;
}

// The player throws along its throw vector; anything else throws toward itself, homing when it is a search target
// (green shells never home). Speed type 0 adds the player's speed.
bool JetTurtle::receiveMsgThrow(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvThrowing));
    MR::deleteEffect(this, "BrakeLamp");

    if (MR::isSensorPlayer(pSender)) {
        MR::getPlayerThrowVec(&mThrowDir);
        mHomingTarget = nullptr;
    } else {
        if (MR::isExistInAttributeGroupSearchTurtle(pSender->mHost) && mShellType != SHELL_GREEN) {
            mHomingTarget = pSender;
        } else {
            mHomingTarget = nullptr;
        }

        mThrowDir = pSender->mPosition - mPosition;
        MR::normalizeOrZero(&mThrowDir);
    }

    if (mSpeedType == 0) {
        mThrowSpeedBonus = MR::getPlayerVelocity()->length();
    } else {
        mThrowSpeedBonus = 0.0f;
    }

    mThrowStartPos = mPosition;
    MR::invalidateClipping(this);
    MR::validateShadow(this, nullptr);
    return true;
}

bool JetTurtle::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (isWait() && MR::isMsgPlayerHipDrop(msg)) {
        reset(0);
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        MR::startSound(this, "SE_EM_TURTLE_SHELL_TRAMPLE");
        return true;
    }

    if (msg == ACTMES_FOO_ATTACK) {
        reset(0);
        return true;
    }

    return false;
}

bool JetTurtle::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserveD)) ||
        isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenStart))) {
        return false;
    }

    MR::sendArbitraryMsg(ACTMES_JET_TURTLE_ATTACK, pSender, getSensor("eye"));
    MR::shakeCameraWeak();
    reset(0);
    return true;
}

bool JetTurtle::isWait() const {
    return isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2));
}

bool JetTurtle::isRestart() const {
    return isNerve(GET_NERVE(JetTurtle, JetTurtleNrvRestart));
}

void GoldenTurtle::init(const TableIter& rIter) {
    init2(rIter, true);
    MR::startBtk(this, "KouraShine");
    getSensor("body")->setType(ATYPE_JET_TURTLE_SLOW);
}

void GoldenTurtle::resetPosition() {
    JetTurtle::resetPosition();
    getSensor("body")->setType(ATYPE_JET_TURTLE_SLOW);
}

void GoldenTurtle::exeThrowing() {
    if (MR::isFirstStep(this)) {
        getSensor("body")->setType(ATYPE_SPECIAL_WEAPON);

        if (mShellType == SHELL_GOLD) {
            MR::deleteEffect(this, "HandyGlow");
        }
    }

    JetTurtle::exeThrowing();
}

void GoldenTurtle::reset(u32 reason) {
    JetTurtle::reset(reason);
    getSensor("body")->setType(ATYPE_JET_TURTLE_SLOW);
}
