#include "Game/MapObj/Banekiti.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initStarPointer_f1, "initStarPointer.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 100.0f);

namespace NrvBanekiti {
    NEW_NERVE(BanekitiNrvWait, Banekiti, Wait);
    NEW_NERVE(BanekitiNrvRepel, Banekiti, Repel);
    NEW_NERVE_ONEND(BanekitiNrvPointerSwoon, Banekiti, PointerSwoon, PointerSwoon);
};  // namespace NrvBanekiti

Banekiti::Banekiti(const char* pName) : LiveActor(pName), mAnimScaleCtrl(), mBindStarPointer(), mRailMover() {
    lumacore::setClass(this, lumacore::kClassId< Banekiti >);
}

inline void Banekiti::initStarPointer() {
    Vec3f offset;
    offset.set(0.0f);
    MR::initStarPointerTarget(this, cNum_initStarPointer_f1, offset);
}

void Banekiti::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Banekiti", nullptr, false);
    MR::connectToSceneMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initHitSensor(3);
    MR::addHitSensorMapObjSimple(this, "body", 4, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorMapObjSimple(this, "right", 4, cNum_init_f2, Vec3f(-cNum_init_f3, 0.0f, 0.0f));
    MR::addHitSensorMapObjSimple(this, "left", 4, cNum_init_f4, Vec3f(cNum_init_f5, 0.0f, 0.0f));
    initEffectKeeper(1, nullptr);
    initSound(4, false);
    initRailRider(rIter);
    mRailMover = new MapPartsRailMover(this);
    mRailMover->init(rIter);
    mRailMover->start();
    initStarPointer();
    mAnimScaleCtrl = new AnimScaleController(nullptr);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mAnimScaleCtrl);
    initNerve(GET_NERVE(Banekiti, BanekitiNrvWait));
    makeActorAppeared();
}

void Banekiti::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    MR::startLevelSound(this, "SE_OJ_LV_BANEKITI_MOVE");
}

void Banekiti::exeRepel() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::shakeCameraWeak();
        MR::startBck(this, "Repel");
        MR::startSound(this, "SE_OJ_BANEKITI_REPEL");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Banekiti, BanekitiNrvWait));
    }
}

void Banekiti::exePointerSwoon() {
    if (MR::isFirstStep(this)) {
        mRailMover->mIsActive = false;
    }

    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(Banekiti, BanekitiNrvWait));
}

void Banekiti::endPointerSwoon() {
    mRailMover->mIsActive = true;
    mBindStarPointer->kill();
}

void Banekiti::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    MR::setBaseScale(this, mAnimScaleCtrl->mScale * mScale);
}

void Banekiti::control() {
    mAnimScaleCtrl->updateNerve();
    mRailMover->movement();

    if (mRailMover->isWorking()) {
        mPosition.set(mRailMover->mStartPos);
    }

    if (!isNerve(GET_NERVE(Banekiti, BanekitiNrvPointerSwoon)) && mBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(Banekiti, BanekitiNrvPointerSwoon));
    }
}

bool Banekiti::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceAttack(msg)) {
        mAnimScaleCtrl->startHitReaction();
        return true;
    }

    return false;
}

bool Banekiti::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (!MR::isSensorPlayer(pSender)) {
        return false;
    }

    if (isNerve(GET_NERVE(Banekiti, BanekitiNrvRepel))) {
        return false;
    }

    if (msg == ACTMES_TERESA_PLAYER_TOUCH) {
        setNerve(GET_NERVE(Banekiti, BanekitiNrvRepel));

        return true;
    }

    return false;
}
