#include "Game/MapObj/SnowMan.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 1.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 0.60f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 0.8f);

namespace {
    NEBULA_TUNABLE(s32, sStepForMelt, 20);
};  // namespace

namespace NrvSnowMan {
    NEW_NERVE(SnowManNrvWait, SnowMan, Wait);
    NEW_NERVE(SnowManNrvWaitBody, SnowMan, WaitBody);
    NEW_NERVE(SnowManNrvWaitHead, SnowMan, WaitHead);
    NEW_NERVE(SnowManNrvMeltHead, SnowMan, MeltHead);
    NEW_NERVE(SnowManNrvMeltBody, SnowMan, MeltBody);
    NEW_NERVE(SnowManNrvDownHead, SnowMan, DownHead);
    NEW_NERVE(SnowManNrvDownBody, SnowMan, DownBody);
};  // namespace NrvSnowMan

SnowMan::SnowMan(const char* pName) : LiveActor(pName), mHeadCollisionParts(), mBodyCollisionParts(), mAnimScaleCtrl(), mAnimScaleParam() {
    lumacore::setClass(this, lumacore::kClassId< SnowMan >);
}

void SnowMan::init(const TableIter& rrIter) {
    MR::initDefaultPos(this, rrIter);
    initModelManagerWithAnm("SnowMan", nullptr, false);
    MR::connectToSceneMapObjStrongLight(this);
    initHitSensor(2);
    MR::addMessageSensorMapObj(this, "head");
    MR::addMessageSensorMapObj(this, "body");
    mHeadCollisionParts = MR::createCollisionPartsFromLiveActor(this, "HeadCol", getSensor("head"), MR::getJointMtx(this, "Head"),
                                                                MR::CollisionScaleType_NotUsingScale);
    mBodyCollisionParts = MR::createCollisionPartsFromLiveActor(this, "BodyCol", getSensor("body"), MR::getJointMtx(this, "Body"),
                                                                MR::CollisionScaleType_NotUsingScale);
    MR::validateCollisionParts(mHeadCollisionParts);
    MR::validateCollisionParts(mBodyCollisionParts);
    mAnimScaleParam = new AnimScaleParam();
    mAnimScaleParam->mScaleMin = cNum_init_f1;
    mAnimScaleParam->mScaleMax = cNum_init_f2;
    mAnimScaleParam->mHitReactionSpring = cNum_init_f3;
    mAnimScaleParam->mHitReactionDamping = cNum_init_f4;
    mAnimScaleCtrl = new AnimScaleController(mAnimScaleParam);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::useStageSwitchWriteA(this, rrIter);
    MR::useStageSwitchWriteB(this, rrIter);
    MR::useStageSwitchWriteDead(this, rrIter);
    initNerve(GET_NERVE(SnowMan, SnowManNrvWait));
    makeActorAppeared();
}

void SnowMan::control() {
    if (mHeadCollisionParts->mIsValid) {
        mHeadCollisionParts->setMtx();
    }

    if (mBodyCollisionParts->mIsValid) {
        mBodyCollisionParts->setMtx();
    }
    mAnimScaleCtrl->updateNerve();
}

void SnowMan::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
    LiveActor::kill();
}

bool SnowMan::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgFireBallAttack(msg)) {
        if (pReceiver == getSensor("head")) {
            damageHead();
        } else {
            damageBody();
        }

        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (mAnimScaleCtrl->isHitReaction(-1)) {
            return false;
        } else {
            mAnimScaleCtrl->startHitReaction();
            return true;
        }
    }

    return false;
}

void SnowMan::damageHead() {
    if (isNerve(GET_NERVE(SnowMan, SnowManNrvWait))) {
        setNerve(GET_NERVE(SnowMan, SnowManNrvMeltHead));
    } else if (isNerve(GET_NERVE(SnowMan, SnowManNrvWaitHead))) {
        setNerve(GET_NERVE(SnowMan, SnowManNrvDownHead));
    }
}

void SnowMan::damageBody() {
    if (isNerve(GET_NERVE(SnowMan, SnowManNrvWait))) {
        setNerve(GET_NERVE(SnowMan, SnowManNrvMeltBody));
    } else if (isNerve(GET_NERVE(SnowMan, SnowManNrvWaitBody))) {
        setNerve(GET_NERVE(SnowMan, SnowManNrvDownBody));
    }
}

void SnowMan::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    Vec3f scale = mAnimScaleCtrl->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void SnowMan::exeWait() {
}

void SnowMan::exeWaitBody() {
}

void SnowMan::exeWaitHead() {
}

void SnowMan::exeMeltHead() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(mHeadCollisionParts);
        MR::startBck(this, "MeltHead");
        MR::startSound(this, "SE_OJ_SNOW_MAN_MELT");
        MR::shakeCameraWeak();
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }
    }

    if (MR::isStep(this, ::sStepForMelt)) {
        MR::hideMaterial(this, "SnowManBucketMat_v");
        setNerve(GET_NERVE(SnowMan, SnowManNrvWaitBody));
    }
}

void SnowMan::exeMeltBody() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(mBodyCollisionParts);
        MR::startBck(this, "MeltBody");
        MR::startSound(this, "SE_OJ_SNOW_MAN_MELT");
        MR::shakeCameraWeak();
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        if (MR::isValidSwitchB(this)) {
            MR::onSwitchB(this);
        }
    }

    if (MR::isStep(this, ::sStepForMelt)) {
        setNerve(GET_NERVE(SnowMan, SnowManNrvWaitHead));
    }
}

void SnowMan::exeDownHead() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(mHeadCollisionParts);
        MR::invalidateCollisionParts(mBodyCollisionParts);
        MR::startBck(this, "DownHead");
        MR::startSound(this, "SE_OJ_SNOW_MAN_MELT");
        MR::shakeCameraWeak();
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void SnowMan::exeDownBody() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(mHeadCollisionParts);
        MR::invalidateCollisionParts(mBodyCollisionParts);
        MR::startBck(this, "DownBody");
        MR::startSound(this, "SE_OJ_SNOW_MAN_MELT");
        MR::shakeCameraWeak();
        if (MR::isValidSwitchB(this)) {
            MR::onSwitchB(this);
        }
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}
