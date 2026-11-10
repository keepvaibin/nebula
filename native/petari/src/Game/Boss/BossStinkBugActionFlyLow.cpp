#include "Game/Boss/BossStinkBugActionFlyLow.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Boss/BossStinkBugActionBase.hpp"
#include "Game/Boss/BossStinkBugFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_BossStinkBugActionFlyLow_f1, "BossStinkBugActionFlyLow.f1", 14.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_BossStinkBugActionFlyLow_f2, "BossStinkBugActionFlyLow.f2", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_BossStinkBugActionFlyLow_f3, "BossStinkBugActionFlyLow.f3", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appear_f1, "appear.f1", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f1, "exeFly.f1", 0.05235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f2, "exeFly.f2", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f3, "exeFly.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f4, "exeFly.f4", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDash_f1, "exeDash.f1", 15.0f);

namespace NrvBossStinkBugActionFlyLow {
    NEW_NERVE(BossStinkBugActionFlyLowNrvFly, BossStinkBugActionFlyLow, Fly);
    NEW_NERVE(BossStinkBugActionFlyLowNrvShakeOffSign, BossStinkBugActionFlyLow, ShakeOffSign);
    NEW_NERVE(BossStinkBugActionFlyLowNrvShakeOff, BossStinkBugActionFlyLow, ShakeOff);
    NEW_NERVE(BossStinkBugActionFlyLowNrvShakeOffWait, BossStinkBugActionFlyLow, ShakeOffWait);
    NEW_NERVE(BossStinkBugActionFlyLowNrvShakeOffEnd, BossStinkBugActionFlyLow, ShakeOffEnd);
    NEW_NERVE(BossStinkBugActionFlyLowNrvFall, BossStinkBugActionFlyLow, Fall);
    NEW_NERVE(BossStinkBugActionFlyLowNrvDamage, BossStinkBugActionFlyLow, Damage);
    NEW_NERVE(BossStinkBugActionFlyLowNrvDash, BossStinkBugActionFlyLow, Dash);
    NEW_NERVE(BossStinkBugActionFlyLowNrvTurnSign, BossStinkBugActionFlyLow, TurnSign);
    NEW_NERVE(BossStinkBugActionFlyLowNrvTurn, BossStinkBugActionFlyLow, Turn);
};  // namespace NrvBossStinkBugActionFlyLow

BossStinkBugActionFlyLow::BossStinkBugActionFlyLow(BossStinkBug* pStinkBug) : BossStinkBugActionBase("ボスカメムシ低空戦", pStinkBug), mHeightAngle(0.0f) {
    mFlySpeed = cNum_BossStinkBugActionFlyLow_f1;
    mHeightOffset = cNum_BossStinkBugActionFlyLow_f2;
    mLocalHeightOffset = -cNum_BossStinkBugActionFlyLow_f3;
    initNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFly));
}

void BossStinkBugActionFlyLow::appear() {
    MR::moveCoordToNearestPos(getHost(), getHost()->mPosition);

    if (MR::isRailGoingToEnd(getHost())) {
        MR::reverseRailDirection(getHost());
    }

    mHeightAngle = 0.0f;
    mHeightOffset = cNum_appear_f1;
    BossStinkBugActionBase::appear();
    setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvDash));
    MR::offBind(getHost());
    getHost()->validateCollisionFly();
    MR::startBck(getHost()->getBombLauncher(), "Open");
    MR::tryStartAllAnim(getHost()->getWingModel(), "Fly");
}

void BossStinkBugActionFlyLow::control() {
    if (!isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFall))) {
        MR::zeroVelocity(getHost());
    }
    updateSound();
    updateSoundFly();
}

bool BossStinkBugActionFlyLow::isValidFollowId(s32 id) const {
    return id == 1;
}

bool BossStinkBugActionFlyLow::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerHipDropFloor(msg) && isEnableHipDrop()) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
        MR::emitEffectHit(getHost(), MR::getSensorPos(pSender), "Hit");

        if (BossStinkBugFunction::isHipDropableSensor(getHost(), pReceiver)) {
            MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_2");
            setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFall));
            return true;
        }
    }

    return false;
}

bool BossStinkBugActionFlyLow::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (getHost()->isSensorBody(pReceiver)) {
        if (isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFly)) && MR::isMsgFloorTouch(msg)) {
            setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvShakeOffSign));
            return true;
        }
    } else if (isEnableAttack() && (MR::isMsgWallTouch(msg) || MR::isMsgCeilTouch(msg))) {
    }
    return false;
}

void BossStinkBugActionFlyLow::exeFly() {
    mHeightAngle += cNum_exeFly_f1;
    MR::repeatDegree(&mHeightAngle);
    mHeightOffset = cNum_exeFly_f2 + MR::sin(mHeightAngle) * cNum_exeFly_f3;
    throwBomb(60);
    updateFly();
    if (BossStinkBugFunction::isExistPlayerBack(getHost(), cNum_exeFly_f4)) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvTurnSign));
    }
}

void BossStinkBugActionFlyLow::exeShakeOffSign() {
    if (updateFlyShakeOffSign(120)) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvShakeOff));
    }
}

void BossStinkBugActionFlyLow::exeShakeOff() {
    if (updateFlyShakeOff()) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvShakeOffWait));
    }
}

void BossStinkBugActionFlyLow::exeShakeOffWait() {
    if (updateFlyShakeOffWait(30)) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvShakeOffEnd));
    }
}

void BossStinkBugActionFlyLow::exeShakeOffEnd() {
    if (updateFlyShakeOffEnd()) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFly));
    }
}

void BossStinkBugActionFlyLow::exeFall() {
    if (updateFall()) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvDamage));
    }
}

void BossStinkBugActionFlyLow::exeDamage() {
    if (updateFallDamage()) {
        kill();
    }
}

void BossStinkBugActionFlyLow::exeDash() {
    if (updateFlyDash(cNum_exeDash_f1)) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFly));
    }
}

void BossStinkBugActionFlyLow::exeTurnSign() {
    if (updateFlyTurnSign()) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvTurn));
    }
}

void BossStinkBugActionFlyLow::exeTurn() {
    if (updateFlyTurn()) {
        setNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFly));
    }
}

bool BossStinkBugActionFlyLow::isEnableHipDrop() const {
    return isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFly)) ||
           isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvShakeOffSign)) ||
           isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvShakeOff)) ||
           isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvShakeOffEnd)) ||
           isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvTurnSign)) ||
           isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvTurn));
}

bool BossStinkBugActionFlyLow::isEnableAttack() const {
    if (isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvFall)) ||
        isNerve(GET_NERVE(BossStinkBugActionFlyLow, BossStinkBugActionFlyLowNrvDamage))) {
        return false;
    } else {
        return true;
    }
}
