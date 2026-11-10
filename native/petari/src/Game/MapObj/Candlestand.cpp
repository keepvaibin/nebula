#include "Game/MapObj/Candlestand.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Effect/SpinPullParticleCallBack.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_appearItem_f1, "appearItem.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearItem_f2, "appearItem.f2", 40.0f);

namespace NrvCandlestand {
    NEW_NERVE(HostTypeWaitFire, Candlestand, WaitFire);
    NEW_NERVE(HostTypeFire, Candlestand, Fire);
    NEW_NERVE(HostTypeBurn, Candlestand, Burn);
    NEW_NERVE(HostTypeAttack, Candlestand, Attack);
    NEW_NERVE(HostTypeExtinguish, Candlestand, Extinguish);
    NEW_NERVE(HostTypeFlicker, Candlestand, Flicker);
};  // namespace NrvCandlestand

namespace {
    struct Param {
        const char* mObjName;
        f32 mClippingRadius;
        f32 mSensorRange;
        f32 mSensorHeight;
        f32 mItemHeight;
        f32 mCoinAngle;
        bool mCanUseSwitch;
    };

    const Param sParams[] = {
        {"PhantomCandlestand", 500.0f, 50.0f, 220.0f, 0.0f, 0.0f, false},
        {"TeresaMansionCandlestand", 800.0f, 70.0f, 300.0f, 220.0f, -5.0f, false},
        {"CandlestandIceVolcano", 500.0f, 150.0f, 220.0f, 0.0f, 0.0f, true},
    };

    const char* const sBurnSound = "SE_OJ_LV_PHANTOM_TOACH_BURN";
    NEBULA_TUNABLE(f32, sAttackRange, 50.0f);
    NEBULA_TUNABLE(f32, sFireRange, 150.0f);
    NEBULA_TUNABLE(f32, sSpinStormRange, 350.0f);
    const s32 sFireStep = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sExtinguishStep, 15);
    const s32 sFlickerStep = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sAttackStep = NebulaFrameClock::stepsOfMilliseconds(500);

    const Param* getParam(const char* pObjName) {
        for (u32 i = 0; i < ARRAY_SIZE(::sParams); i++) {
            if (MR::isEqualString(pObjName, ::sParams[i].mObjName)) {
                return &::sParams[i];
            }
        }

        // An object name outside the table takes the first entry's values.
        return &::sParams[0];
    }
};  // namespace

Candlestand::Candlestand(const char* pName) : MapObjActor(pName), mItem(-1), mHasItemAppear(), mSpinPtclCb() {
}

void Candlestand::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupHitSensor();

    const Param* pParam = ::getParam(mObjectName);
    info.setupHitSensorParam(8, pParam->mSensorRange, Vec3f(0.0f, pParam->mSensorHeight, 0.0f));
    info.setupClippingRadius(pParam->mClippingRadius);
    info.setupNerve(GET_NERVE(Candlestand, HostTypeBurn));
    info.setupAffectedScale();
    initialize(rIter, info);

    MR::getPlacementArg0NoInit(rIter, &mItem);
    if (mItem == -1) {
        MR::declareCoin(this, 1);
    }

    if (mItem == 0) {
        MR::declareStarPiece(this, 1);
    }

    if (::getParam(mObjectName)->mCanUseSwitch) {
        setNerve(MR::isValidSwitchA(this) ? static_cast< const Nerve* >(GET_NERVE(Candlestand, HostTypeWaitFire))
                                          : GET_NERVE(Candlestand, HostTypeBurn));
    }

    mSpinPtclCb = new SpinPullParticleCallBack();
}

void Candlestand::makeActorAppeared() {
    LiveActor::makeActorAppeared();

    if (!::getParam(mObjectName)->mCanUseSwitch && !MR::isClipped(this)) {
        emitEffectFire();
    }
}

void Candlestand::startClipped() {
    MapObjActor::startClipped();

    if (isBurning()) {
        deleteEffectFire();
    }
}

void Candlestand::endClipped() {
    if (isBurning()) {
        emitEffectFire();
    }

    MapObjActor::endClipped();
}

void Candlestand::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(Candlestand, HostTypeBurn))) {
        return;
    }

    if (MR::isNear(pSender, pReceiver, mScale.x * ::sAttackRange + pReceiver->mRadius) && MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
        setNerve(GET_NERVE(Candlestand, HostTypeAttack));
    }
}

bool Candlestand::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return isNerve(GET_NERVE(Candlestand, HostTypeWaitFire)) && MR::isMsgFireBallAttack(msg) && tryFire(pSender, pReceiver);
}

bool Candlestand::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return isNerve(GET_NERVE(Candlestand, HostTypeWaitFire)) && MR::isMsgEnemyAttackFire(msg) && tryFire(pSender, pReceiver);
}

bool Candlestand::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isInSpinStormRange(msg, pSender, pReceiver, ::sSpinStormRange * mScale.x) || !isNerve(GET_NERVE(Candlestand, HostTypeBurn))) {
        return false;
    }

    setNerve(::getParam(mObjectName)->mCanUseSwitch ? static_cast< const Nerve* >(GET_NERVE(Candlestand, HostTypeFlicker))
                                                    : GET_NERVE(Candlestand, HostTypeExtinguish));
    return true;
}

bool Candlestand::isBurning() const {
    return isNerve(GET_NERVE(Candlestand, HostTypeBurn)) || isNerve(GET_NERVE(Candlestand, HostTypeAttack));
}

bool Candlestand::tryFire(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isNear(pSender, pReceiver, mScale.x * ::sFireRange + pSender->mRadius)) {
        return false;
    }

    setNerve(GET_NERVE(Candlestand, HostTypeFire));
    return true;
}

void Candlestand::emitEffectFire() {
    if (isObjectName("TeresaMansionCandlestand")) {
        MR::emitEffect(this, "Fire01");
        MR::emitEffect(this, "Fire02");
    } else {
        MR::emitEffect(this, "Fire");
    }
}

void Candlestand::deleteEffectFire() {
    if (isObjectName("TeresaMansionCandlestand")) {
        MR::deleteEffect(this, "Fire01");
        MR::deleteEffect(this, "Fire02");
    } else {
        MR::deleteEffect(this, "Fire");
    }
}

void Candlestand::emitEffectExtinguishFire() {
    if (isObjectName("TeresaMansionCandlestand")) {
        MR::emitEffectWithParticleCallBack(this, "Extinguish01", mSpinPtclCb);
        MR::emitEffectWithParticleCallBack(this, "Extinguish02", mSpinPtclCb);
    } else {
        MR::emitEffectWithParticleCallBack(this, "Extinguish", mSpinPtclCb);
    }
}

void Candlestand::appearItem() {
    if (mHasItemAppear) {
        return;
    }

    const Param* pParam = ::getParam(mObjectName);
    Vec3f position(mPosition);
    f32 height = pParam->mItemHeight;
    if (pParam->mCanUseSwitch) {
        height *= mScale.y;
    }

    position.y += height;
    if (mItem == -1) {
        f32 angle = pParam->mCoinAngle;
        if (MR::isNearZero(angle)) {
            MR::appearCoinPop(this, position, 1);
        } else {
            Vec3f front;
            MR::calcFrontVec(&front, this);

            Vec3f up;
            MR::calcUpVec(&up, this);

            Pos3f rotation;
            rotation.makeRotate(front, MR::toRadian(angle));
            rotation.mult(up, up);

            MR::appearCoinPopToDirection(this, position, up, 1);
        }
    } else if (mItem == 0) {
        if (MR::appearStarPiece(this, position, 1, cNum_appearItem_f1, cNum_appearItem_f2, false)) {
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        }
    }

    mHasItemAppear = true;
}

void Candlestand::exeWaitFire() {
}

void Candlestand::exeFire() {
    if (MR::isFirstStep(this)) {
        emitEffectFire();
        MR::startSound(this, "SE_OJ_FIRE_STAND_IGNIT");
    }

    MR::startLevelSound(this, ::sBurnSound);

    if (MR::isStep(this, ::sFireStep)) {
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        setNerve(GET_NERVE(Candlestand, HostTypeBurn));
    }
}

void Candlestand::exeExtinguish() {
    if (MR::isFirstStep(this)) {
        deleteEffectFire();
        emitEffectExtinguishFire();
        MR::startSound(this, "SE_OJ_PHANTOM_TOACH_OFF");
    }

    if (MR::isStep(this, ::sExtinguishStep)) {
        if (MR::isValidSwitchA(this)) {
            MR::offSwitchA(this);
        }

        appearItem();

        if (::getParam(mObjectName)->mCanUseSwitch) {
            setNerve(GET_NERVE(Candlestand, HostTypeWaitFire));
        }
    }
}

void Candlestand::exeFlicker() {
    if (MR::isFirstStep(this)) {
        deleteEffectFire();

        if (isObjectName("TeresaMansionCandlestand")) {
            MR::emitEffect(this, "Extinguish01");
            MR::emitEffect(this, "Extinguish02");
        } else {
            MR::emitEffect(this, "Extinguish");
        }

        MR::startSound(this, "SE_OJ_PHANTOM_TOACH_OFF");
    }

    if (MR::isStep(this, ::sFlickerStep)) {
        emitEffectFire();
        setNerve(GET_NERVE(Candlestand, HostTypeBurn));
    }
}

void Candlestand::exeAttack() {
    if (MR::isStep(this, ::sAttackStep)) {
        setNerve(GET_NERVE(Candlestand, HostTypeBurn));
    } else {
        MR::startLevelSound(this, ::sBurnSound);
    }
}

void Candlestand::exeBurn() {
    MR::startLevelSound(this, ::sBurnSound);
}
