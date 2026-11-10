#include "Game/MapObj/Sandstorm.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrap_f1, "exeTrap.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrap_f2, "exeTrap.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrap_f3, "exeTrap.f3", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrap_f4, "exeTrap.f4", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrap_f5, "exeTrap.f5", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f11, "init.f11", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f12, "init.f12", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSandstormParam_f1, "initSandstormParam.f1", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_initTrapModel_f1, "initTrapModel.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBindActorMtx_f1, "updateBindActorMtx.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBindActorMtx_f2, "updateBindActorMtx.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBindActorMtx_f3, "updateBindActorMtx.f3", 270.0f);

namespace {
    NEBULA_TUNABLE(f32, sRailSpeed, 3.0f);
    NEBULA_TUNABLE(f32, sTurnRate, 2.0f);
    NEBULA_TUNABLE(f32, sActiveRadius, 7000.0f);
    NEBULA_TUNABLE(s32, sStepToEnableSpin, 15);
    static const s32 sTrapNum = 2;  // on opposite sides
    NEBULA_TUNABLE(f32, sTrapOffsetY, 200.0f);
    NEBULA_TUNABLE(f32, sTrapOffsetZ, 220.0f);
    NEBULA_TUNABLE(f32, sTrapRotateSpeed, 4.0f);
    static const char* const sParamFileName = "SandstormParam.bcsv";
};  // namespace

namespace NrvSunakazeKunTrap {
    NEW_NERVE(SunakazeKunTrapNrvTrap, SunakazeKunTrap, Trap);
};  // namespace NrvSunakazeKunTrap

SunakazeKunTrap::SunakazeKunTrap(LiveActor* pHost, const Vec3f& rLocalPos)
    : ModelObj("スナカゼ用トラップ", "SunakazeKunTrap", nullptr, -2, -2, -2, false), mHost(pHost), mLocalPos(rLocalPos) {
}

void SunakazeKunTrap::init(const TableIter& /*rIter*/) {
    initHitSensor(1);
    MR::addHitSensorEnemyAttack(this, "attack", 4, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initShadowVolumeCylinder(this, cNum_init_f2);

    Vec3f up;
    MR::calcUpVec(&up, mHost);
    up.negate();

    MR::setShadowDropDirection(this, nullptr, up);
    MR::setShadowDropLength(this, nullptr, cNum_init_f3);
    NebulaMtxMultVec(mHost->getBaseMtx(), &mLocalPos, &mPosition);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(SunakazeKunTrap, SunakazeKunTrapNrvTrap));
    makeActorAppeared();
}

void SunakazeKunTrap::exeTrap() {
    Vec3f axis = Vec3f(0.0f, 1.0f, 0.0f);

    MR::rotateVecDegree(&mLocalPos, axis, ::sTrapRotateSpeed);
    NebulaMtxMultVec(mHost->getBaseMtx(), &mLocalPos, &mPosition);
    f32 value = MR::getEaseInValue(getNerveStep() % 100, cNum_exeTrap_f1, 1.0f, cNum_exeTrap_f2);
    f32 angle = MR::sinDegree(getNerveStep() * cNum_exeTrap_f3);

    mPosition.scaleAdd(cNum_exeTrap_f4 * value * angle, axis, mPosition);
    Vec3f rotation(cNum_exeTrap_f5);
    mRotation.add(rotation);
}

void SunakazeKunTrap::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgEnemyAttackStrong(pReceiver, pSender);
    }
}

namespace NrvSandstorm {
    NEW_NERVE(SandstormNrvNonActive, Sandstorm, NonActive);
    NEW_NERVE(SandstormNrvWait, Sandstorm, Wait);
    NEW_NERVE(SandstormNrvStorm, Sandstorm, Storm);
    NEW_NERVE(SandstormNrvStormAdjust, Sandstorm, StormAdjust);
    NEW_NERVE(SandstormNrvStormFix, Sandstorm, StormFix);
    NEW_NERVE(SandstormNrvStormEnd, Sandstorm, StormEnd);
};  // namespace NrvSandstorm

Sandstorm::Sandstorm(const char* pName)
    : LiveActor(pName), mCameraInfo(), mBindActor(), mFront(0.0f, 0.0f, 1.0f), mClippingCenter(0.0f, 0.0f, 0.0f), mType(), mSpiralRot(), mSpiralRadius(), mSpiralAngle(), mBindRadius(),
      mAdjustStartRadius(), mBindOffsetY(), mBindStep(), mBindStartStep(), mSensorHeight(), mSizeUpRate(), mSensorMin(), mStepStorm(), mStepAdjust(), mStepFix(), mStepEnd(), mBindHeight(),
      mTrapModel() {
}

void Sandstorm::init(const TableIter& rIter) {
    const char* objName;

    MR::initDefaultPos(this, rIter);
    MR::getObjectName(&objName, rIter);

    if (MR::isEqualString("TwisterSan", objName)) {
        mType = Type_TwisterSan;
    } else if (MR::isEqualString("SunakazeKun", objName)) {
        mType = Type_SunakazeKun;
    } else if (MR::isEqualString("TwisterSanSlim", objName)) {
        mType = Type_TwisterSanSlim;
    }

    initModelManagerWithAnm(objName, nullptr, false);
    MR::connectToSceneNoShadowedMapObj(this);
    initSandstormParam();
    initHitSensor(2);
    MR::addHitSensorCallbackPriorBinder(this, "binder", 4, 0.0f);
    MR::addHitSensorMapObjSimple(this, "star_piece", 4, mSensorHeight / cNum_init_f4 + cNum_init_f5, Vec3f(0.0f, mSensorHeight / cNum_init_f6 + cNum_init_f7, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(6, false);
    MR::initShadowVolumeCylinder(this, cNum_init_f8);
    MR::setShadowVolumeStartDropOffset(this, nullptr, -cNum_init_f9);
    MR::setShadowDropLength(this, nullptr, cNum_init_f10);
    MR::calcGravity(this);
    MR::initActorCamera(this, rIter, &mCameraInfo);

    bool useTrapModel = false;
    MR::getPlacementArg0NoInit(rIter, &useTrapModel);

    if (useTrapModel) {
        initTrapModel();
    }

    initRailRider(rIter);
    MR::moveCoordAndTransToNearestRailPos(this);

    mFront.set(MR::getRailDirection(this));

    f32 boundingRadius;
    MR::calcModelBoundingRadius(&boundingRadius, this);

    f32 railBoundingRadius;
    MR::calcRailClippingInfo(&mClippingCenter, &railBoundingRadius, this, cNum_init_f11, cNum_init_f12);

    MR::setClippingTypeSphere(this, boundingRadius + railBoundingRadius, &mClippingCenter);
    MR::setGroupClipping(this, rIter, 8);
    MR::useStageSwitchWriteA(this, rIter);

    if (MR::isValidSwitchA(this)) {
        MR::onSwitchA(this);
    }

    initNerve(GET_NERVE(Sandstorm, SandstormNrvWait));
    makeActorAppeared();
}

void Sandstorm::exeNonActive() {
    if (MR::isFirstStep(this)) {
        MR::offCalcShadow(this, nullptr);
        MR::invalidateHitSensors(this);
        MR::deleteEffectAll(this);
    }

    if (MR::isNearPlayerAnyTime(this, ::sActiveRadius)) {
        MR::onCalcShadow(this, nullptr);
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(Sandstorm, SandstormNrvWait));
    }
}

void Sandstorm::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "Wait");
    }

    MR::turnDirectionToPlayerDegree(this, &mFront, ::sTurnRate);
    tryNonActive();
}

void Sandstorm::exeStorm() {
    if (MR::isFirstStep(this)) {
        if (isSunakazeKun()) {
            MR::startBck(this, "Spin");
        }
    }

    MR::tryRumblePadWeak(this, 0);
    updateSpiral();

    if ((!MR::isGreaterStep(this, ::sStepToEnableSpin) || !tryTornadoJumpPlayer()) && MR::isStep(this, mStepStorm)) {
        setNerve(GET_NERVE(Sandstorm, SandstormNrvStormAdjust));
    }
}

void Sandstorm::exeStormAdjust() {
    if (MR::isFirstStep(this)) {
        mAdjustStartRadius = mBindRadius;
    }

    MR::tryRumblePadVeryWeak(this, 0);
    updateSpiral();

    if (!tryTornadoJumpPlayer() && MR::isStep(this, mStepAdjust)) {
        setNerve(GET_NERVE(Sandstorm, SandstormNrvStormFix));
    }
}

void Sandstorm::exeStormFix() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("StormWait");

        if (MR::isValidSwitchA(this)) {
            MR::offSwitchA(this);
        }
    }

    MR::tryRumblePadVeryWeak(this, 0);

    if (isSunakazeKun()) {
        MR::startLevelSound(this, "SE_OJ_LV_SANDSTORM_S_CHANCE");
    } else {
        MR::startLevelSound(this, "SE_OJ_LV_SANDSTORM_CHANCE");
    }

    tryTornadoJumpPlayer();
}

void Sandstorm::exeStormEnd() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);

        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        // BUG: Invokes `__ptmf_test` instead of calling the function.
        if (&Sandstorm::isSunakazeKun != nullptr) {
            MR::startSound(this, "SE_OJ_SANDSTORM_S_BIND_END");
        } else {
            MR::startSound(this, "SE_OJ_SANDSTORM_BIND_END");
        }
    }

    if (MR::isGreaterStep(this, mStepEnd)) {
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(Sandstorm, SandstormNrvWait));
    }
}

void Sandstorm::control() {
    MR::moveCoordAndFollowTrans(this, ::sRailSpeed);

    if (isNerve(GET_NERVE(Sandstorm, SandstormNrvNonActive))) {
        return;
    }

    if (isSunakazeKun()) {
        MR::startLevelSound(this, "SE_OJ_LV_SANDSTORM_S_WAIT");

        if (mTrapModel != nullptr) {
            MR::startLevelSound(this, "SE_OJ_LV_SANDSTORM_S_HAMMER");
        }
    } else {
        MR::startLevelSound(this, "SE_OJ_LV_SANDSTORM_WAIT");
    }
}

void Sandstorm::calcAndSetBaseMtx() {
    Vec3f vec;
    MR::calcUpVec(&vec, this);
    vec.negate();

    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, vec, mFront);
    MR::setBaseTRMtx(this, mtx);
}

void Sandstorm::startClipped() {
    if (mTrapModel != nullptr) {
        for (s32 i = 0; i < ::sTrapNum; i++) {
            if (mTrapModel[i] != nullptr) {
                mTrapModel[i]->startClipped();
            }
        }
    }

    LiveActor::startClipped();
}

void Sandstorm::endClipped() {
    if (mTrapModel != nullptr) {
        for (s32 i = 0; i < ::sTrapNum; i++) {
            if (mTrapModel[i] != nullptr) {
                mTrapModel[i]->endClipped();
            }
        }
    }

    LiveActor::endClipped();
}

bool Sandstorm::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver != getSensor("star_piece")) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        Vec3f up;
        MR::calcUpVec(&up, this);

        f32 verticalDistance = MR::calcDistanceVertical(this, pSender->mPosition, up);

        if (verticalDistance > mSensorHeight + mSizeUpRate * mSensorHeight) {
            return false;
        }

        f32 horizontalDistance = MR::calcDistanceHorizontal(this, pSender->mPosition, up);
        bool isInside = horizontalDistance <= MR::max(mSensorMin, mSizeUpRate * verticalDistance);

        if (isInside) {
            MR::limitedStarPieceHitSound();
        }

        return isInside;
    }

    return false;
}

bool Sandstorm::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Sandstorm, SandstormNrvStormEnd))) {
        return false;
    }

    if (pReceiver == getSensor("star_piece")) {
        return false;
    }

    if (MR::isMsgAutoRushBegin(msg) && MR::isSensorPlayer(pSender)) {
        if (tryStartBind(pSender)) {
            if (isSunakazeKun()) {
                MR::startSound(this, "SE_OJ_SANDSTORM_BIND_S_IN");
            } else {
                MR::startSound(this, "SE_OJ_SANDSTORM_BIND_IN");
            }

            setNerve(GET_NERVE(Sandstorm, SandstormNrvStorm));

            return true;
        }

        return false;
    }

    if (MR::isMsgUpdateBaseMtx(msg) && mBindActor != nullptr) {
        updateBindActorMtx();

        return true;
    }

    return false;
}

void Sandstorm::updateHitSensor(HitSensor* pSensor) {
    Vec3f up;
    MR::calcUpVec(&up, this);

    Vec3f top;
    top.scaleAdd(mSensorHeight, up, mPosition);

    // The sensor rides the column at the player's height and widens toward the top.
    f32 heightRate = MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerCenterPos(), mPosition, top);
    pSensor->mRadius = MR::max(mSensorMin, mSizeUpRate * heightRate * mSensorHeight);
}

void Sandstorm::initSandstormParam() {
    const char* arcName = nullptr;

    if (mType == Type_TwisterSan) {
        arcName = "TwisterSan";
    } else if (mType == Type_SunakazeKun) {
        arcName = "SunakazeKun";
    } else if (mType == Type_TwisterSanSlim) {
        arcName = "TwisterSanSlim";
    }

    Table* csvParser = MR::createCsvParser(arcName, ::sParamFileName);

    MR::getCsvDataF32(&mSensorHeight, csvParser, "SensorHeight", 0);
    MR::getCsvDataF32(&mSizeUpRate, csvParser, "SizeUpRate", 0);
    MR::getCsvDataF32(&mSensorMin, csvParser, "SensorMin", 0);
    MR::getCsvDataS32(&mStepStorm, csvParser, "StepStorm", 0);
    MR::getCsvDataS32(&mStepAdjust, csvParser, "StepAdjust", 0);
    MR::getCsvDataS32(&mStepFix, csvParser, "StepFix", 0);
    MR::getCsvDataS32(&mStepEnd, csvParser, "StepEnd", 0);
    MR::getCsvDataF32(&mBindHeight, csvParser, "BindHeight", 0);
    MR::getCsvDataF32(&mSpiralRot, csvParser, "SpiralRot", 0);
    MR::getCsvDataF32(&mSpiralRadius, csvParser, "SpiralRadius", 0);

    // Step counts below 1 never reach their step test (0 divides the bind steps by zero), and a bind height of 0 divides the
    // start step by zero.
    if (mStepStorm < 1) {
        mStepStorm = 1;
    }

    if (mStepAdjust < 1) {
        mStepAdjust = 1;
    }

    if (MR::isNearZero(mBindHeight, cNum_initSandstormParam_f1)) {
        mBindHeight = 1.0f;
    }
}

void Sandstorm::initTrapModel() {
    if (!isSunakazeKun()) {
        return;
    }

    mTrapModel = new SunakazeKunTrap*[::sTrapNum];

    for (s32 i = 0; i < ::sTrapNum; i++) {
        Vec3f axis = Vec3f(0.0f, 1.0f, 0.0f);
        Vec3f offset = Vec3f(0.0f, ::sTrapOffsetY, ::sTrapOffsetZ);
        MR::rotateVecDegree(&offset, axis, i * cNum_initTrapModel_f1);

        mTrapModel[i] = new SunakazeKunTrap(this, offset);
        mTrapModel[i]->initWithoutIter();
    }
}

void Sandstorm::updateBindActorMtx() {
    f32 radius;
    Vec3f* pPosition;

    if (mBindStep < 10) {
        pPosition = &mPosition;
        Vec3f toPlayerCenter;
        toPlayerCenter.sub(*MR::getPlayerCenterPos(), *pPosition);

        Vec3f up;
        MR::calcUpVec(&up, this);

        Vec3f vec;
        vec.killElement(toPlayerCenter, up);

        f32 len = vec.length();
        f32 rate = mBindStep / cNum_updateBindActorMtx_f1;
        radius = MR::getLinerValue(rate, len, mBindRadius, 1.0f);
    } else {
        radius = mBindRadius;
    }

    Vec3f vec;
    vec.x = radius * MR::cos(mSpiralAngle);
    vec.y = mBindOffsetY;
    vec.z = radius * MR::sin(mSpiralAngle);

    bool isSunakaze = isSunakazeKun();

    if (!isSunakaze && isNerve(GET_NERVE(Sandstorm, SandstormNrvStormFix))) {
        vec.y += MR::sinDegree(getNerveStep() * cNum_updateBindActorMtx_f2) * cNum_updateBindActorMtx_f3;
    }

    pPosition = &mBindActor->mPosition;
    NebulaMtxMultVec(getBaseMtx(), &vec, pPosition);
    Pos3f mtx;
    mtx.set(mBindActor->getBaseMtx());
    mtx.setTrans(mBindActor->mPosition);
    MR::setBaseTRMtx(mBindActor, mtx);
}

void Sandstorm::endBind() {
    MR::tryRumblePadStrong(this, 0);
    MR::shakeCameraNormal();

    Vec3f up;
    MR::calcUpVec(&up, this);

    MR::endBindAndPlayerJump(this, up, 0);

    if (isSunakazeKun()) {
        MR::miniTornadoJumpPlayer();
        MR::startSound(this, "SE_OJ_SANDSTORM_S_BLOW_UP");
    } else {
        MR::tornadoJumpPlayer();
        MR::startSound(this, "SE_OJ_SANDSTORM_BLOW_UP");
    }

    mBindActor = nullptr;

    MR::validateClipping(this);
    MR::endActorCamera(this, mCameraInfo, true);
}

void Sandstorm::updateSpiral() {
    f32 x = mBindStartStep / mStepStorm;
    f32 startRadius = MR::getLinerValue(x, 0.0f, mSpiralRadius, 1.0f);
    f32 startOffsetY = MR::getLinerValue(x, 0.0f, mBindHeight, 1.0f);
    f32 startAngle = MR::toRadian(-mSpiralRot * x);

    if (mBindStep <= mStepStorm) {
        f32 x = static_cast< f32 >(mBindStep) / mStepStorm;

        mBindRadius = MR::getLinerValue(x, startRadius, mSpiralRadius, 1.0f);
        mBindOffsetY = MR::getLinerValue(x, startOffsetY, mBindHeight, 1.0f);
    } else {
        mBindRadius = MR::getLinerValue(static_cast< f32 >(mBindStep - mStepStorm) / mStepAdjust, mAdjustStartRadius, 0.0f, 1.0f);
        mBindOffsetY = mBindHeight;
    }

    mSpiralAngle = startAngle + MR::toRadian(-mSpiralRot * mBindStep);
    mBindStep++;
}

bool Sandstorm::tryStartBind(HitSensor* pSensor) {
    if (mBindActor != nullptr) {
        return false;
    }

    if (MR::isPlayerDamaging()) {
        return false;
    }

    if (MR::isPlayerElementModeTornado()) {
        return false;
    }

    mBindActor = pSensor->mHost;

    MR::tryRumblePadStrong(this, 0);
    MR::shakeCameraNormal();
    MR::startActorCameraTargetSelf(this, mCameraInfo);
    MR::startBckPlayer("StormIn");
    MR::startSound(mBindActor, "SE_PV_UPSET");
    MR::invalidateClipping(this);

    mSpiralAngle = 0.0f;
    mBindRadius = 0.0f;
    mAdjustStartRadius = 0.0f;
    mBindOffsetY = 0.0f;
    mBindStep = 0;

    Vec3f toBindActor;
    toBindActor.sub(mBindActor->mPosition, mPosition);
    Vec3f up;
    MR::calcUpVec(&up, this);

    mBindStartStep = mStepStorm * toBindActor.dot(up) / mBindHeight;

    return true;
}

bool Sandstorm::tryTornadoJumpPlayer() {
    if (!MR::isSpin(PLAYER_1)) {
        return false;
    }

    endBind();
    setNerve(GET_NERVE(Sandstorm, SandstormNrvStormEnd));

    return true;
}

bool Sandstorm::tryNonActive() {
    if (MR::isNearPlayerAnyTime(this, ::sActiveRadius)) {
        return false;
    }

    setNerve(GET_NERVE(Sandstorm, SandstormNrvNonActive));

    return true;
}

bool Sandstorm::isSunakazeKun() const {
    return mType == Type_SunakazeKun;
}

