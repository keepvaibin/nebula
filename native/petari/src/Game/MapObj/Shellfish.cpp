#include "Game/MapObj/Shellfish.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/MapObj/BlueChip.hpp"
#include "Game/MapObj/Coin.hpp"
#include "Game/MapObj/YellowChip.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 170.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 260.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOpen_f1, "exeOpen.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_endBindItem_f1, "endBindItem.f1", 30.0f);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, sBodyHitOffset, {0.0f, 100.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sSandHitOffset, {0.0f, -100.0f, 200.0f});
    NEBULA_TUNABLE_RECORD(Vec, sCoinOffset, {0.0f, 50.0f, 30.0f});
    NEBULA_TUNABLE_RECORD(Vec, sChipOffset, {0.0f, 100.0f, 50.0f});
    NEBULA_TUNABLE_RECORD(Vec, sKinokoOneUpOffset, {0.0f, 70.0f, 30.0f});
    const s32 sStepToOpen = NebulaFrameClock::stepsOfMilliseconds(2500);
    NEBULA_TUNABLE(s32, sStepToSetPosItem, 100);
    NEBULA_TUNABLE(s32, sStepToCloseSignal, 170);
    const s32 sStepToClose = NebulaFrameClock::stepsOfMilliseconds(2500);
    NEBULA_TUNABLE(s32, sStepToHitSensorOn, 0);
    NEBULA_TUNABLE(s32, sStepToHitSensorOff, 15);
    NEBULA_TUNABLE(s32, sStepToApperItem, 40);
    NEBULA_TUNABLE(s32, sStepToDeadItem, 5);
    const s32 sStepToRecover = NebulaFrameClock::stepsOfSeconds(9);
    NEBULA_TUNABLE(s32, sShockToInterpole, 2);
    NEBULA_TUNABLE(f32, sHitPushPower, 40.0f);
};  // namespace

namespace NrvShellfish {
    NEW_NERVE(ShellfishNrvWait, Shellfish, Wait);
    NEW_NERVE(ShellfishNrvOpen, Shellfish, Open);
    NEW_NERVE(ShellfishNrvOpenWait, Shellfish, OpenWait);
    NEW_NERVE(ShellfishNrvCloseSignal, Shellfish, CloseSignal);
    NEW_NERVE(ShellfishNrvClose, Shellfish, Close);
    NEW_NERVE(ShellfishNrvShock, Shellfish, Shock);
    NEW_NERVE_ONEND(ShellfishNrvPointerOpen, Shellfish, PointerOpen, PointerOpen);
};  // namespace NrvShellfish

Shellfish::Shellfish(const char* pName)
    : LiveActor(pName), mScaleController(), mStarPointerState(), mItem(), mItemType(), mIsItemBound(), mLowCollision() {
}

void Shellfish::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Shellfish", nullptr, false);
    MR::connectToSceneMapObjStrongLight(this);
    initHitSensor(2);
    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f1, Vec3f(::sBodyHitOffset));
    MR::addHitSensorAtJointEnemyAttack(this, "core", "Lid", 8, cNum_init_f2, Vec3f(::sSandHitOffset));
    MR::invalidateHitSensor(this, "core");
    MR::initCollisionPartsAutoEqualScaleOne(this, "Hi", getSensor("body"), MR::getJointMtx(this, "Lid"));
    mLowCollision = MR::createCollisionPartsFromLiveActor(this, "Low", getSensor("body"), MR::CollisionScaleType_NotUsingScale);
    MR::validateCollisionParts(mLowCollision);
    MR::initLightCtrlNoDrawEnemy(this);
    MR::initShadowVolumeCylinder(this, cNum_init_f3);
    Vec3f front;
    MR::calcFrontVec(&front, this);
    Vec3f up;
    MR::calcUpVec(&up, this);
    Vec3f shadowPosition;
    shadowPosition.scaleAdd(cNum_init_f4, front, mPosition);
    shadowPosition.scaleAdd(cNum_init_f5, up, shadowPosition);
    MR::setShadowDropPosition(this, nullptr, shadowPosition);
    MR::offCalcShadow(this, nullptr);
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::initStarPointerTarget(this, cNum_init_f6, Vec3f(0.0f, cNum_init_f7, -cNum_init_f8));
    mScaleController = new AnimScaleController(nullptr);
    mScaleController->setParamTight();
    mStarPointerState = new WalkerStateBindStarPointer(this, mScaleController);
    initNerve(GET_NERVE(Shellfish, ShellfishNrvWait));
    initItem(rIter);
    MR::addToAttributeGroupSearchTurtle(this);
    makeActorAppeared();
}

void Shellfish::initAfterPlacement() {
    MR::onCalcShadowOneTime(this, nullptr);
}

void Shellfish::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    Vec3f scale;
    scale.mul(mScaleController->mScale, mScale);
    MR::setBaseScale(this, scale);
}

void Shellfish::control() {
    mScaleController->updateNerve();

    if ((isNerve(GET_NERVE(Shellfish, ShellfishNrvOpenWait)) || isNerve(GET_NERVE(Shellfish, ShellfishNrvCloseSignal))) &&
        mStarPointerState->tryStartPointBind()) {
        setNerve(GET_NERVE(Shellfish, ShellfishNrvPointerOpen));
    }
}

void Shellfish::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (MR::isStep(this, ::sStepToOpen)) {
        setNerve(GET_NERVE(Shellfish, ShellfishNrvOpen));
    }
}

void Shellfish::exeOpen() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Open");
        MR::startSound(this, "SE_OJ_SHELLFISH_OPEN");
        putItem();
    }

    if (MR::isStep(this, ::sStepToApperItem)) {
        MR::sendArbitraryMsg(ACTMES_ITEM_SHOW, mItem->getSensor(nullptr), getSensor("body"));
        startBindItem();
    }

    if (mIsItemBound && MR::isLessStep(this, ::sStepToSetPosItem) && MR::isGreaterStep(this, ::sStepToApperItem)) {
        f32 speed = cNum_exeOpen_f1;
        Vec3f up;
        MR::calcUpVec(&up, this);
        mItem->mPosition.scaleAdd(speed, up, mItem->mPosition);
    }

    if (mIsItemBound && MR::isStep(this, ::sStepToSetPosItem)) {
        endBindItem();
        setNerve(GET_NERVE(Shellfish, ShellfishNrvOpenWait));
    }
}

void Shellfish::exeOpenWait() {
    if (MR::isStep(this, ::sStepToCloseSignal)) {
        setNerve(GET_NERVE(Shellfish, ShellfishNrvCloseSignal));
    }
}

void Shellfish::exeCloseSignal() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "CloseSignal");
    }

    MR::startLevelSound(this, "SE_OJ_LV_SHELLFISH_SIGNAL");

    if (MR::isStep(this, ::sStepToClose)) {
        setNerve(GET_NERVE(Shellfish, ShellfishNrvClose));
    }
}

void Shellfish::exeClose() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Close");
        MR::startSound(this, "SE_OJ_SHELLFISH_CLOSE");
    }

    if (MR::isStep(this, ::sStepToHitSensorOn)) {
        MR::validateHitSensor(this, "core");
    }

    if (MR::isStep(this, ::sStepToHitSensorOff)) {
        MR::invalidateHitSensor(this, "core");
    }

    if (MR::isStep(this, ::sStepToDeadItem)) {
        MR::sendArbitraryMsg(ACTMES_ITEM_HIDE, mItem->getSensor(nullptr), getSensor("body"));
    }

    if (MR::isBckStopped(this)) {
        MR::validateCollisionParts(this);
        setNerve(GET_NERVE(Shellfish, ShellfishNrvWait));
    }
}

void Shellfish::exeShock() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensor(this, "core");
        MR::sendArbitraryMsg(ACTMES_ITEM_SHOW, mItem->getSensor(nullptr), getSensor("body"));
    }

    if (MR::isStep(this, ::sStepToRecover)) {
        setNerve(GET_NERVE(Shellfish, ShellfishNrvCloseSignal));
    }
}

void Shellfish::exePointerOpen() {
    MR::updateActorStateAndNextNerve(this, mStarPointerState, GET_NERVE(Shellfish, ShellfishNrvOpenWait));
}

void Shellfish::endPointerOpen() {
    mStarPointerState->kill();
}

void Shellfish::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pSender) && MR::isSensorPlayer(pReceiver)) {
        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
        MtxPtr mtx = getBaseMtx();
        Vec3f direction(mtx[0][2], mtx[1][2], mtx[2][2]);

        if (MR::sendMsgEnemyAttackStrongToDir(pReceiver, pSender, direction)) {
            MR::invalidateCollisionParts(this);
        }

        Vec3f push;
        push.scale(::sHitPushPower, direction);
        MR::pushPlayer(push);
    }
}

bool Shellfish::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    if (MR::isMsgJetTurtleAttack(msg) || MR::isMsgStarPieceReflect(msg)) {
        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);

        if (isNerve(GET_NERVE(Shellfish, ShellfishNrvWait))) {
            MR::startBckWithInterpole(this, "CloseDamage", ::sShockToInterpole);
        } else {
            MR::startBckWithInterpole(this, "OpenDamage", ::sShockToInterpole);
        }

        if (mIsItemBound) {
            endBindItem();
        }

        setNerve(GET_NERVE(Shellfish, ShellfishNrvShock));
        return true;
    }

    return false;
}

void Shellfish::initItem(const TableIter& rIter) {
    const char* pObjectName;
    MR::getObjectName(&pObjectName, rIter);

    if (MR::isEqualStringCase(pObjectName, "ShellfishCoin")) {
        mItemType = 0;
    } else if (MR::isEqualStringCase(pObjectName, "ShellfishYellowChip")) {
        mItemType = 1;
    } else if (MR::isEqualStringCase(pObjectName, "ShellfishBlueChip")) {
        mItemType = 2;
    } else if (MR::isEqualStringCase(pObjectName, "ShellfishKinokoOneUp")) {
        mItemType = 3;
    }

    if (mItemType == 0) {
        initCoin(rIter);
    } else if (mItemType == 1) {
        initYellowChip(rIter);
    } else if (mItemType == 2) {
        initBlueChip(rIter);
    } else if (mItemType == 3) {
        initKinokoOneUp(rIter);
    }

    putItem();
    mItem->makeActorAppeared();
    MR::sendArbitraryMsg(ACTMES_ITEM_HIDE, mItem->getSensor(nullptr), getSensor("body"));
}

void Shellfish::initCoin(const TableIter&) {
    mItem = static_cast< Coin* >(MR::createCoin(this, "コイン"));
    mItem->initWithoutIter();
}

void Shellfish::initYellowChip(const TableIter& rIter) {
    s32 groupId;
    MR::getPlacementArg0WithInit(rIter, &groupId);
    YellowChip* chip = new YellowChip("イエローチップ");
    chip->setHost(this);
    chip->setGroupID(groupId);
    mItem = chip;
    chip->initWithoutIter();
}

void Shellfish::initBlueChip(const TableIter& rIter) {
    s32 groupId;
    MR::getPlacementArg0WithInit(rIter, &groupId);
    BlueChip* chip = new BlueChip("ブルーチップ");
    chip->setHost(this);
    chip->setGroupID(groupId);
    mItem = chip;
    chip->initWithoutIter();
}

void Shellfish::initKinokoOneUp(const TableIter&) {
    mItem = new BenefitItemOneUp("１ＵＰキノコ");
    mItem->initWithoutIter();
}

void Shellfish::startBindItem() {
    mIsItemBound = MR::sendArbitraryMsg(ACTMES_ITEM_START_MOVE, mItem->getSensor(nullptr), getSensor("body"));
}

void Shellfish::endBindItem() {
    putItem();
    Vec3f up;
    MR::calcUpVec(&up, this);
    mItem->mPosition.scaleAdd(cNum_endBindItem_f1, up, mItem->mPosition);
    MR::sendArbitraryMsg(ACTMES_ITEM_END_MOVE, mItem->getSensor(nullptr), getSensor("body"));
    mIsItemBound = false;
}

void Shellfish::putItem() {
    const Vec* pOffset = nullptr;

    if (mItemType == 0) {
        pOffset = &::sCoinOffset;
    } else if (mItemType == 1) {
        pOffset = &::sChipOffset;
    } else if (mItemType == 2) {
        pOffset = &::sChipOffset;
    } else if (mItemType == 3) {
        pOffset = &::sKinokoOneUpOffset;
    }

    NebulaMtxMultVec(getBaseMtx(), pOffset, &mItem->mPosition);
}
