#include "Game/MapObj/PunchBox.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvPunchBox {
    NEW_NERVE(PunchBoxNrvWait, PunchBox, Wait);
    NEW_NERVE(PunchBoxNrvHit, PunchBox, Hit);
};  // namespace NrvPunchBox

PunchBox::PunchBox(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< PunchBox >);
}

void PunchBox::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("PunchBox", nullptr, false);

    MR::connectToSceneMapObjStrongLight(this);

    initSound(2, false);
    initNerve(GET_NERVE(PunchBox, PunchBoxNrvWait));
    initEffectKeeper(3, nullptr);

    f32 radius = mScale.x * 90.0f;
    initHitSensor(1);
    Vec3f offset(0.0f, radius, 0.0f);
    MR::addHitSensorMapObj(this, "body", 8, radius, offset);

    MR::initCollisionParts(this, "PunchBox", getSensor("body"), nullptr);

    s32 coinCount = 1;
    s32 hasShadow = 1;

    if (rIter.isValid()) {
        MR::getPlacementArg0NoInit(rIter, &coinCount);
        MR::getPlacementArg1NoInit(rIter, &hasShadow);
    }

    mCoinCount = coinCount;
    mHitPoint = 1;
    mHitCoolDown = 0;
    mShadowStep = MR::getRandom(0, 31);
    mFloorTouchTimer = 0;

    setNerve(GET_NERVE(PunchBox, PunchBoxNrvWait));

    MR::validateClipping(this);
    MR::setGroupClipping(this, rIter, 128);

    mHasShadow = hasShadow != 0;

    MR::declareCoin(this, mCoinCount);
    MR::declareStarPiece(this, 1);

    appear();

    if (mHasShadow) {
        MR::initShadowVolumeBox(this, Vec3f(150.0f, 150.0f, 150.0f));
        MR::setShadowVolumeStartDropOffset(this, nullptr, 77.0f);
        MR::setShadowVolumeEndDropOffset(this, nullptr, 150.0f);
        MR::onShadowVolumeCutDropLength(this, nullptr);
        MR::excludeCalcShadowToMyCollision(this, nullptr);
    }

    MR::addToAttributeGroupSearchTurtle(this);

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &PunchBox::kill));
    }

    getSensor("body")->invalidate();
}

void PunchBox::initAfterPlacement() {
    if (mHasShadow) {
        MR::onCalcGravity(this);
        MR::calcGravity(this);

        MR::onCalcShadowOneTime(this, nullptr);
    }
}

void PunchBox::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (pSender != getSensor("eye")) {
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    Vec3f distance(pReceiver->mPosition - pSender->mPosition);

    if (distance.length() < 410.0f) {
        mIsPlayerNear = true;
    }
}

void PunchBox::control() {
    if (mHitCoolDown == 0) {
        mShadowStep++;
    }

    if (mFloorTouchTimer != 0) {
        mFloorTouchTimer--;
    }
}

void PunchBox::calcAndSetBaseMtx() {
    if (mHasShadow && (mShadowStep & 0x7F) == 0x7F) {
        MR::onCalcShadowOneTime(this, nullptr);
    }

    LiveActor::calcAndSetBaseMtx();
}

void PunchBox::exeWait() {
    if (mHitCoolDown != 0 && --mHitCoolDown == 0) {
        mHitPoint = 1;
    }

    if (mHitPoint == 0) {
        setNerve(GET_NERVE(PunchBox, PunchBoxNrvHit));
    }

    mIsPlayerNear = false;
}

void PunchBox::exeHit() {
    if (getNerveStep() == 0) {
        getSensor("body")->invalidate();
    }

    if (getNerveStep() == 5) {
        MR::hideModel(this);
    }

    if (getNerveStep() == 15) {
        if (mCoinCount != 0) {
            MR::appearCoinPop(this, mPosition, mCoinCount);
        }

        makeActorDead();
    }
}

void PunchBox::kill() {
    if (MR::isDead(this)) {
        return;
    }

    MR::appearStarPiece(this, mPosition, 1, 10.0f, 40.0f, false);

    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");

    LiveActor::kill();
}

void PunchBox::doHit(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (mHitPoint != 0) {
        mHitPoint--;
    }

    if (mHitPoint == 0) {
        MR::invalidateCollisionParts(this);
    }

    mHitCoolDown = 30;

    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        MR::emitEffect(this, "BreakWater");
        MR::startSound(this, "SE_OJ_PUNCH_BOX_BREAK_W");
    } else {
        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_OJ_PUNCH_BOX_BREAK");
    }
}

bool PunchBox::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(PunchBox, PunchBoxNrvHit))) {
        return false;
    }

    if (MR::isMsgPlayerUpperPunch(msg)) {
        if (mHitPoint != 0) {
            doHit(pSender, pReceiver);
            pSender->receiveMessage(ACTMES_REFLECT_V, pReceiver);
        }

        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgPlayerHipDropFloor(msg)) {
        if (mFloorTouchTimer != 0) {
            doHit(pSender, pReceiver);
            return true;
        }

        return true;
    } else if (msg == ACTMES_JET_TURTLE_ATTACK) {
        doHit(pSender, pReceiver);
        return true;
    } else if (MR::isMsgPlayerHitAll(msg) && !MR::isOnPlayer(getSensor("body"))) {
        if (mHitPoint != 0) {
            doHit(pSender, pReceiver);
        }

        return true;
    }

    return false;
}

bool PunchBox::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (mHitPoint != 0) {
        doHit(pSender, pReceiver);
        return true;
    }

    return false;
}

bool PunchBox::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_IS_PUNCH_ENABLE) {
        if (mIsPlayerNear && mHitPoint != 0) {
            return true;
        }

        return false;
    }

    if (msg == ACTMES_IS_BROKEN) {
        if (mIsPlayerNear && mHitPoint != 0) {
            return false;
        }

        return true;
    }

    if (msg == ACTMES_IS_RUSH_ENABLE && mIsPlayerNear && mHitPoint != 0) {
        return true;
    }

    if (msg == ACTMES_FLOOR_TOUCH) {
        mFloorTouchTimer = 2;
    }

    if (msg == ACTMES_IS_OPD_SITE_IN && mHitPoint != 0) {
        return true;
    }

    if ((msg == ACTMES_OPD_SHOOT || msg == ACTMES_BALL_DASH_WALL || msg == ACTMES_BALL_DASH_GROUND) && !MR::isSensorEye(pReceiver) && mHitPoint != 0) {
        doHit(pSender, pReceiver);
        return true;
    }

    if (mHitPoint != 0 && (msg == ACTMES_TURNIP_ATTACK || msg == ACTMES_PAKKUN_BALL_ATTACK)) {
        doHit(pSender, pReceiver);
        return true;
    }

    return false;
}
