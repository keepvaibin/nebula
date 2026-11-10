#include "Game/MapObj/DeadLeaves.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpin_f1, "exeSpin.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpin_f2, "exeSpin.f2", 40.0f);

namespace NrvDeadLeaves {
    NEW_NERVE(DeadLeavesNrvWait, DeadLeaves, Wait);
    NEW_NERVE(DeadLeavesNrvSpin, DeadLeaves, Spin);
};  // namespace NrvDeadLeaves

DeadLeaves::DeadLeaves(const char* pName) : MapObjActor(pName), mItemType(-1) {
}

void DeadLeaves::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupHitSensor();
    info.setupHitSensorParam(8, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    info.setupNerve(GET_NERVE(DeadLeaves, DeadLeavesNrvWait));
    initialize(rIter, info);
    MR::getPlacementArg0NoInit(rIter, &mItemType);

    if (mItemType == 0) {
        MR::declareCoin(this, 1);
    } else if (mItemType == 1) {
        MR::declareStarPiece(this, 3);
    }
}

void DeadLeaves::exeWait() {
}

void DeadLeaves::exeSpin() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "Spin");
        MR::startSound(this, "SE_OJ_LEAVES_SWING");

        if (mItemType == 0) {
            MR::appearCoinPop(this, mPosition, 1);
        } else if (mItemType == 1) {
            if (MR::appearStarPiece(this, mPosition, 3, cNum_exeSpin_f1, cNum_exeSpin_f2, false)) {
                MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
            }
        }
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(DeadLeaves, DeadLeavesNrvWait));
    }
}

bool DeadLeaves::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        setNerve(GET_NERVE(DeadLeaves, DeadLeavesNrvSpin));
        return true;
    }

    return false;
}
