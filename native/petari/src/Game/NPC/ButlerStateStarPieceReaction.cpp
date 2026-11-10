#include "Game/NPC/ButlerStateStarPieceReaction.hpp"
#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ButlerStateStarPieceReaction_f1, "ButlerStateStarPieceReaction.f1", 180.0f);

namespace NrvButlerStateStarPieceReaction {
    NEW_NERVE(ButlerStateStarPieceReactionNrvWait, ButlerStateStarPieceReaction, Wait);
};  // namespace NrvButlerStateStarPieceReaction

ButlerStateStarPieceReaction::ButlerStateStarPieceReaction(LiveActor* pHost, const TableIter& rIter, const char* pName)
    : ActorStateBase< LiveActor >("バトラースターピース反応", pHost), mTalkMessage(nullptr), mIsTalkForce(false) {
    mTalkMessage = MR::createTalkCtrlDirectOnRootNodeAutomatic(pHost, rIter, pName, Vec3f(0.0f, cNum_ButlerStateStarPieceReaction_f1, 0.0f), nullptr);
}

void ButlerStateStarPieceReaction::init() {
    initNerve(GET_NERVE(ButlerStateStarPieceReaction, ButlerStateStarPieceReactionNrvWait));
}

void ButlerStateStarPieceReaction::appear() {
    mIsTalkForce = true;
    mIsDead = false;

    setNerve(GET_NERVE(ButlerStateStarPieceReaction, ButlerStateStarPieceReactionNrvWait));
}

void ButlerStateStarPieceReaction::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mHost, "StarPieceReaction");
        MR::limitedStarPieceHitSound();
        MR::startSound(mHost, "SE_SM_BUTLER_ABSORB");
    }

    if (mIsTalkForce) {
        MR::tryTalkForce(mTalkMessage);
    }

    if (MR::isBckStopped(mHost)) {
        kill();
    }
}
