#include "Game/MapObj/PomponPlant.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_tryGenItem_f1, "tryGenItem.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryGenItem_f2, "tryGenItem.f2", 40.0f);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, sSensorOffset, {55.0f, 170.0f, -100.0f});
    NEBULA_TUNABLE(f32, sSensorRadius, 70.0f);
};  // namespace

namespace NrvPomponPlant {
    NEW_NERVE(HostTypeWait, PomponPlant, Wait);
    NEW_NERVE(HostTypeSwingPointer, PomponPlant, SwingPointer);
    NEW_NERVE(HostTypeSwingPlayer, PomponPlant, SwingPlayer);
};  // namespace NrvPomponPlant

PomponPlant::PomponPlant(const char* pName) : LiveActor(pName), mItemType(-1), mItemCount(1), mCanGenItem(true) {
    lumacore::setClass(this, lumacore::kClassId< PomponPlant >);
}

void PomponPlant::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* modelName = nullptr;
    MR::getObjectName(&modelName, rIter);
    initModelManagerWithAnm(modelName, nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, ::sSensorRadius, Vec3f(::sSensorOffset));
    MR::initStarPointerTarget(this, ::sSensorRadius, Vec3f(::sSensorOffset));
    initEffectKeeper(0, nullptr);
    initSound(2, false);
    MR::getPlacementArg0NoInit(rIter, &mItemType);
    MR::getPlacementArg1NoInit(rIter, &mItemCount);

    if (mItemType == -1) {
        MR::declareCoin(this, mItemCount);
    } else if (mItemType == 0) {
        MR::declareStarPiece(this, mItemCount);
    }

    initNerve(GET_NERVE(PomponPlant, HostTypeWait));
    makeActorAppeared();
}

void PomponPlant::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && !isNerve(GET_NERVE(PomponPlant, HostTypeSwingPlayer))) {
        if (MR::sendMsgTouchJump(pReceiver, pSender)) {
            setNerve(GET_NERVE(PomponPlant, HostTypeSwingPlayer));
        }
    }
}

bool PomponPlant::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceReflect(msg)) {
        setNerve(GET_NERVE(PomponPlant, HostTypeSwingPlayer));
        return true;
    }

    return false;
}

bool PomponPlant::tryGenItem() {
    if (!mCanGenItem) {
        return false;
    }

    if (mItemType == 0) {
        MR::appearStarPiece(this, getSensor("body")->mPosition, mItemCount, cNum_tryGenItem_f1, cNum_tryGenItem_f2, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    } else {
        MR::appearCoinPop(this, getSensor("body")->mPosition, mItemCount);
    }

    mCanGenItem = false;
    return true;
}

void PomponPlant::exeWait() {
    if (MR::tryStarPointerCheckWithoutRumble(this, false)) {
        setNerve(GET_NERVE(PomponPlant, HostTypeSwingPointer));
    }
}

void PomponPlant::exeSwingPointer() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_POMPON_PLANT_DPD_HIT");
        MR::startBck(this, "DpdHit");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PomponPlant, HostTypeWait));
    }
}

void PomponPlant::exeSwingPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_POMPON_PLANT_HIT");
        MR::startBck(this, "Hit");
        tryGenItem();
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PomponPlant, HostTypeWait));
    }
}
