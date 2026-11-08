#include "Game/MapObj/JetTurtle.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"

namespace {
    static const f32 sThrowSpdStraight[] = {30.0f, 20.0f, 30.0f};
    static const f32 sThrowSpdHoming[] = {24.0f, 20.0f, 24.0f};
    static const f32 sGravityLevel[] = {0.017f, 0.01f, 0.0055f};
    static const u16 sResetStep[] = {300, 480, 300};

    static const f32 sBoundReductionXZ = 0.8f;
    static const f32 sBoundReductionY = 0.9f;
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

JetTurtle::JetTurtle(const char* pName) : LiveActor(pName), mIsResetRequested() {
}

void JetTurtle::init(const TableIter& rIter) {
    init2(rIter, 0);
}

void JetTurtle::init2(const TableIter& rIter, s32 param2) {
    mSpinTime = 0;

    MR::initDefaultPos(this, rIter);

    s32 shellType = 0;
    s32 arg1 = -1;

    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg0NoInit(rIter, &shellType);
        MR::getPlacementArg1NoInit(rIter, &arg1);

        arg1++;
    } else {
        arg1 = 0;
    }

    mType = arg1;
    mHasReviveLight = false;
    mIsRevive = true;

    f32 shadowDropLength = 1000.0f;

    if (MR::isValidInfo(rIter)) {
        s32 arg2 = -1;
        MR::getPlacementArg2NoInit(rIter, &arg2);

        if (arg2 == 1) {
            mIsRevive = false;
        }

        s32 arg3 = -1;
        MR::getPlacementArg3NoInit(rIter, &arg3);

        if (arg3 == 1) {
            mHasReviveLight = true;
        }

        s32 arg7 = -1;
        MR::getPlacementArg7NoInit(rIter, &arg7);

        if (arg7 != -1) {
            shadowDropLength = arg7;
        }
    }

    if (param2 != 0) {
        shellType = JETTURTLETYPE_GOLD;
    }

    switch (shellType) {
    case 0:
    case JETTURTLETYPE_RED:
        initModelManagerWithAnm("Koura", nullptr, false);
        break;
    case JETTURTLETYPE_GOLD:
        initModelManagerWithAnm("KouraShine", nullptr, false);
        break;
    }

    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    initHitSensor(2);
    MR::addHitSensor(this, "body", ATYPE_JET_TURTLE, 8, 50.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEye(this, "eye", 8, 1000.0f, Vec3f(0.0f, 0.0f, 0.0f));

    if (shellType < JETTURTLETYPE_GOLD) {
        MR::startBrk(this, "Koura");
        MR::setBrkFrameAndStop(this, 1.0f);
    }

    if (shellType != JETTURTLETYPE_RED) {
        if (shellType == 0) {
            becomeSlowType();
        } else {
            getSensor("body")->setType(ATYPE_JET_TURTLE_SLOW);
        }
    }

    mShellType = shellType;
    mThrowSpeedBonus = 0.0f;

    initNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait));

    MR::initShadowVolumeCylinder(this, 50.0f);
    MR::setShadowDropLength(this, nullptr, shadowDropLength);
    MR::setShadowVolumeEndDropOffset(this, nullptr, 50.0f);
    MR::onShadowVolumeCutDropLength(this, nullptr);

    initSound(6, false);
    initBinder(35.0f, 20.0f, 8);
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
    mShellType = 0;
}

void JetTurtle::becomeFastType() {
    getSensor("body")->setType(ATYPE_JET_TURTLE);
    MR::startBrk(this, "Koura");
    MR::setBrkFrameAndStop(this, 1.0f);
    mShellType = 1;
}

// ...

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
        // why?
        MR::emitEffect(this, "Vanish");
        break;
    case JETTURTLETYPE_RED:
        MR::emitEffect(this, "VanishRed");
        break;
    case JETTURTLETYPE_GOLD:
        MR::emitEffect(this, "VanishGold");
        break;
    }

    if (!mIsRevive) {
        kill();
    }
}

void JetTurtle::exeWait() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve))) {
            switch (mShellType) {
            default:
                MR::emitEffect(this, "SpinBlur");
                break;
            case JETTURTLETYPE_RED:
                MR::emitEffect(this, "SpinBlurRed");
                break;
            case JETTURTLETYPE_GOLD:
                MR::emitEffect(this, "SpinBlurGold");
                break;
            }

            MR::invalidateClipping(this);
            return;
        }

        switch (mShellType) {
        case JETTURTLETYPE_GOLD:
            MR::emitEffect(this, "Glow");
            break;
        }

        MR::calcGravity(this);
        MR::onCalcShadowOneTime(this, nullptr);
        MR::validateShadow(this, nullptr);
        MR::validateHitSensors(this);
        getSensor("eye")->invalidate();
        MR::startBck(this, "Wait");
    }

    if (mSpinTime != 0) {
        mRotation.y = 24.0f * mSpinTime;
        mSpinTime--;
    }

    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve))) {
        if (getNerveStep() >= 120) {
            Vec3f direction = *MR::getPlayerCenterPos() - mPosition;
            f32 rate = MR::clamp((getNerveStep() - 120) / 120.0f, 0.0f, 1.0f);
            direction.setLength(mVelocity.length());
            MR::vecBlendSphere(mVelocity, direction, &mVelocity, rate);
        }

        if (MR::isStep(this, 300)) {
            reset(0);
        }
    }
}

void JetTurtle::exeThrowWait() {
    if (MR::isFirstStep(this)) {
        MR::onCalcGravity(this);
    }

    MR::invalidateHitSensors(this);

    if (mShellType == 2) {
        if (MR::isHiddenModel(this)) {
            MR::deleteEffect(this, "HandyGlow");
        } else {
            MR::emitEffect(this, "HandyGlow");
        }
    }

    if (MR::isPlayerInWaterMode()) {
        MR::tryStartBck(this, "SwimFlutterboard");

        if (mShellType != static_cast< s16 >(JETTURTLETYPE_RED)) {
            MR::emitEffect(this, "SwimBubble");
        } else {
            MR::emitEffect(this, "SwimBubbleRed");
        }

        if (mShellType == 2) {
            MR::startLevelSound(this, "SE_OJ_LV_JET_TURTLE_G_LIGHT");
        }
    } else {
        MR::tryStartBck(this, "CarryWait");
        MR::deleteEffect(this, "SwimBubble");
        MR::deleteEffect(this, "SwimBubbleRed");
    }
}

void JetTurtle::appear() {
    LiveActor::appear();
    mHomingTarget = nullptr;
    mTakenSensor = nullptr;
    MR::showModel(this);

    if (mIsAppeared != 0) {
        if (mType == 0) {
            Vec3f gravityVec;
            Vec3f gravityVec2;
            Vec3f frontVec;
            MR::calcGravityVectorOrZero(this, mPosition, &gravityVec, nullptr, 0);
            MR::calcFrontVec(&frontVec, this);
            MR::calcGravityVectorOrZero(this, mPosition + frontVec * 100.0f, &gravityVec2, nullptr, 0);

            if (!MR::isNearZero(gravityVec - gravityVec2)) {
                mType = 1;
            }
        }

        setNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2));
        HitSensor* pSensor = getSensor("body");
        pSensor->mRadius = 60.0f;
        initAfterPlacement();
        return;
    }

    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait));
}

void JetTurtle::bound() {
    Vec3f v24;
    MR::calcGravityVector(this, &v24, nullptr, 0);
    Vec3f v23;
    MR::vecKillElement(mVelocity, v24, &v23);

    if (MR::isBindedGround(this)) {
        Vec3f v22;
        f32 v3 = MR::vecKillElement(mThrowDir, *MR::getGroundNormal(this), &v22);

        if (v3 < 0.0f) {
            mThrowDir = v22 - *MR::getGroundNormal(this) * v3 * ::sBoundReductionXZ;
        }
    }

    if (MR::isBindedWall(this)) {
        Vec3f v21;
        f32 v6 = MR::vecKillElement(mThrowDir, *MR::getWallNormal(this), &v21);

        if (v6 < 0.0f) {
            mThrowDir = v21 - *MR::getWallNormal(this) * v6 * ::sBoundReductionY;
        }
    }

    if (MR::isBindedRoof(this)) {
        Vec3f v21;
        f32 v6 = MR::vecKillElement(mThrowDir, *MR::getRoofNormal(this), &v21);

        if (v6 < 0.0f) {
            mThrowDir = v21 - *MR::getRoofNormal(this) * v6 * ::sBoundReductionXZ;
        }
    }
}

inline void JetTurtle::endWait() {
    if (mShellType == static_cast< s16 >(JETTURTLETYPE_GOLD)) {
        MR::deleteEffect(this, "Glow");
    }
}

void JetTurtle::reset(u32 reason) {
    mIsResetRequested = 0;
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
        case 1:
            MR::emitEffect(this, "ExplosionRed");
            break;
        case 2:
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
        if (vertical < 5.0f) {
            mVelocity.zero();
        } else {
            horizontal.scale(0.8999999761581421f);
            mVelocity = horizontal - gravity * vertical * 0.800000011920929f;
        }
    }

    if (MR::isBindedWall(this)) {
        horizontal.scale(-0.8999999761581421f);
        mVelocity = horizontal + gravity * vertical;
    }

    if (MR::isBindedRoof(this)) {
        mVelocity.zero();
    }
}

void JetTurtle::exeThrowing() {
    if (!mIsResetRequested) {
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
                case JETTURTLETYPE_RED:
                    MR::emitEffect(this, "BlurRed");
                    break;
                case JETTURTLETYPE_GOLD:
                    MR::emitEffect(this, "BlurGold");
                    break;
                }
            }
        }

        if (MR::isStep(this, ::sResetStep[mType])) {
            if ((mThrowStartPos - mPosition).length() > 5000.0f) {
                reset(1);
            } else {
                reset(0);
            }

            return;
        }

        Vec3f v22;

        if (mHomingTarget != nullptr) {
            if (!mHomingTarget->mValidByHost) {
                mHomingTarget = nullptr;
            } else {
                Vec3f v21 = mHomingTarget->mPosition - mPosition;
                MR::normalizeOrZero(&v21);
                MR::vecBlendSphere(mThrowDir, v21, &mThrowDir, 0.2f);

                if (MR::isBckOneTimeAndStopped(this)) {
                    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
                        MR::startBck(this, "BulletWater");
                    } else {
                        MR::startBck(this, "Bullet");
                    }
                }

                v22 = mThrowDir;
                v22.setLength(mThrowSpeedBonus + ::sThrowSpdHoming[mType]);
                mVelocity = v22;
            }
        }

        if (mHomingTarget == nullptr) {
            v22 = mThrowDir;
            v22.setLength(mThrowSpeedBonus + ::sThrowSpdStraight[mType]);
            mVelocity = v22;
        }

        if ((mThrowStartPos - mPosition).length() > 10000.0f) {
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
        Vec3f v20(jointMtx[0][1], jointMtx[1][1], jointMtx[2][1]);
        Vec3f grav;
        MR::calcGravityVector(this, &grav, nullptr, 0);

        if (MR::isNearZero(grav)) {
            MR::shakeCameraWeak();
            reset(0);
            return;
        }

        if (!MR::isSameDirection(grav, mVelocity)) {
            v20 = -grav;
        }

        if (mHomingTarget == nullptr) {
            if (!(grav == mLastGravity)) {
                MR::vecBlendSphere(mThrowDir, grav, &mThrowDir, ::sGravityLevel[mType]);
            }

            mLastGravity = grav;
        }

        Vec3f v18 = mVelocity.cross(v20);
        v20.cross(v18, mVelocity);

        if (!MR::isNearZero(mVelocity)) {
            Pos3f frontUp;
            MR::makeMtxFrontUp(&frontUp, mVelocity, v20);
            frontUp.getEulerXYZ(mRotation);
            mRotation.mult(57.295776f);
        }
    }
}

void JetTurtle::exeTakenReserve() {
    if (MR::isFirstStep(this)) {
        HitSensor* bodySensor = getSensor("body");
        bodySensor->mRadius = 50.0f;
        mVelocity.zero();

        if (MR::isPlayerInWaterMode()) {
            if (mShellType != static_cast< s16 >(JETTURTLETYPE_RED)) {
                MR::emitEffect(this, "SwimBubble");
            } else {
                MR::emitEffect(this, "SwimBubbleRed");
            }
        }
    }

    f32 v2;

    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserveD))) {
        v2 = 0.5f;
    } else {
        v2 = 0.25f;
    }

    Vec3f takePos;
    MR::getPlayerTakePos(&takePos);
    mPosition = mPosition * (1.0f - v2) + takePos * v2;

    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserveD))) {
        if (MR::isStep(this, 2)) {
            setNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenStart));
        }

        return;
    }

    if (MR::isStep(this, 8)) {
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

        if (mShellType == JETTURTLETYPE_GOLD) {
            MR::deleteEffect(this, "Glow");
            MR::emitEffect(this, "HandyGlow");
        }

        setNerve(GET_NERVE(JetTurtle, JetTurtleNrvThrowWait));
        MR::invalidateShadow(this, nullptr);
    } else {
        reset(0);
    }
}

// missing branch
void JetTurtle::exeRestart() {
    if (MR::isFirstStep(this)) {
    }

    if (MR::isStep(this, 45)) {
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

    if (MR::isGreaterStep(this, 45)) {
        if (mHasReviveLight) {
            MR::startLevelSound(this, "SE_EM_LV_NOKOLAND_REVIVE_EFFECT");
        }
    } else {
        return;
    }

    if (!mHasReviveLight) {
        s32 step = getNerveStep();

        switch (step & 0x3) {
        case 0:
            MR::showModel(this);
            break;
        case 2:
            MR::hideModel(this);
            break;
        }
    }

    if (MR::isStep(this, 105)) {
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
        mVelocity = -mGravity * 10.0f;
        mVelocity += front * 10.0f;
    }

    boundDrop();
    MR::addVelocityToGravity(this, 1.0f);

    if (MR::isStep(this, 90)) {
        mVelocity.zero();
        reset(0);
    }
}
inline bool JetTurtle::isNerveWait() {
    return isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2));
}

void JetTurtle::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (isNerveWait()) {
            if (getNerveStep() >= 2 && !MR::isSensorEye(pSender)) {
                MR::sendMsgPush(pReceiver, pSender);
            }
        }

        return;
    }

    if (isNerveWait()) {
        MR::sendMsgPush(pReceiver, pSender);
    }

    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvThrowing)) && !MR::isSensorItem(pReceiver)) {
        if (MR::isSensorEye(pSender)) {
            if (mHomingTarget == nullptr && mShellType != 0 && MR::isExistInAttributeGroupSearchTurtle(pReceiver->mHost)) {
                Vec3f difference = pReceiver->mPosition - pSender->mPosition;

                if (MR::diffAngleAbs(mVelocity, difference) < 0.5235988f) {
                    MR::normalizeOrZero(&difference);
                    MR::vecBlendSphere(mThrowDir, difference, &mThrowDir, 0.2f);
                    mHomingTarget = pReceiver;
                    MR::startBck(this, "BulletSearch");
                }
            }
        } else if (MR::sendArbitraryMsg(ACTMES_JET_TURTLE_ATTACK, pReceiver, pSender)) {
            MR::shakeCameraWeak();
            mIsResetRequested = 1;
            reset(0);
        }
    }
}

bool JetTurtle::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgItemGet(msg)) {
        if (mFlag.mIsDead) {
            return false;
        } else {
            bool v7 = false;

            if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2))) {
                v7 = true;
            }

            if (v7 || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve))) {
                mTakenSensor = pSender;
                setNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserveD));
                return true;
            } else {
                return false;
            }
        }
    } else {
        if (msg == ACTMES_DAMAGEDROP) {
            resetPositionAndVanish();
            return true;
        } else if (msg == ACTMES_ATTACKDROP) {
            MR::shakeCameraWeak();
            reset(0);
            return true;
        } else if (msg == ACTMES_RUSHDROP) {
            setNerve(GET_NERVE(JetTurtle, JetTurtleNrvDrop));
            MR::startBck(this, "Drop");
            return true;
        } else if (msg == ACTMES_IS_PULL_ENABLE) {
            bool v8 = false;

            if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2))) {
                v8 = true;
            }

            if (v8) {
                return true;
            } else {
                return isNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve));
            }
        } else {
            if (MR::isMsgItemPull(msg)) {
                bool v9 = false;

                if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2))) {
                    v9 = true;
                }

                if (v9) {
                    bool v10 = false;

                    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2))) {
                        v10 = true;
                    }

                    if (v10) {
                        MR::startBck(this, "YRotation");
                    }

                    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvTakenReserve));
                    MR::offBind(this);
                }

                return true;
            }

            return false;
        }
    }
}

bool JetTurtle::receiveMsgThrow(HitSensor* pSender, HitSensor* pReceiver) {
    setNerve(GET_NERVE(JetTurtle, JetTurtleNrvThrowing));
    MR::deleteEffect(this, "BrakeLamp");

    if (MR::isSensorPlayer(pSender)) {
        MR::getPlayerThrowVec(&mThrowDir);
        mHomingTarget = nullptr;
    } else {
        if (MR::isExistInAttributeGroupSearchTurtle(pSender->mHost)) {
            mHomingTarget = pSender;
        } else {
            mHomingTarget = nullptr;
        }

        if (mShellType == 0) {
            mHomingTarget = nullptr;
        }

        mThrowDir = pSender->mPosition - mPosition;
        MR::normalizeOrZero(&mThrowDir);
    }

    if (!mType) {
        mThrowSpeedBonus = MR::getPlayerVelocity()->length();
    } else {
        mThrowSpeedBonus = 0.0f;
    }

    mThrowStartPos = mPosition;
    MR::invalidateClipping(this);
    MR::validateShadow(this, nullptr);
    return true;
}

bool JetTurtle::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2))) {
        if (MR::isMsgPlayerHipDrop(msg)) {
            reset(0);
            return true;
        }
    }

    if (MR::isMsgPlayerTrample(msg)) {
        MR::startSound(this, "SE_EM_TURTLE_SHELL_TRAMPLE");
        return true;
    } else {
        if (msg == ACTMES_FOO_ATTACK) {
            reset(0);
            return true;
        }
    }

    return false;
}

bool JetTurtle::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
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
    bool isWait = false;

    if (isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait)) || isNerve(GET_NERVE(JetTurtle, JetTurtleNrvWait2))) {
        isWait = true;
    }

    return isWait;
}

bool JetTurtle::isRestart() const {
    return isNerve(GET_NERVE(JetTurtle, JetTurtleNrvRestart));
}

void GoldenTurtle::init(const TableIter& rIter) {
    init2(rIter, 1);
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

        if (mShellType == JETTURTLETYPE_GOLD) {
            MR::deleteEffect(this, "HandyGlow");
        }
    }

    JetTurtle::exeThrowing();
}

void GoldenTurtle::reset(u32 a1) {
    JetTurtle::reset(a1);
    getSensor("body")->setType(ATYPE_JET_TURTLE_SLOW);
}

JetTurtle::~JetTurtle() {
}

GoldenTurtle::~GoldenTurtle() {
}
