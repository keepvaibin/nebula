#include "Game/Screen/InformationObserver.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

struct InformationObserverAttribute {
    const char* mMessageId;
    const char* mFirstMorphBgmName;
    const char* mBgmName;
};

static InformationObserverAttribute sAttr[] = {
    {"InformationObserverBee", "BGM_FIRST_MORPH_B", nullptr},
    {"InformationObserverTeresa", "BGM_FIRST_MORPH_B", nullptr},
    {"InformationObserverHopper", "BGM_FIRST_MORPH_B", nullptr},
    {"InformationObserverFire", "BGM_FIRST_MORPH_A", "BGM_FIRE_B"},
    {"InformationObserverIce", "BGM_FIRST_MORPH_A", "BGM_ICE_B"},
    {"InformationObserverFlying", "BGM_FIRST_MORPH_A", "BGM_FLYING_B"},
    {"InformationObserverInvincible", "BGM_FIRST_MORPH_A", "BGM_MUTEKI_B"},
    {"InformationObserverLifeUpMario", nullptr, nullptr},
    {"InformationObserverOneUp", nullptr, nullptr},
    {"InformationObserverSpin", nullptr, nullptr},
};

namespace {
    static s32 sDisplayFramesMin = NebulaFrameClock::stepsOfMilliseconds(500);
};  // namespace

namespace {
    InformationObserver* getInformationObserver() {
        return MR::getSceneObj< InformationObserver >(SceneObj_InformationObserver);
    }
};  // namespace

namespace NrvInformationObserver {
    NEW_NERVE(InformationObserverNrvWait, InformationObserver, Wait);
    NEW_NERVE(InformationObserverNrvDisp, InformationObserver, Disp);
};  // namespace NrvInformationObserver

InformationObserver::InformationObserver() : LiveActor("初出監視"), mType(Type_Bee), mIsTimeKeepDemo(false), mDisplayFrame(-1) {
    lumacore::setClass(this, lumacore::kClassId< InformationObserver >);
}

void InformationObserver::init(const TableIter& /*rIter*/) {
    MR::connectToSceneLayoutMovement(this);
    MR::declareEventCameraProgrammable("初出監視カメラ");
    initNerve(GET_NERVE(InformationObserver, InformationObserverNrvWait));
    MR::invalidateClipping(this);
    makeActorDead();
}

void InformationObserver::entry(TYPE type, LiveActor* pParam2) {
    mDemoActor = pParam2;
    mType = type;
    mIsTimeKeepDemo = MR::isTimeKeepDemoActive();

    if (mIsTimeKeepDemo) {
        MR::pauseTimeKeepDemo(mDemoActor);
        setNerve(GET_NERVE(InformationObserver, InformationObserverNrvDisp));
    } else {
        MR::requestStartDemoWithoutCinemaFrame(this, "初出表示", GET_NERVE(InformationObserver, InformationObserverNrvDisp),
                                               GET_NERVE(InformationObserver, InformationObserverNrvWait));
    }

    makeActorAppeared();
}

void InformationObserver::exeWait() {
}

void InformationObserver::exeDisp() {
    if (MR::isFirstStep(this)) {
        if (mType == Type_LifeUp && MR::isPlayerLuigi()) {
            MR::appearInformationMessage(MR::getLayoutMessageDirect("InformationObserverLifeUpLuigi"), true);
        } else {
            MR::appearInformationMessage(MR::getLayoutMessageDirect(sAttr[mType].mMessageId), true);
        }

        if (sAttr[mType].mFirstMorphBgmName != nullptr) {
            MR::stopSubBGM(0);
            MR::startSubBGM(sAttr[mType].mFirstMorphBgmName, false);
        }

        mDisplayFrame = ::sDisplayFramesMin;
    }

    if (mDisplayFrame >= 0) {
        mDisplayFrame--;
    }

    if (!MR::isPressedJump(PLAYER_1) || mDisplayFrame >= 0) {
        return;
    }

    if (mIsTimeKeepDemo) {
        MR::resumeTimeKeepDemo(mDemoActor);
    } else {
        MR::endDemo(this, "初出表示");
    }

    if (sAttr[mType].mBgmName != nullptr) {
        if (mType == Type_Flying) {
            MR::stopSubBGM(5);
            MR::startStageBGM(sAttr[mType].mBgmName, false);
        } else {
            MR::startSubBGM(sAttr[mType].mBgmName, false);
        }
    }

    MR::startSystemSE("SE_SY_TALK_OK");
    MR::disappearInformationMessage();

    switch (mType) {
    case Type_Bee:
        MR::onGameEventFlagBeeMarioAtFirst();
        break;
    case Type_Teresa:
        MR::onGameEventFlagTeresaMarioAtFirst();
        break;
    case Type_Hopper:
        MR::onGameEventFlagHopperMarioAtFirst();
        break;
    case Type_Fire:
        MR::onGameEventFlagFireMarioAtFirst();
        break;
    case Type_Ice:
        MR::onGameEventFlagIceMarioAtFirst();
        break;
    case Type_Flying:
        MR::onGameEventFlagFlyingMarioAtFirst();
        break;
    case Type_Invincible:
        MR::onGameEventFlagInvincibleMarioAtFirst();
        break;
    case Type_LifeUp:
        MR::onGameEventFlagLifeUpAtFirst();
        break;
    case Type_OneUp:
        MR::onGameEventFlagOneUpAtFirst();
        break;
    case Type_Spin:
        MR::onGameEventFlagEnableToSpinAndStarPointer();
        break;
    }

    makeActorDead();
}

namespace InformationObserverFunction {
    void explainBee() {
        ::getInformationObserver()->entry(InformationObserver::Type_Bee, nullptr);
    }

    void explainTeresa() {
        ::getInformationObserver()->entry(InformationObserver::Type_Teresa, nullptr);
    }

    void explainHopper() {
        ::getInformationObserver()->entry(InformationObserver::Type_Hopper, nullptr);
    }

    void explainFire() {
        ::getInformationObserver()->entry(InformationObserver::Type_Fire, nullptr);
    }

    void explainIce() {
        ::getInformationObserver()->entry(InformationObserver::Type_Ice, nullptr);
    }

    void explainFlying() {
        ::getInformationObserver()->entry(InformationObserver::Type_Flying, nullptr);
    }

    void explainInvincible() {
        ::getInformationObserver()->entry(InformationObserver::Type_Invincible, nullptr);
    }

    void explainLifeUp() {
        ::getInformationObserver()->entry(InformationObserver::Type_LifeUp, nullptr);
    }

    void explainOneUp() {
        ::getInformationObserver()->entry(InformationObserver::Type_OneUp, nullptr);
    }

    void explainSpin(LiveActor* pParam1) {
        ::getInformationObserver()->entry(InformationObserver::Type_Spin, pParam1);
    }
};  // namespace InformationObserverFunction
