#include "Game/MapObj/PhantomShipBridge.hpp"
#include "Game/Animation/BckCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 2000.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeMoveA_1, "exeMoveA.1", 55);
NEBULA_TUNABLE_NAMED(s32, cStep_exeMoveA_2, "exeMoveA.2", 55);
NEBULA_TUNABLE_NAMED(s32, cStep_exeMoveB_1, "exeMoveB.1", 55);
NEBULA_TUNABLE_NAMED(s32, cStep_exeMoveB_2, "exeMoveB.2", 55);

namespace NrvPhantomShipBridge {
    NEW_NERVE(HostTypeWait, PhantomShipBridge, Wait);
    NEW_NERVE(HostTypeMoveA, PhantomShipBridge, MoveA);
    NEW_NERVE(HostTypeMoveB, PhantomShipBridge, MoveB);
};  // namespace NrvPhantomShipBridge

PhantomShipBridge::PhantomShipBridge(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< PhantomShipBridge >);
    mMoveCollision = nullptr;
    mIsNutShipBridge = 0;
}

void PhantomShipBridge::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* objName;
    MR::getObjectName(&objName, rIter);
    initModelManagerWithAnm(objName, nullptr, false);
    if (MR::isEqualString(objName, "NutShipFleetBridge")) {
        mIsNutShipBridge = 1;
    }

    MR::connectToSceneCollisionMapObj(this);
    initNerve(GET_NERVE(PhantomShipBridge, HostTypeWait));
    initEffectKeeper(0, nullptr);
    initHitSensor(2);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, objName, getSensor("body"), nullptr);
    MR::addMessageSensorMapObjMoveCollision(this, "move");
    mMoveCollision = MR::createCollisionPartsFromLiveActor(this, "Move", getSensor("move"), MR::getJointMtx(this, "Move"), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mMoveCollision);
    initSound(4, false);
    MR::setClippingTypeSphere(this, cNum_init_f1);
    makeActorAppeared();

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor(this, &PhantomShipBridge::startMoveB));
    }

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &PhantomShipBridge::startMoveA));
    } else {
        setStateMoveA();
    }
}

void PhantomShipBridge::calcAnim() {
    LiveActor::calcAnim();
    mMoveCollision->setMtx();
}

void PhantomShipBridge::startMoveA() {
    setNerve(GET_NERVE(PhantomShipBridge, HostTypeMoveA));
}

void PhantomShipBridge::startMoveB() {
    setNerve(GET_NERVE(PhantomShipBridge, HostTypeMoveB));
}

void PhantomShipBridge::setStateMoveA() {
    MR::startBck(this, "MoveA");
    MR::setBckFrameAndStop(this, MR::getBckCtrl(this)->getEnd());
    MR::calcAnimDirect(this);
    mMoveCollision->forceResetAllMtxAndSetUpdateMtxOneTime();
    setNerve(GET_NERVE(PhantomShipBridge, HostTypeWait));
}

void PhantomShipBridge::exeMoveA() {
    if (MR::isFirstStep(this)) {
        if (!MR::isPlayerDead()) {
            MR::startSystemSE("SE_SY_READ_RIDDLE_S");
        }

        MR::startBck(this, "MoveA");
    }

    if (MR::isLessStep(this, cStep_exeMoveA_1)) {
        if (mIsNutShipBridge == 1) {
            MR::startLevelSound(this, "SE_OJ_LV_NUT_SHIP_BRIDGE_MV");
        } else {
            MR::startLevelSound(this, "SE_OJ_LV_FALL_BRIDGE_FALL");
        }
    }

    if (MR::isStep(this, cStep_exeMoveA_2)) {
        if (mIsNutShipBridge == 1) {
            MR::startSound(this, "SE_OJ_NUT_SHIP_BRIDGE_ED");
        } else {
            MR::startSound(this, "SE_OJ_FALL_BRIDGE_LAND");
        }

        MR::shakeCameraNormal();
    }
}

void PhantomShipBridge::exeMoveB() {
    if (MR::isFirstStep(this)) {
        if (mIsNutShipBridge == 1) {
            MR::startSound(this, "SE_OJ_NUT_SHIP_BRIDGE_ST");
        } else {
            MR::startSound(this, "SE_OJ_PTM_SHIP_BRIDGE_FALL");
        }

        MR::startBck(this, "MoveB");
    }

    if (MR::isLessStep(this, cStep_exeMoveB_1)) {
        if (mIsNutShipBridge == 1) {
            MR::startLevelSound(this, "SE_OJ_LV_NUT_SHIP_BRIDGE_MV");
        } else {
            MR::startLevelSound(this, "SE_OJ_LV_FALL_BRIDGE_FALL");
        }
    }

    if (MR::isStep(this, cStep_exeMoveB_2)) {
        if (mIsNutShipBridge == 1) {
            MR::startSound(this, "SE_OJ_NUT_SHIP_BRIDGE_ED");
        } else {
            MR::startSound(this, "SE_OJ_FALL_BRIDGE_LAND");
        }

        MR::shakeCameraNormal();
    }
}

void PhantomShipBridge::exeWait() {
}
