#include "Game/NPC/KoopaJr.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sSensorSize = 175.0f;
    const f32 sShadowRadius = 100.0f;
    const s32 sStarPieceNum = 20;
};  // namespace

namespace NrvKoopaJr {
    NEW_NERVE(HostTypeWait, KoopaJr, Wait);
    NEW_NERVE(HostTypeReaction, KoopaJr, Reaction);
    NEW_NERVE(HostTypeReactionEnd, KoopaJr, ReactionEnd);
    NEW_NERVE(HostTypeShipBattleAppear, KoopaJr, ShipBattleAppear);
    NEW_NERVE(HostTypeShipBattlePowerUp, KoopaJr, ShipBattlePowerUp);
    NEW_NERVE(HostTypeShipBattleEscape, KoopaJr, ShipBattleEscape);
    NEW_NERVE(HostTypeShipBattleShipDamage, KoopaJr, ShipBattleShipDamage);
    NEW_NERVE(HostTypeShipBattleDemoTalkStart, KoopaJr, ShipBattleDemoTalkStart);
    NEW_NERVE(HostTypeShipBattleDemoTalkWait, KoopaJr, ShipBattleDemoTalkWait);
};  // namespace NrvKoopaJr

KoopaJr::KoopaJr(const char* pName) : NPCActor(pName), mIsInvalidAppearStarPiece(), mIsShipBattleTalk() {
    lumacore::setClass(this, lumacore::kClassId< KoopaJr >);
}

void KoopaJr::init(const TableIter& rIter) {
    NPCActor::init(rIter);
    initModelManagerWithAnm("KoopaJr", nullptr, false);

    NPCActorCaps caps("KoopaJr");
    caps.mConnectTo = true;
    caps.mPosition = true;
    caps.mLodCtrl = true;
    caps.mLightCtrl = true;
    caps.mSound = true;
    caps.mSoundSize = 4;
    caps.mSensor = true;
    caps.mSensorSize = ::sSensorSize;
    caps.mSensorOffset.y = ::sSensorSize;
    caps.mNerve = true;
    caps.mWaitNerve = GET_NERVE(KoopaJr, HostTypeWait);
    caps.mMessage = true;
    caps.mShadow = true;
    caps.mShadowSize = ::sShadowRadius;
    caps.mEffect = true;
    mParam.setSingleAction("Wait");
    mParam.mTurnWhenNear = 0;
    mParam.mTurnOnTalk = 0;
    mReactionAction = "Damage";
    initialize(rIter, caps);

    MR::declareStarPiece(this, ::sStarPieceNum);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoActionFunctor(this, MR::Functor(this, &KoopaJr::startShipBattleTalk), "クッパJr会話");
    }

    MR::onCalcShadow(this, nullptr);
    MR::setShadowDropLength(this, nullptr, 200.0f);
    makeActorAppeared();
    MR::startBva(this, "Apron");
}

void KoopaJr::kill() {
    if (mLodCtrl != nullptr) {
        mLodCtrl->invalidate();
    }

    LiveActor::kill();
}

void KoopaJr::setStateShipBattleAppear() {
    setNerve(GET_NERVE(KoopaJr, HostTypeShipBattleAppear));
}

void KoopaJr::setStateShipBattlePowerUp() {
    setNerve(GET_NERVE(KoopaJr, HostTypeShipBattlePowerUp));
}

void KoopaJr::setStateShipBattleEscape() {
    setNerve(GET_NERVE(KoopaJr, HostTypeShipBattleEscape));
}

void KoopaJr::setStateShipBattleShipDamage() {
    setNerve(GET_NERVE(KoopaJr, HostTypeShipBattleShipDamage));
}

void KoopaJr::endShipBattleTalk() {
    setNerve(mWaitNerve);
}

void KoopaJr::control() {
    NPCActor::control();
    turnToPlayer();
}

void KoopaJr::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
}

void KoopaJr::startShipBattleTalk() {
    if (mIsShipBattleTalk) {
        return;
    }

    mIsShipBattleTalk = true;

    MR::tryTalkTimeKeepDemo(mMsgCtrl);
    setNerve(GET_NERVE(KoopaJr, HostTypeShipBattleDemoTalkStart));
}

void KoopaJr::exeWait() {
    if (MR::tryStartReaction(this)) {
        if (!mIsJetTurtleHit) {
            MR::startSound(this, "SE_OJ_STAR_PIECE_HIT_ENM_F");
        }

        setNerve(GET_NERVE(KoopaJr, HostTypeReaction));
    } else {
        MR::tryStartTurnAction(this);
    }
}

void KoopaJr::exeReaction() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Damage");
        MR::startSound(this, "SE_BV_KOOPAJR_DAMAGE_S");

        if (!mIsInvalidAppearStarPiece) {
            if (MR::appearStarPiece(this, mPosition, 20, 10.0f, 40.0f, false)) {
                MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
            }
        }
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(KoopaJr, HostTypeReactionEnd));
    }
}

void KoopaJr::exeReactionEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Recovery");
    }

    if (MR::isBckStopped(this)) {
        setNerve(mWaitNerve);
    }
}

void KoopaJr::exeShipBattleAppear() {
    if (MR::isFirstStep(this)) {
        if (MR::isEqualStageName("TriLegLv1Galaxy")) {
            MR::startBck(this, "RoboAppearDemo");
        } else {
            MR::startBck(this, "AppearDemo");
        }
    }

    if (MR::isBckStopped(this)) {
        setNerve(mWaitNerve);
    }
}

void KoopaJr::exeShipBattlePowerUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "AngryDemo");
    }

    if (MR::isBckStopped(this)) {
        mParam.setSingleAction("WaitAngry");
        setNerve(mWaitNerve);
    }
}

void KoopaJr::exeShipBattleEscape() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "RunAwayDemo");
    }

    if (MR::isBckStopped(this)) {
        setNerve(mWaitNerve);
    }
}

void KoopaJr::exeShipBattleShipDamage() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Stumble");
    }

    if (MR::isBckStopped(this)) {
        setNerve(mWaitNerve);
    }
}

void KoopaJr::exeShipBattleDemoTalkStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Talk");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(KoopaJr, HostTypeShipBattleDemoTalkWait));
    }
}

void KoopaJr::exeShipBattleDemoTalkWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "TalkWait");
    }
}

