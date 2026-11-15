#include "Game/MapObj/WaterLeakPipe.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sDefaultHeight = 500.0f;
    const s32 sLifeTime = 15;
};  // namespace

namespace NrvIceStepNoSlip {
    NEW_NERVE(IceStepNoSlipNrvAppear, IceStepNoSlip, Appear);
    NEW_NERVE(IceStepNoSlipNrvBreak, IceStepNoSlip, Break);
};  // namespace NrvIceStepNoSlip

namespace NrvWaterLeakPipe {
    NEW_NERVE(WaterLeakPipeNrvWait, WaterLeakPipe, Wait);
    NEW_NERVE(WaterLeakPipeNrvFreeze, WaterLeakPipe, Freeze);
};  // namespace NrvWaterLeakPipe

IceStepNoSlip::IceStepNoSlip(MtxPtr mtx) : ModelObj("アイス床", "IceStepNoSlip", mtx, MR::DrawBufferType_IndirectMapObjStrongLight, -2, -2, false) {
    lumacore::setClass(this, lumacore::kClassId< IceStepNoSlip >);
}

void IceStepNoSlip::init(const TableIter& rIter) {
    ModelObj::init(rIter);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, "IceStepNoSlip", getSensor(nullptr), nullptr);
    initNerve(GET_NERVE(IceStepNoSlip, IceStepNoSlipNrvAppear));
}

void IceStepNoSlip::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(IceStepNoSlip, IceStepNoSlipNrvAppear));
}

void IceStepNoSlip::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Start");
    }
}

void IceStepNoSlip::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "End");
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}

WaterLeakPipe::WaterLeakPipe(const char* pName)
    : LiveActor(pName), mIceStep(), mPipeHeight(::sDefaultHeight), mTopMtx(), mBottomMtx(), mClippingCenter(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< WaterLeakPipe >);
}

void WaterLeakPipe::init(const TableIter& rIter) {
    Vec3f upVec;
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("WaterLeakPipe", nullptr, false);
    mTopMtx = MR::getJointMtx(this, "Top");
    mBottomMtx = MR::getJointMtx(this, "Bottom");
    MR::getPlacementArg0NoInit(rIter, &mPipeHeight);
    initPipeHeight();
    MR::connectToSceneMapObjNoCalcAnim(this);
    initHitSensor(1);
    MR::addHitSensorAtJointMapObj(this, "ice", "Top", 8, 120.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::calcUpVec(&upVec, this);
    mClippingCenter.scaleAdd(0.5f * mPipeHeight, upVec, mPosition);
    MR::setClippingTypeSphere(this, (0.5f * mPipeHeight), &mClippingCenter);
    mIceStep = new IceStepNoSlip(mTopMtx);
    mIceStep->initWithoutIter();
    mIceStep->makeActorDead();
    initNerve(GET_NERVE(WaterLeakPipe, WaterLeakPipeNrvWait));
    makeActorAppeared();
}

void WaterLeakPipe::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Splash");
    }

    MR::startLevelSound(this, "SE_OJ_LV_WATER_LEAK");
}

void WaterLeakPipe::exeFreeze() {
    if (MR::isFirstStep(this) && MR::isDead(mIceStep)) {
        MR::forceDeleteEffect(this, "Splash");
        mIceStep->appear();
        MR::startSound(this, "SE_OJ_ICE_FLOOR_FREEZE");
        MR::invalidateClipping(this);
    } else if (MR::isOnPlayer(mIceStep) && MR::isPlayerElementModeIce()) {
        setNerve(GET_NERVE(WaterLeakPipe, WaterLeakPipeNrvFreeze));
    } else if (MR::isStep(this, ::sLifeTime)) {
        mIceStep->setNerve(GET_NERVE(IceStepNoSlip, IceStepNoSlipNrvBreak));
        MR::startSound(this, "SE_OJ_ICE_FLOOR_MELT");
        MR::validateClipping(this);
        setNerve(GET_NERVE(WaterLeakPipe, WaterLeakPipeNrvWait));
    }
}

bool WaterLeakPipe::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (!MR::isPlayerElementModeIce()) {
        return false;
    }

    if (isNerve(GET_NERVE(WaterLeakPipe, WaterLeakPipeNrvFreeze))) {
        return false;
    }

    if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
        Vec3f upVec;
        MR::calcUpVec(&upVec, this);
        Vec3f toPlayer;
        toPlayer.sub(*MR::getPlayerPos(), mPosition);

        if (toPlayer.dot(upVec) < 0.0f) {
            return false;
        }

        if (MR::getPlayerVelocity()->dot(upVec) >= 0.0f) {
            return false;
        }

        setNerve(GET_NERVE(WaterLeakPipe, WaterLeakPipeNrvFreeze));
        return false;
    }

    return false;
}

void WaterLeakPipe::initPipeHeight() {
    Pos3f mtx;
    mtx.set(mTopMtx);
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);
    Vec3f topPos;
    topPos.scaleAdd(mPipeHeight, upVec, mPosition);
    mtx.setTrans(topPos);
    NebulaMtxCopy(mtx, mTopMtx);
    calcAndSetBaseMtx();
}
