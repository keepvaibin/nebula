#include "Game/NPC/TicoRail.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 cStepForWait = NebulaFrameClock::stepsOfSeconds(1);
    // Look around: turn one way until L, back until LR, then forward again until LRL.
    NEBULA_TUNABLE(s32, cStepForLookAroundL, 40);
    const s32 cStepForLookAroundLR = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, cStepForLookAroundLRL, 160);
    NEBULA_TUNABLE(s32, cStepForMove, 500);
    NEBULA_TUNABLE(s32, cStepForTalk, 320);
    // Past its minimum step a nerve ends with one chance in this many per step, and surely this many steps later.
    const s32 cStepRandomRange = NebulaFrameClock::stepsOfSeconds(5);
    // One chance in this many per step of a reaction while talking.
    NEBULA_TUNABLE(s32, cTalkReactionChance, 60);
    NEBULA_TUNABLE(s32, cTalkReactionInterpole, 5);
    NEBULA_TUNABLE(f32, cRailSpeedMove, 15.0f);
    NEBULA_TUNABLE(f32, cRailSpeedTalkStart, 2.0f);
    NEBULA_TUNABLE(f32, cRailSpeedGoodBye, 1.5f);
    NEBULA_TUNABLE(f32, cTurnRate, 1.2f);
    NEBULA_TUNABLE(f32, cTalkTerritoryY, 30.0f);
    NEBULA_TUNABLE(f32, cBodyRadius, 50.0f);
}  // namespace

namespace NrvTicoRail {
    NEW_NERVE(TicoRailNrvWait, TicoRail, Wait);
    NEW_NERVE(TicoRailNrvLookAround, TicoRail, LookAround);
    NEW_NERVE(TicoRailNrvMoveSign, TicoRail, MoveSign);
    NEW_NERVE(TicoRailNrvMoveSignAndTurn, TicoRail, MoveSign);
    NEW_NERVE(TicoRailNrvMove, TicoRail, Move);
    NEW_NERVE(TicoRailNrvStop, TicoRail, Stop);
    NEW_NERVE(TicoRailNrvTalkStart, TicoRail, TalkStart);
    NEW_NERVE(TicoRailNrvTalk, TicoRail, Talk);
    NEW_NERVE(TicoRailNrvTalkCancel, TicoRail, TalkCancel);
    NEW_NERVE(TicoRailNrvGoodBye, TicoRail, GoodBye);
};  // namespace NrvTicoRail

TicoRail::TicoRail(const char* pName) : LiveActor(pName), mFront(0.0f, 0.0f, 1.0f), mTalkPartner(nullptr), mLodCtrl(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< TicoRail >);
}

void TicoRail::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Tico", nullptr, false);
    MR::connectToSceneNpc(this);
    initHitSensor(1);
    MR::addHitSensorNpc(this, "body", 8, ::cBodyRadius, Vec3f(0.0f, ::cBodyRadius, 0.0f));
    MR::initLightCtrl(this);
    initEffectKeeper(0, nullptr);
    MR::initShadowFromCSV(this, "Shadow");
    initRailRider(rIter);
    MR::moveCoordAndTransToNearestRailPos(this);
    mFront.set(MR::getRailDirection(this));
    mLodCtrl = MR::createLodCtrlNPC(this, rIter);

    s32 color = 0;
    MR::getPlacementArg0NoInit(rIter, &color);
    MR::startBrk(this, "ColorChange");
    MR::setBrkFrameAndStop(this, color);
    AstroDemoFunction::tryRegisterGrandStarReturnAndSimpleCast(this, rIter);
    AstroDemoFunction::tryRegisterDemo(this, "バトラーグリーンドライバ説明", rIter);

    if (MR::getRandom(0, 2) == 0) {
        initNerve(GET_NERVE(TicoRail, TicoRailNrvWait));
    } else {
        initNerve(GET_NERVE(TicoRail, TicoRailNrvMove));
    }

    makeActorAppeared();
}

void TicoRail::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Turn");
    }

    if (isGreaterEqualStepAndRandom(::cStepForWait)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvLookAround));
    }
}

void TicoRail::exeLookAround() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Turn");
    }

    Vec3f up;
    MR::calcUpVec(&up, this);

    f32 turn = 0.0f;
    if (MR::isLessStep(this, ::cStepForLookAroundL)) {
        turn = ::cTurnRate;
    } else if (MR::isLessStep(this, ::cStepForLookAroundLR)) {
        turn = -::cTurnRate;
    } else if (MR::isLessStep(this, ::cStepForLookAroundLRL)) {
        turn = ::cTurnRate;
    }

    MR::rotateVecDegree(&mFront, up, turn);

    if (MR::isStep(this, ::cStepForLookAroundLRL)) {
        if (MR::getRandom(0, 2) != 0) {
            setNerve(GET_NERVE(TicoRail, TicoRailNrvMoveSign));
        } else {
            setNerve(GET_NERVE(TicoRail, TicoRailNrvMoveSignAndTurn));
        }
    }
}

void TicoRail::exeMoveSign() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Spin");

        if (isNerve(GET_NERVE(TicoRail, TicoRailNrvMoveSignAndTurn))) {
            MR::reverseRailDirection(this);
        }
    }

    const f32 rate = MR::calcNerveRate(this, MR::getBckFrameMax(this));
    MR::blendVec(&mFront, -MR::getRailDirection(this), MR::getRailDirection(this), rate);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvMove));
    }
}

void TicoRail::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
    }

    MR::moveCoordAndFollowTrans(this, ::cRailSpeedMove);
    mFront.set(MR::getRailDirection(this));

    if (isGreaterEqualStepAndRandom(::cStepForMove)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvStop));
    }
}

void TicoRail::exeStop() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Spin");
    }

    MR::moveCoordAndFollowTrans(this, MR::calcNerveValue(this, MR::getBckFrameMax(this), ::cRailSpeedMove, 0.0f));

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvWait));
    }
}

void TicoRail::exeTalkStart() {
    Vec3f toPartner;
    toPartner.sub(mTalkPartner->mPosition, mPosition);
    MR::normalize(&toPartner);

    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Spin");

        if (toPartner.dot(MR::getRailDirection(this)) > 0.0f) {
            MR::reverseRailDirection(this);
        }
    }

    MR::moveCoordAndFollowTrans(this, ::cRailSpeedTalkStart);
    MR::blendVec(&mFront, MR::getRailDirection(this), toPartner, MR::calcNerveRate(this, MR::getBckFrameMax(this)));

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvTalk));
    }
}

void TicoRail::exeTalk() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Talk");
    }

    if (!MR::isBckPlaying(this, "Reaction") && MR::getRandom(0, ::cTalkReactionChance) == 0) {
        MR::startBckWithInterpole(this, "Reaction", ::cTalkReactionInterpole);
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startBck(this, "Talk");
    }

    if (MR::isStep(this, ::cStepForTalk)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvGoodBye));
    }
}

void TicoRail::exeTalkCancel() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Spin");
    }

    MR::moveCoordAndFollowTrans(this, ::cRailSpeedMove);
    mFront.set(MR::getRailDirection(this));

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvMove));
    }
}

void TicoRail::exeGoodBye() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "CallBack");

        if (mFront.dot(MR::getRailDirection(this)) > 0.0f) {
            MR::reverseRailDirection(this);
        }
    }

    MR::moveCoordAndFollowTrans(this, ::cRailSpeedGoodBye);

    if (MR::isBckLooped(this)) {
        mTalkPartner = nullptr;
        setNerve(GET_NERVE(TicoRail, TicoRailNrvMoveSign));
    }
}

bool TicoRail::isGreaterEqualStepAndRandom(s32 step) const {
    if (MR::isGreaterEqualStep(this, step + ::cStepRandomRange)) {
        return true;
    }

    return MR::isGreaterEqualStep(this, step) && MR::getRandom(0, ::cStepRandomRange) == 0;
}

void TicoRail::kill() {
    LiveActor::kill();
    mLodCtrl->invalidate();
}

void TicoRail::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::setBaseTRMtx(this, mtx);
}

void TicoRail::control() {
    mLodCtrl->update();
}

void TicoRail::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (!MR::isSensorNpc(pReceiver) || isNrvTalking()) {
        return;
    }

    if (MR::sendArbitraryMsg(ACTMES_TICO_RAIL_TOUCH, pReceiver, pSender)) {
        mTalkPartner = pReceiver->mHost;
        setNerve(GET_NERVE(TicoRail, TicoRailNrvTalkStart));
    } else if (!MR::isExistRail(pReceiver->mHost) || MR::isRailGoingToEnd(this) != MR::isRailGoingToEnd(pReceiver->mHost)) {
        setNerve(GET_NERVE(TicoRail, TicoRailNrvTalkCancel));
    }
}

bool TicoRail::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg != ACTMES_TICO_RAIL_TOUCH || isNrvTalking()) {
        return false;
    }

    if (!isSameRailActor(pReceiver->mHost)) {
        return false;
    }

    if (MR::getRandom(0, 2) == 0) {
        return false;
    }

    if (MR::calcDistanceVertical(this, pSender->mHost->mPosition) > ::cTalkTerritoryY) {
        return false;
    }

    mTalkPartner = pSender->mHost;
    setNerve(GET_NERVE(TicoRail, TicoRailNrvTalkStart));
    return true;
}

bool TicoRail::isNrvTalking() const {
    return isNerve(GET_NERVE(TicoRail, TicoRailNrvTalkStart)) || isNerve(GET_NERVE(TicoRail, TicoRailNrvTalk)) ||
           isNerve(GET_NERVE(TicoRail, TicoRailNrvTalkCancel)) || isNerve(GET_NERVE(TicoRail, TicoRailNrvGoodBye));
}

bool TicoRail::isSameRailActor(const LiveActor* pActor) const {
    if (!MR::isExistRail(pActor)) {
        return false;
    }

    return MR::getRailPointPosStart(this) == MR::getRailPointPosStart(pActor) && MR::getRailPointPosEnd(this) == MR::getRailPointPosEnd(pActor);
}
