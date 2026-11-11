#include "Game/NPC/TicoStarRing.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/TicoGalaxy.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvTicoStarRing {
    NEW_NERVE(TicoStarRingNrvWait, TicoStarRing, Wait);
};  // namespace NrvTicoStarRing

TicoStarRing::TicoStarRing(const char* pName) : Tico(pName), mGalaxyName(), mAppearActor(), mTicoGalaxy() {
    lumacore::setClass(this, lumacore::kClassId< TicoStarRing >);
}

void TicoStarRing::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        initBase(rIter, 2);
    } else {
        initBase(5);
    }

    initMessage("AstroGalaxy_TicoCommon000");

    if (MR::isValidInfo(rIter)) {
        s32 challengeGalaxy = 0;
        MR::getPlacementArg0NoInit(rIter, &challengeGalaxy);

        switch (challengeGalaxy) {
        case 0:
            mGalaxyName = "TamakoroExLv2Galaxy";
            break;
        case 1:
            mGalaxyName = "SurfingLv2Galaxy";
            break;
        case 2:
            mGalaxyName = "CubeBubbleExLv2Galaxy";
            break;
        case 3:
            mGalaxyName = "PeachCastleFinalGalaxy";
            break;
        }

        MR::setMessageArgToCurrentGalaxyName(mMsgCtrl, mGalaxyName);

        if (!MR::isAppearGalaxy(mGalaxyName)) {
            makeActorDead();
        }
    } else {
        mGalaxyName = "unknown";
    }

    setNerve(GET_NERVE(TicoStarRing, TicoStarRingNrvWait));
}

void TicoStarRing::kill() {
    NPCActor::kill();

    if (mAppearActor != nullptr) {
        mAppearActor->appear();
    }

    if (mTicoGalaxy != nullptr) {
        if (MR::isValidSwitchA(mTicoGalaxy)) {
            MR::onSwitchA(mTicoGalaxy);
        }
    }
}

void TicoStarRing::initialize(LiveActor* pTicoGalaxy, LiveActor* pAppearActor, const char* pGalaxyName) {
    mTicoGalaxy = pTicoGalaxy;
    mAppearActor = pAppearActor;
    mGalaxyName = pGalaxyName;
    setBaseMtx(pTicoGalaxy->getBaseMtx());
    mPosition.set(pAppearActor != nullptr ? pAppearActor->mPosition : pTicoGalaxy->mPosition);
    makeActorDead();
    MR::setMessageArgToCurrentGalaxyName(mMsgCtrl, pGalaxyName);
}

void TicoStarRing::exeWait() {
    if (!tryReaction() && MR::tryTalkNearPlayerAtEndAndStartTalkAction(this)) {
        if (MR::tryTalkSelectLeft(mMsgCtrl)) {
            setNerveMeta();
        }
    }
}
