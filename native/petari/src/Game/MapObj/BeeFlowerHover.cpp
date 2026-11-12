#include "Game/MapObj/BeeFlowerHover.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/MapObj/MapPartsRailPosture.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sDefaultRotateSpeed = 50.0f;
    static const s32 sStepToRecoverStart = NebulaFrameClock::stepsOfSeconds(1);
};  // namespace

namespace NrvBeeFlowerHover {
    NEW_NERVE(BeeFlowerHoverNrvWait, BeeFlowerHover, Wait);
    NEW_NERVE(BeeFlowerHoverNrvSoftTouch, BeeFlowerHover, SoftTouch);
    NEW_NERVE(BeeFlowerHoverNrvSoftTouchWait, BeeFlowerHover, SoftTouchWait);
    NEW_NERVE(BeeFlowerHoverNrvHardTouch, BeeFlowerHover, HardTouch);
    NEW_NERVE(BeeFlowerHoverNrvRecover, BeeFlowerHover, Recover);
};  // namespace NrvBeeFlowerHover

BeeFlowerHover::BeeFlowerHover(const char* pName)
    : LiveActor(pName), mRotateSpeed(::sDefaultRotateSpeed), mLodCtrlPlanet(), mRailMover(), mRailPosture(), mClipCenter(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< BeeFlowerHover >);
    mRotateMtx.identity();
}

void BeeFlowerHover::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("BeeFlowerHover", 0, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionPartsAutoEqualScale(this, "BeeFlowerHover", getSensor(nullptr), 0);
    initEffectKeeper(0, nullptr);
    initSound(4, false);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        mRailMover = new MapPartsRailMover(this);
        mRailMover->init(rIter);
        mRailMover->start();

        mRailPosture = new MapPartsRailPosture(this);
        mRailPosture->mMovePosture = MapPartsRailPosture::POSTURE_RAIL_DIR_FRONT;
        mRailPosture->initWithoutIter();
        mRailPosture->start();
    }

    MR::initShadowVolumeCylinder(this, 450.0f);
    MR::onShadowFollowHostScale(this, 0);

    if (mRailMover == nullptr) {
        MR::onCalcShadowOneTime(this, 0);
    }

    s32 drop_length = -1;
    MR::getPlacementArg7NoInit(rIter, &drop_length);
    if (drop_length != -1) {
        MR::setShadowDropLength(this, 0, drop_length);
    }

    if (mRailMover != nullptr) {
        MR::initAndSetRailClipping(&mClipCenter, this, 100.0f, 500.0f);
    } else {
        MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    }

    if (mRailMover != nullptr) {
        MR::onCalcGravity(this);
    } else {
        MR::calcGravity(this);
    }

    MR::getPlacementArg0NoInit(rIter, &mRotateSpeed);
    mRotateSpeed /= 100.0f;
    mLodCtrlPlanet = MR::createLodCtrlPlanet(this, rIter, 100.0f);
    mLodCtrlPlanet->validate();
    initNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvWait));
    makeActorAppeared();
}

void BeeFlowerHover::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }
}

void BeeFlowerHover::exeSoftTouch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "OnBee");
        MR::startSound(this, "SE_OJ_BEE_FLOWER_BEE_ON");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvSoftTouchWait));
    }
}

void BeeFlowerHover::exeSoftTouchWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (!MR::isPlayerElementModeBee()) {
        setNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvHardTouch));
    } else if (!MR::isOnPlayer(getSensor(nullptr))) {
        setNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvWait));
    }
}

void BeeFlowerHover::exeHardTouch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ThroughMario");
        MR::startSound(this, "SE_OJ_BEE_FLOWER_DISAPPEAR");
        MR::invalidateCollisionParts(this);
        MR::invalidateShadow(this, 0);
        MR::onCalcShadowOneTime(this, 0);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isBckStopped(this)) {
        MR::hideModel(this);
        setNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvRecover));
    }
}

void BeeFlowerHover::exeRecover() {
    if (MR::isStep(this, ::sStepToRecoverStart)) {
        MR::validateShadow(this, 0);
        MR::onCalcShadowOneTime(this, 0);
        MR::showModel(this);
        MR::startBck(this, "Appear");
        MR::startSound(this, "SE_OJ_BEE_FLOWER_RECOVER");
    }

    if (MR::isGreaterEqualStep(this, ::sStepToRecoverStart) && MR::isBckStopped(this) && !MR::isHiddenModel(this)) {
        MR::validateCollisionParts(this);
        setNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvWait));
    }
}

void BeeFlowerHover::control() {
    if (!isNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvRecover))) {
        mLodCtrlPlanet->update();
    }

    Pos3f mtx;
    mtx.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), MR::toRadian(mRotateSpeed));
    mRotateMtx.concat(mtx);

    if (mRailMover != nullptr) {
        mRailMover->movement();
        mRailPosture->movement();
        mPosition.set(mRailMover->mStartPos);
    }
}

void BeeFlowerHover::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    baseMtx.identity();

    if (mRailPosture) {
        baseMtx.concat(mRailPosture->mPostureMtx);
    } else {
        MR::makeMtxRotate(baseMtx.toMtxPtr(), mRotation);
    }

    baseMtx.concat(mRotateMtx);
    baseMtx.mMtx[0][3] = mPosition.x;
    baseMtx.mMtx[1][3] = mPosition.y;
    baseMtx.mMtx[2][3] = mPosition.z;
    MR::setBaseTRMtx(this, baseMtx);
}

bool BeeFlowerHover::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (!isNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvWait))) {
        return false;
    }

    if (MR::isMsgFloorTouch(msg)) {
        if (MR::isPlayerElementModeBee()) {
            setNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvSoftTouch));
        } else {
            setNerve(GET_NERVE(BeeFlowerHover, BeeFlowerHoverNrvHardTouch));
        }

        return true;
    }

    return false;
}
