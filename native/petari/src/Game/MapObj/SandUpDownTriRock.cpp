#include "Game/MapObj/SandUpDownTriRock.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvSandUpDownTriRock {
    NEW_NERVE(SandUpDownTriRockNrvWait, SandUpDownTriRock, Wait);
    NEW_NERVE(SandUpDownTriRockNrvDown, SandUpDownTriRock, Down);
    NEW_NERVE(SandUpDownTriRockNrvRest, SandUpDownTriRock, Rest);
    NEW_NERVE(SandUpDownTriRockNrvUp, SandUpDownTriRock, Up);
    NEW_NERVE(SandUpDownTriRockNrvStop, SandUpDownTriRock, Stop);
};  // namespace NrvSandUpDownTriRock

SandUpDownTriRock::SandUpDownTriRock(const char* pName) : MapObjActor(pName), mRestStep() {
    lumacore::setClass(this, lumacore::kClassId< SandUpDownTriRock >);
    for (s32 i = 0; i < ARRAY_SIZE(mRockCollisionParts); i++) {
        mRockCollisionParts[i] = nullptr;
    }
}

void SandUpDownTriRock::init(const TableIter& rIter) {
    MapObjActor::init(rIter);

    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupHitSensor();
    info.setupEffect(nullptr);
    info.setupSound(4);
    info.setupNerve(GET_NERVE(SandUpDownTriRock, SandUpDownTriRockNrvWait));
    initialize(rIter, info);

    MR::getPlacementArg0NoInit(rIter, &mRestStep);

    for (s32 i = 0; i < ARRAY_SIZE(mRockCollisionParts); i++) {
        const MR::FormattedName name("Move0%d", i);

        mRockCollisionParts[i] = MR::createCollisionPartsFromLiveActor(this, name.c_str(), getSensor("body"), MR::getJointMtx(this, name.c_str()),
                                                                       MR::CollisionScaleType_Default);
        MR::validateCollisionParts(mRockCollisionParts[i]);
    }
}

void SandUpDownTriRock::exeWait() {
}

void SandUpDownTriRock::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "On");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(SandUpDownTriRock, SandUpDownTriRockNrvRest));
    }
}

void SandUpDownTriRock::exeRest() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    if (MR::isStep(this, mRestStep)) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(SandUpDownTriRock, SandUpDownTriRockNrvUp));
    }
}

void SandUpDownTriRock::exeUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Return");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(SandUpDownTriRock, SandUpDownTriRockNrvWait));
    }
}

void SandUpDownTriRock::exeStop() {
}

bool SandUpDownTriRock::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStartPowerStarGet(msg)) {
        setNerve(GET_NERVE(SandUpDownTriRock, SandUpDownTriRockNrvStop));

        return true;
    }

    return false;
}

void SandUpDownTriRock::initCaseUseSwitchB(const MapObjActorInitInfo& /*info*/) {
    MR::listenStageSwitchOnB(this, MR::Functor(this, &SandUpDownTriRock::startDown));
}

void SandUpDownTriRock::control() {
    for (s32 i = 0; i < ARRAY_SIZE(mRockCollisionParts); i++) {
        mRockCollisionParts[i]->setMtx();
    }
}

void SandUpDownTriRock::startDown() {
    setNerve(GET_NERVE(SandUpDownTriRock, SandUpDownTriRockNrvDown));
}
