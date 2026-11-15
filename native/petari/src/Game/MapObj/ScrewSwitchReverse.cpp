#include "Game/MapObj/ScrewSwitchReverse.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sDefaultShadowLength = 1000;
    const s32 sStepForAdjust = 3;
    const s32 sStepToRumbleCamera = 25;
    const f32 sJumpPower = 28.0f;
};  // namespace

namespace NrvScrewSwitchReverse {
    NEW_NERVE(ScrewSwitchReverseNrvWait, ScrewSwitchReverse, Wait);
    NEW_NERVE(ScrewSwitchReverseNrvAdjust, ScrewSwitchReverse, Adjust);
    NEW_NERVE(ScrewSwitchReverseNrvScrew, ScrewSwitchReverse, Screw);
};  // namespace NrvScrewSwitchReverse

ScrewSwitchReverse::ScrewSwitchReverse(const char* pName) : LiveActor(pName), mHost(), mIsForceJump() {
    lumacore::setClass(this, lumacore::kClassId< ScrewSwitchReverse >);
}

void ScrewSwitchReverse::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::needStageSwitchWriteA(this, rIter);
    MR::useStageSwitchSleep(this, rIter);

    initModelManagerWithAnm("ScrewSwitchReverse", nullptr, false);

    MR::connectToSceneMapObjDecorationStrongLight(this);

    MR::initLightCtrl(this);

    initHitSensor(2);
    MR::addBodyMessageSensorMapObj(this);
    Vec3f offset;
    offset.scale(-130.0f, mGravity);
    MR::addHitSensorAtJoint(this, "binder", "Screw", ATYPE_BINDER, 8, 120.0f, offset);

    MR::initCollisionParts(this, "ScrewReverseCol", getSensor("binder"), MR::getJointMtx(this, "Screw"));
    initEffectKeeper(0, nullptr);
    initSound(4, false);

    s32 shadowLength = ::sDefaultShadowLength;
    f32 dropLength = shadowLength;
    MR::getPlacementArg7NoInit(rIter, &dropLength);

    if (dropLength > 0.0f) {
        Vec3f pos(mPosition);
        pos.add(Vec3f(0.0f, 10.0f, 0.0f));
        MR::initShadowVolumeCylinder(this, 100.0f);
        MR::setShadowDropPosition(this, nullptr, pos);
        MR::setShadowDropLength(this, nullptr, dropLength);

        MR::calcGravity(this);
    }

    MR::getPlacementArg0NoInit(rIter, &mIsForceJump);

    initNerve(GET_NERVE(ScrewSwitchReverse, ScrewSwitchReverseNrvWait));

    makeActorAppeared();
}

void ScrewSwitchReverse::exeWait() {
}

void ScrewSwitchReverse::exeAdjust() {
    mHost->mPosition.lerp(mHost->mPosition, getSensor("binder")->mPosition, getNerveStep() / 3.0f);

    if (MR::isStep(this, ::sStepForAdjust)) {
        setNerve(GET_NERVE(ScrewSwitchReverse, ScrewSwitchReverseNrvScrew));
    }
}

void ScrewSwitchReverse::exeScrew() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ScrewSwitchReverseOn");
        MR::startBckPlayer("ScrewSwitchReverseOn");
    }

    if (MR::isLessStep(this, ::sStepToRumbleCamera)) {
        MR::startLevelSound(this, "SE_OJ_LV_SCR_SWITCH_R_MOVE");
    }

    if (MR::isStep(this, ::sStepToRumbleCamera)) {
        MR::startSound(this, "SE_OJ_SCR_SWITCH_R_ON");

        MR::tryRumblePadVeryStrong(this, 0);
        MR::shakeCameraNormal();

        MR::invalidateHitSensors(this);
        MR::invalidateCollisionParts(this);

        if (MR::isPlayerInRush()) {
            endBind();
        }
    }

    if (MR::isBckStopped(this)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");

        MR::onSwitchA(this);

        kill();
    }
}

bool ScrewSwitchReverse::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgRushBegin(msg) && MR::isSensorPlayer(pSender) && MR::isOnPlayer(getSensor("binder"))) {
        mHost = pSender->mHost;

        MR::startSound(mHost, "SE_PV_TWIST_START");
        MR::startSound(mHost, "SE_PM_SPIN_ATTACK");

        setNerve(GET_NERVE(ScrewSwitchReverse, ScrewSwitchReverseNrvAdjust));

        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg) && mHost != nullptr && isNerve(GET_NERVE(ScrewSwitchReverse, ScrewSwitchReverseNrvScrew))) {
        updateBindActorMtx();
        return true;
    }

    return false;
}

void ScrewSwitchReverse::updateBindActorMtx() {
    Pos3f baseMtx = mHost->getBaseMtx();
    baseMtx.setTrans(getSensor("binder")->mPosition);
    MR::setBaseTRMtx(mHost, baseMtx);
}

void ScrewSwitchReverse::endBind() {
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    Vec3f jumpVec;
    jumpVec.scale(::sJumpPower, upVec);

    if (mIsForceJump) {
        MR::endBindAndPlayerForceJump(this, jumpVec, 0);
    } else {
        MR::endBindAndPlayerJump(this, jumpVec, 0);
    }

    mHost = nullptr;
}
