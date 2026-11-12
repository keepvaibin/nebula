#include "Game/Enemy/BegomanSpringHead.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvBegomanSpringHead {
    NEW_NERVE(HostTypeNrvWait, BegomanSpringHead, Wait);
    NEW_NERVE(HostTypeNrvHopStart, BegomanSpringHead, HopStart);
    NEW_NERVE(HostTypeNrvHopWait, BegomanSpringHead, HopWait);
    NEW_NERVE(HostTypeNrvHopEnd, BegomanSpringHead, HopEnd);
    NEW_NERVE(HostTypeNrvHopJump, BegomanSpringHead, HopJump);
};  // namespace NrvBegomanSpringHead

void BegomanHead::calcAndSetBaseMtx() {
    PartsModel::calcAndSetBaseMtx();
    Vec3f scale = *mHostScale * mScale;
    MR::setBaseScale(this, scale);
}

BegomanSpringHead::BegomanSpringHead(LiveActor* pActor, MtxPtr pMtx) : BegomanHead(pActor, "バネ頭", "BegomanSpringHead", pMtx, MR::DrawBufferType_MapObjStrongLight, false) {
    lumacore::setClass(this, lumacore::kClassId< BegomanSpringHead >);
}

void BegomanSpringHead::init(const TableIter& rIter) {
    initNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvWait));
    MR::initLightCtrl(this);
    initHitSensor(1);
    MR::addHitSensorAtJoint(this, "head", "SpringJoint5", ATYPE_MAP_OBJ_SIMPLE, 8, 100.0f, Vec3f(0.0f, 100.0f, 0.0f));
    PartsModel::init(rIter);
}

bool BegomanSpringHead::isSpringHop() {
    return !LiveActor::isNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvWait));
}

s32 BegomanSpringHead::getHopEndBckFrameMax() {
    return MR::getBckFrameMax(this, "HopEnd");
}

void BegomanSpringHead::tryHopStart() {
    if (!isNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvHopStart))) {
        setNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvHopStart));
    }
}

void BegomanSpringHead::tryHopEnd() {
    if (isNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvHopEnd)) || isNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvWait))) {
        return;
    }

    setNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvHopEnd));
}

void BegomanSpringHead::tryHopJump() {
    if (!isNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvHopJump))) {
        setNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvHopJump));
    }
}

void BegomanSpringHead::forceWaitImmediately() {
    MR::startBck(this, "Wait");
    MR::startBrk(this, "Green");
    setNerve(GET_NERVE(BegomanSpringHead, HostTypeNrvWait));
}

void BegomanSpringHead::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::startBrk(this, "Green");
    }
}

void BegomanSpringHead::exeHopStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HopStart");
        MR::startBrk(this, "OnAndOff");
    }

    MR::setNerveAtBckStopped(this, GET_NERVE(BegomanSpringHead, HostTypeNrvHopWait));
}

void BegomanSpringHead::exeHopWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HopWait");
        MR::startBrk(this, "OnAndOff");
    }
}

void BegomanSpringHead::exeHopEnd() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_BEGOMAN_CLOSE_SPRING");
    }

    MR::startBckAtFirstStep(this, "HopEnd");
    MR::setNerveAtBckStopped(this, GET_NERVE(BegomanSpringHead, HostTypeNrvWait));
}

void BegomanSpringHead::exeHopJump() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HopJump");
        MR::startBrk(this, "OnAndOff");
    }

    MR::setNerveAtBckStopped(this, GET_NERVE(BegomanSpringHead, HostTypeNrvHopWait));
}
