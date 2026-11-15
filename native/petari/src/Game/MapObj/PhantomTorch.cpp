#include "Game/MapObj/PhantomTorch.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Effect/SpinPullParticleCallBack.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvPhantomTorch {
    NEW_NERVE(HostTypeWait, PhantomTorch, Wait);
    NEW_NERVE(HostTypeAttack, PhantomTorch, Attack);
    NEW_NERVE(HostTypeExtinguish, PhantomTorch, Extinguish);
};  // namespace NrvPhantomTorch

PhantomTorch::PhantomTorch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< PhantomTorch >);
    mParticle = nullptr;
    mItem = -1;
    mPullParticle = nullptr;
    mIsDecorative = false;
    mBurnSe = nullptr;
}

void PhantomTorch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    f32 x = mScale.x;
    MR::getPlacementArg0NoInit(rIter, &mItem);
    s32 arg = -1;
    MR::getPlacementArg1NoInit(rIter, &arg);
    mIsDecorative = arg != -1;
    MR::getObjectName(&mParticle, rIter);
    mBurnSe = MR::isEqualString(mParticle, "PhantomBonfire") ? "SE_OJ_LV_BONFIRE_BURN" : "SE_OJ_LV_PHANTOM_TOACH_BURN";

    if (mItem == -1) {
        MR::declareCoin(this, 1);
    }

    if (!mItem) {
        MR::declareStarPiece(this, 1);
    }

    initEffectKeeper(0, mParticle);
    mPullParticle = new SpinPullParticleCallBack();
    initSound(4, false);
    initHitSensor(1);
    Vec3f vec;
    vec.x = 0.0f;
    vec.y = 70.0f * x;
    vec.z = 0.0f;
    MR::addHitSensorMapObj(this, "body", 8, 50.0f * x, vec);
    MR::setClippingTypeSphere(this, 500.0f * x);
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    initNerve(GET_NERVE(PhantomTorch, HostTypeWait));
    MR::connectToSceneMapObjMovement(this);
    makeActorAppeared();
    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::listenStageSwitchOnOffAppear(this, MR::Functor(this, &PhantomTorch::makeActorAppeared), MR::Functor(this, &PhantomTorch::makeActorDead));
        makeActorDead();
    }
}

void PhantomTorch::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    if (!MR::isClipped(this)) {
        MR::emitEffect(this, mParticle);
    }
}

void PhantomTorch::makeActorDead() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
    MR::deleteEffect(this, mParticle);
    LiveActor::makeActorDead();
}

void PhantomTorch::kill() {
    switch (mItem) {
    case -1:
        MR::appearCoinPop(this, mPosition, 1);
        break;
    case 1:
        break;
    case 2:
        break;
    case 0:
        if (MR::appearStarPiece(this, mPosition, 1, 10.0f, 40.0f, false)) {
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        }
        break;
    }
    soundOrder();
    LiveActor::kill();
}

void PhantomTorch::startClipped() {
    LiveActor::startClipped();
    MR::deleteEffect(this, mParticle);
}

void PhantomTorch::endClipped() {
    MR::emitEffect(this, mParticle);
    LiveActor::endClipped();
}

void PhantomTorch::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!mIsDecorative && isNerve(GET_NERVE(PhantomTorch, HostTypeWait)) && MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
        setNerve(GET_NERVE(PhantomTorch, HostTypeAttack));
    }
}

bool PhantomTorch::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsDecorative) {
        return false;
    }

    if (MR::isInSpinStormRange(msg, pSender, pReceiver, 350.0f) && isNerve(GET_NERVE(PhantomTorch, HostTypeWait))) {
        setNerve(GET_NERVE(PhantomTorch, HostTypeExtinguish));
        return true;
    }
    return false;
}

void PhantomTorch::soundOrder() {
    if (MR::getGroupFromArray(this) && MR::getGroupFromArray(this)->getObjNum() > 0) {
        s32 i;
        LiveActorGroup* group;
        s32 num = 0;
        group = MR::getGroupFromArray(this);

        for (i = 0; i < group->getObjNum(); i++) {
            if (!MR::isDead(group->getActor(i))) {
                num++;
            }
        }

        switch (num) {
        case 1:
            MR::startSystemSE("SE_SY_CANDLE_COMBO_5");
            break;
        case 2:
            MR::startSystemSE("SE_SY_CANDLE_COMBO_4");
            break;
        case 3:
            MR::startSystemSE("SE_SY_CANDLE_COMBO_3");
            break;
        case 4:
            MR::startSystemSE("SE_SY_CANDLE_COMBO_2");
            break;
        case 5:
        default:
            MR::startSystemSE("SE_SY_CANDLE_COMBO_1");
            break;
        }
    }
}

void PhantomTorch::exeWait() {
    MR::startLevelSound(this, mBurnSe);
}

void PhantomTorch::exeAttack() {
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        setNerve(GET_NERVE(PhantomTorch, HostTypeWait));
    } else {
        MR::startLevelSound(this, mBurnSe);
    }
}

void PhantomTorch::exeExtinguish() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffect(this, mParticle);
        MR::emitEffectWithParticleCallBack(this, "Extinguish", mPullParticle);
        MR::startSound(this, "SE_OJ_PHANTOM_TOACH_OFF");
    }

    if (MR::isStep(this, 15)) {
        kill();
    }
}
