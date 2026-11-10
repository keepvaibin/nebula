#include <nebula_game_api.h>
#include "Game/NPC/KinopioAstro.hpp"
#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Screen/LuigiLetter.hpp"
#include "Game/Screen/PeachLetter.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createLetterIcon_f1, "createLetterIcon.f1", 122.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startDemo_f1, "startDemo.f1", 180.0f);

namespace {
    NEBULA_TUNABLE(s32, sMessangerOneUpNum, 5);
    NEBULA_TUNABLE(s32, sMessangerOneUpExNum, 20);
    // The messenger always gives the larger supply; the game: only for Luigi.
    NEBULA_TUNABLE(bool, cMessangerAlwaysEx, false);
    NEBULA_TUNABLE(f32, sMessangerTalkDistance, 3000.0f);
    NEBULA_TUNABLE(f32, sGotMailSePlayAreaRadius, 1500.0f);
};  // namespace

KinopioAstro::KinopioAstro(const char* pName)
    : Kinopio(pName), mLuigiLetter(), mPeachLetter(), mLetterIconTalkCtrl(), mHasPeachLetter(), mHasLuigiLetter(), mIsPlayerLuigi() {
}

bool KinopioAstro::dispLetter(LayoutActor* pLayout) const {
    if (pLayout == nullptr) {
        return true;
    }

    if (MR::isDead(pLayout)) {
        pLayout->appear();
    }

    pLayout->movement();
    return MR::isDead(pLayout);
}

bool KinopioAstro::isDispLetterIcon() const {
    return mHasLuigiLetter || mHasPeachLetter;
}

void KinopioAstro::createLetterIcon(const TableIter& rIter) {
    mLetterIconTalkCtrl = MR::createTalkCtrlDirect(this, rIter, "Common_Kinopio000", Vec3f(0.0f, cNum_createLetterIcon_f1, 0.0f), nullptr);
    MR::setDistanceToTalk(mLetterIconTalkCtrl, ::sMessangerTalkDistance);
}

bool KinopioAstro::branchFunc(u32) {
    return true;
}

bool KinopioAstro::eventFunc(u32 letterEvent) {
    if (letterEvent == LetterEvent_LuigiLetter) {
        if (dispLetter(mLuigiLetter)) {
            mHasLuigiLetter = false;
            MR::onGameEventFlagGetLuigiLetter();
            return true;
        }

        return false;
    } else if (letterEvent == LetterEvent_PeachLetterNormal) {
        if (dispLetter(mPeachLetter)) {
            mHasPeachLetter = false;
            MR::offAllPlayerLeftSupply();
            return true;
        }

        return false;
    } else if (letterEvent == LetterEvent_PeachLetter1Ups) {
        if (mIsPlayerLuigi) {
            for (s32 i = 0; i < ::sMessangerOneUpExNum; i++) {
                MR::incPlayerLeft();
            }

            MR::getGameSceneLayoutHolder()->requestOneUp(::sMessangerOneUpExNum);
            MR::startSystemSE("SE_SY_5UP");
        } else {
            for (s32 i = 0; i < ::sMessangerOneUpNum; i++) {
                MR::incPlayerLeft();
            }

            MR::getGameSceneLayoutHolder()->requestOneUp(::sMessangerOneUpNum);
            MR::startSystemSE("SE_SY_5UP");
        }

        return true;
    }

    return true;
}

void KinopioAstro::startDemo() {
    MR::moveCoordAndTransToRailStartPoint(this);
    tryPushNullNerve();
    turnToPlayer(cNum_startDemo_f1);
}

void KinopioAstro::endDemo() {
    popNerve();
}

void KinopioAstro::init(const TableIter& rIter) {
    Kinopio::init(rIter);
    if (mMsgCtrl != nullptr) {
        MR::registerEventFunc(getMsgCtrl(), TalkMessageFunc(this, &KinopioAstro::eventFunc));
        MR::registerBranchFunc(getMsgCtrl(), TalkMessageFunc(this, &KinopioAstro::branchFunc));
    }

    if (MR::isEqualStageName("PeachCastleFinalGalaxy")) {
        // No message board (B-31, Q5), no letter from the staff.
        return;
    }

    if (!MR::isKinopioExplorerRescued()) {
        makeActorDead();
    }

    if (mObjArg0 == 2) {
        if (MR::isDemoCast(this, "ルイージ失踪デモ")) {
            Vec3f followOffset(MR::getMessageBalloonFollowOffset(mMsgCtrl));
            TalkMessageCtrl* pMsgCtrl = MR::createTalkCtrlDirectOnRootNodeAutomatic(this, rIter, "AstroGalaxy_Kinopio100", followOffset, nullptr);
            MR::registerEventFunc(pMsgCtrl, TalkMessageFunc(this, &KinopioAstro::eventFunc));
            DemoFunction::registerDemoTalkMessageCtrlDirect(this, pMsgCtrl, "ルイージ失踪デモ");
            MR::registerDemoActionFunctor(this, MR::Functor(this, &KinopioAstro::startDemo), "開始");
            MR::registerDemoActionFunctor(this, MR::Functor(this, &KinopioAstro::endDemo), "終了");
        }

        if (MR::isAnyPlayerLeftSupply()) {
            mPeachLetter = new PeachLetter("ピーチ姫からの手紙");
            mPeachLetter->initWithoutIter();
            mHasPeachLetter = true;
            if (MR::isLuigiLeftSupply()) {
                mIsPlayerLuigi = true;
            }
            if (::cMessangerAlwaysEx) {
                mIsPlayerLuigi = true;
            }

            if (mIsPlayerLuigi) {
                MR::setMessageArg(mMsgCtrl, ::sMessangerOneUpExNum);
            } else {
                MR::setMessageArg(mMsgCtrl, ::sMessangerOneUpNum);
            }
        }

        if (MR::isLuigiLetterArrivalAtMessenger()) {
            mLuigiLetter = MR::createLuigiLetterForTalk();
            mLuigiLetter->initWithoutIter();
            mHasLuigiLetter = true;
        }

        if (mGoodsIndex != 3) {
            NPCActorItem npcItems("Kinopio");
            MR::getNPCItemData(&npcItems, 3);
            mGoods0 = MR::createNPCGoods(this, npcItems.mGoods0, npcItems.mGoodsJoint0);
            mGoods1 = MR::createNPCGoods(this, npcItems.mGoods1, npcItems.mGoodsJoint1);
        }

        if (mGoods0 != nullptr) {
            MR::registerDemoSimpleCastAll(mGoods0);
        }

        if (mGoods1 != nullptr) {
            MR::registerDemoSimpleCastAll(mGoods1);
        }
    }

    if (mHasPeachLetter || mHasLuigiLetter) {
        createLetterIcon(rIter);
    }

    AstroDemoFunction::tryRegisterDemoForLuigiAndKinopio(this, rIter);
}

void KinopioAstro::control() {
    if (isDispLetterIcon() && MR::calcCameraDistanceZ(mPosition) < ::sMessangerTalkDistance && MR::tryTalkNearPlayer(mLetterIconTalkCtrl) &&
        MR::calcDistanceToPlayer(this) <= ::sGotMailSePlayAreaRadius) {
        MR::startLevelSound(this, "SE_SM_LV_KINOMES_GOT_MAIL");
    }

    NPCActor::control();
}
