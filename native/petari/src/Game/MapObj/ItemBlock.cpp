#include "Game/MapObj/ItemBlock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 77.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initBlock_f1, "initBlock.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initBlock_f2, "initBlock.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f1, "receiveMsgPlayerAttack.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearItem_f1, "exeAppearItem.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearItemSplash_f1, "exeAppearItemSplash.f1", 40.0f);

namespace {
    NEBULA_TUNABLE(s32, cAppearItemStep, 1);
    NEBULA_TUNABLE(f32, cAppearItemOffsetY, 100.0f);
    static const s32 cTimerLimit = NebulaFrameClock::stepsOfSeconds(4);
    NEBULA_TUNABLE(s32, hNumTimerLastPiece, 11);
    NEBULA_TUNABLE(f32, hLastBonusSplashRangeH, 10.0f);
    NEBULA_TUNABLE(f32, hPointLightOffsetY, 100.0f);
    NEBULA_TUNABLE(f32, hBrightness, 0.998646f);
};  // namespace

namespace NrvItemBlock {
    NEW_NERVE(ItemBlockNrvStandby, ItemBlock, Standby);
    NEW_NERVE(ItemBlockNrvWait, ItemBlock, Wait);
    NEW_NERVE(ItemBlockNrvAppearItem, ItemBlock, AppearItem);
    NEW_NERVE(ItemBlockNrvAppearItemSplash, ItemBlock, AppearItemSplash);
};  // namespace NrvItemBlock

namespace {
    const Color8 hPointLight(0xFF, 0xE6, 0, 0xFF);
};  // namespace

ItemBlock::ItemBlock(const char* pName) : LiveActor(pName) {
    mKind = 0;
    mTimer = 0;
    mItemCount = 8;
    mCalcShadow = false;
    mNoCalcAnim = false;
}

void ItemBlock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    checkKind(rIter);
    initBlock();
    MR::connectToSceneMapObjStrongLight(this);
    MR::useStageSwitchSleep(this, rIter);
    initEffectKeeper(1, "CoinBlock");
    initSound(4, false);
    initNerve(GET_NERVE(ItemBlock, ItemBlockNrvStandby));
    s32 arg = -1;
    MR::getPlacementArg4NoInit(rIter, &arg);
    MR::initShadowVolumeBox(this, Vec3f(cNum_init_f1, cNum_init_f2, cNum_init_f3));
    MR::setShadowVolumeStartDropOffset(this, 0, cNum_init_f4);

    if (arg != -1) {
        MR::setShadowDropLength(this, nullptr, arg);
    }

    MR::onShadowVolumeCutDropLength(this, nullptr);
    MR::excludeCalcShadowToMyCollision(this, nullptr);

    if (mCalcShadow) {
        MR::onCalcShadow(this, nullptr);
    } else {
        MR::onCalcShadowOneTime(this, nullptr);
    }

    if (MR::isZeroGravity(this)) {
        Vec3f upVec;
        MR::calcUpVec(&upVec, this);
        MR::setShadowDropDirection(this, 0, -upVec);
    } else {
        MR::calcGravity(this);
    }

    MR::offCalcGravity(this);
    MR::setGroupClipping(this, rIter, 16);
    MR::validateClipping(this);

    appear();
}

void ItemBlock::startClipped() {
    LiveActor::startClipped();
    MR::forceDeleteEffect(this, "Glow");
}

void ItemBlock::endClipped() {
    LiveActor::endClipped();
    MR::emitEffect(this, "Glow");
}

void ItemBlock::checkKind(const TableIter& rIter) {
    const char* name = nullptr;
    MR::getObjectName(&name, rIter);

    if (MR::isEqualString(name, "ItemBlockSwitch")) {
        MR::needStageSwitchWriteDead(this, rIter);
        mKind = 5;
    } else if (MR::isEqualString(name, "TimerCoinBlock")) {
        mKind = 1;
        MR::declareCoin(this, 10);
    } else if (MR::isEqualString(name, "SplashCoinBlock")) {
        mKind = 2;
        MR::declareCoin(this, mItemCount);
    } else if (MR::isEqualString(name, "TimerPieceBlock")) {
        mKind = 3;
        MR::declareStarPiece(this, 20);
    } else if (MR::isEqualString(name, "SplashPieceBlock")) {
        mKind = 4;
        MR::declareStarPiece(this, mItemCount);
    }

    s32 arg = -1;

    if (mKind == 2 || mKind == 4) {
        arg = -1;
        MR::getPlacementArg0NoInit(rIter, &arg);

        if (arg >= 0) {
            mItemCount = arg;
        } else {
            mItemCount = 8;
        }
    }

    if (mKind == 1 || mKind == 3) {
        MR::getPlacementArg1NoInit(rIter, &arg);

        if (arg == -1) {
            mTimer = ::cTimerLimit;
        } else {
            mTimer = arg;
        }
    }

    arg = -1;
    MR::getPlacementArg2NoInit(rIter, &arg);
    mCalcShadow = !(arg - 1);
}

void ItemBlock::initBlock() {
    initHitSensor(1);
    switch (mKind) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        initModelManagerWithAnm("CoinBlock", nullptr, false);
        Pos3f mtx;
        MR::addHitSensorMtxMapObj(this, "body", 8, cNum_initBlock_f1, MR::getJointMtx(this, "CoinBlock"), Vec3f(0.0f, cNum_initBlock_f2, 0.0f));
        MR::initCollisionPartsAutoEqualScale(this, "CoinBlock", getSensor("body"), MR::getJointMtx(this, "CoinBlock"));
    }
}

void ItemBlock::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(ItemBlock, ItemBlockNrvStandby));

    switch (mKind) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        MR::startBrk(this, "CoinBlock");
    }
}

void ItemBlock::kill() {
    MR::startSound(this, "SE_EM_EXPLODE_S");
    MR::emitEffect(this, "Break");
    LiveActor::kill();
}

void ItemBlock::control() {
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);
    MR::requestPointLight(this, mPosition + upVec * ::hPointLightOffsetY, ::hPointLight, ::hBrightness, -1);
}

bool ItemBlock::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerUpperPunch(msg) && MR::isPlayerExistDown(this, 0.0f, cNum_receiveMsgPlayerAttack_f1) && tryStartJumpPunch()) {
        MR::sendArbitraryMsg(ACTMES_REFLECT_V, pSender, pReceiver);
        return true;
    }

    return false;
}

bool ItemBlock::tryStartJumpPunch() {
    if (isNerve(GET_NERVE(ItemBlock, ItemBlockNrvStandby))) {
        if (mKind == 1 || mKind == 3) {
            MR::startBck(this, "Appear");
            calcAnim();
            mNoCalcAnim = true;
            setNerve(GET_NERVE(ItemBlock, ItemBlockNrvAppearItem));
        } else if (mKind == 2 || mKind == 4 || mKind == 5) {
            setNerve(GET_NERVE(ItemBlock, ItemBlockNrvAppearItemSplash));
        }

        return true;
    }

    if (isNerve(GET_NERVE(ItemBlock, ItemBlockNrvWait))) {
        if (mKind == 1 || mKind == 3) {
            if (mKind == 1) {
                if (MR::getDeclareRemnantCoinCount(this) == 1) {
                    mTimer = 0;
                }
            } else {
                if (MR::getDeclareRemnantStarPieceCountNotExist(this) - ::hNumTimerLastPiece == 0) {
                    mTimer = 0;
                }
            }

            MR::startBck(this, "Appear");
            calcAnim();
            mNoCalcAnim = true;
            setNerve(GET_NERVE(ItemBlock, ItemBlockNrvAppearItem));

            return true;
        }
    }

    return false;
}

void ItemBlock::exeStandby() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }
}

void ItemBlock::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::invalidateClipping(this);
    }

    decTimer();
}

void ItemBlock::exeAppearItem() {
    if (MR::isFirstStep(this)) {
        if (!mTimer) {
            MR::forceDeleteEffect(this, "Glow");
            MR::startBck(this, "Bomb");
        }

        MR::invalidateClipping(this);
    }

    if (MR::isStep(this, ::cAppearItemStep)) {
        Vec3f gravVec;
        Vec3f position = mPosition;
        MR::calcGravityVector(this, &gravVec, nullptr, 0);
        position -= gravVec * ::cAppearItemOffsetY;

        switch (mKind) {
        case 1:
            MR::hopCoin(this, position, -gravVec);
            break;

        case 3:
            if (!mTimer && MR::getDeclareRemnantStarPieceCountNotExist(this) - ::hNumTimerLastPiece == 0) {
                MR::appearStarPieceToDirection(this, position, -gravVec, 11, ::hLastBonusSplashRangeH, cNum_exeAppearItem_f1, false);

            } else {
                MR::hopStarPiece(this, position, -gravVec);
            }

            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
            break;
        case 2:
        default:
            MR::startSystemSE("SE_SY_ITEM_APPEAR");
            break;
        }
    }

    decTimer();

    if (!MR::isBckStopped(this)) {
        return;
    }

    if (!mTimer) {
        kill();
    } else {
        setNerve(GET_NERVE(ItemBlock, ItemBlockNrvWait));
    }
}

void ItemBlock::exeAppearItemSplash() {
    if (MR::isFirstStep(this)) {
        MR::forceDeleteEffect(this, "Glow");
        MR::startBck(this, "Bomb");
    }

    if (!MR::isBckStopped(this)) {
        return;
    }

    if (mKind == 2) {
        if (mItemCount == 1) {
            MR::appearCoinPop(this, mPosition, 1);
        } else {
            MR::appearCoinCircle(this, mPosition, mItemCount);
        }
    } else if (mKind == 4) {
        MR::appearStarPiece(this, mPosition, mItemCount, ::hLastBonusSplashRangeH, cNum_exeAppearItemSplash_f1, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    } else if (mKind == 5) {
        MR::onSwitchDead(this);
    }

    mTimer = 0;
    kill();
}

void ItemBlock::decTimer() {
    --mTimer;
    if (mTimer > 0) {
        return;
    }
    mTimer = 0;
}

void ItemBlock::calcAnim() {
    if (!mNoCalcAnim) {
        LiveActor::calcAnim();
    }
    mNoCalcAnim = false;
}
