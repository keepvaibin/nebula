#include "Game/NPC/HoneyQueen.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeItch_f1, "exeItch.f1", 0.2f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeReady_isNearPlayer_1, "exeReady.isNearPlayer.1", -1.0f);

namespace {
    NEBULA_TUNABLE(f32, sTalkBalloonOffset, 950.0f);

    struct CollisionPartInfo {
        const char* mCollisionName;
        const char* mJointName;
    };

    const CollisionPartInfo sCollisionPartInfo[] = {{"Center", "Center"}, {"CenterFur", "Center"}, {"Face", "Face"},
                                                    {"LArm01", "LArm01"}, {"LArm02", "LArm02"},    {"LFoot001", "LFoot001"},
                                                    {"RArm01", "RArm01"}, {"RArm02", "RArm02"},    {"RFoot001", "RFoot001"},
                                                    {"Tactile", "Tactile"}};
};  // namespace

namespace NrvHoneyQueen {
    NEW_NERVE(HoneyQueenNrvWait, HoneyQueen, Wait);
    NEW_NERVE(HoneyQueenNrvReady, HoneyQueen, Ready);
    NEW_NERVE(HoneyQueenNrvDemo, HoneyQueen, Demo);
    NEW_NERVE(HoneyQueenNrvFade, HoneyQueen, Fade);
    NEW_NERVE(HoneyQueenNrvTalk, HoneyQueen, Talk);
    NEW_NERVE(HoneyQueenNrvItch, HoneyQueen, Itch);
    NEW_NERVE(HoneyQueenNrvEvent, HoneyQueen, Event);
    NEW_NERVE(HoneyQueenNrvAfter, HoneyQueen, After);
};  // namespace NrvHoneyQueen

HoneyQueen::HoneyQueen(const char* pName) : NPCActor(pName), mWing() {
    lumacore::setClass(this, lumacore::kClassId< HoneyQueen >);
}

void HoneyQueen::init(const TableIter& rIter) {
    NPCActorCaps caps = "HoneyQueen";
    caps.setDefault();
    caps.mWaitNerve = GET_NERVE(HoneyQueen, HoneyQueenNrvWait);
    caps.mUseShadow = true;
    caps.mMessageOffset.x = 0.0f;
    caps.mMessageOffset.y = ::sTalkBalloonOffset;
    caps.mMessageOffset.z = 0.0f;
    caps.mSensorJoint = "Center";
    caps.mMakeActor = 0;
    caps.mSensorMax = 2;
    NPCActor::initialize(rIter, caps);

    static_assert(ARRAY_SIZEU(::sCollisionPartInfo) == ARRAY_SIZEU(mCollisionParts));
    for (s32 i = 0; i < ARRAY_SIZE(mCollisionParts); i++) {
        mCollisionParts[i] = MR::createCollisionPartsFromLiveActor(this, ::sCollisionPartInfo[i].mCollisionName, getSensor("Body"),
                                                                   MR::getJointMtx(this, ::sCollisionPartInfo[i].mJointName),
                                                                   MR::CollisionScaleType_Default);
    }

    MR::excludeCalcShadowToActorAll(this, this);

    mWing = MR::createModelObjNpc("羽", "HoneyQueenWing", MR::getJointMtx(this, "Center"));
    mWing->makeActorAppeared();
    MR::startBck(mWing, "HoneyQueenWing");
    MR::startBtk(mWing, "HoneyQueenWing");

    for (s32 i = 0; i < ARRAY_SIZE(mCollisionParts); i++) {
        MR::validateCollisionParts(mCollisionParts[i]);
    }

    const bool isDemoCast = MR::tryRegisterDemoCast(this, rIter);
    if (isDemoCast) {
        MR::registerDemoActionFunctor(this, MR::Functor(this, &HoneyQueen::fadeOut), "フェードアウト");
        MR::registerDemoActionFunctor(this, MR::Functor(this, &HoneyQueen::fadeIn), "フェードイン");
        MR::registerDemoActionFunctor(this, MR::Functor(this, &HoneyQueen::talkEntry), "謁見");
        MR::tryRegisterDemoCast(mWing, rIter);
    }

    if (isDemoCast) {
        setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvDemo));
        MR::listenStageSwitchOnA(this, MR::Functor(this, &HoneyQueen::switchFunc));
        MR::needStageSwitchWriteB(this, rIter);
    } else {
        MR::useStageSwitchWriteA(this, rIter);
    }

    mLodCtrl->setFarClipping(-1.0f);
    mLodCtrl->setClippingTypeSphereContainsModelBoundingBox(cNum_init_f1);

    MR::setDistanceToTalk(mMsgCtrl, 0.0f);

    MR::initFur(this);

    makeActorAppeared();
}

void HoneyQueen::control() {
    NPCActor::control();
    MR::startLevelSound(this, "SE_SM_LV_HONEYQUEEN_FLY");
}

void HoneyQueen::calcAnim() {
    LiveActor::calcAnim();

    for (s32 i = 0; i < ARRAY_SIZE(mCollisionParts); i++) {
        mCollisionParts[i]->setMtx();
    }
}

void HoneyQueen::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isSensor(pSender, "TouchJump")) {
            MR::sendMsgTouchJump(pReceiver, pSender);
        }
    }
}

bool HoneyQueen::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        return false;
    }

    return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

void HoneyQueen::fadeOut() {
    MR::setPlayerStateWait();
    MR::closeWipeFade();
}

void HoneyQueen::fadeIn() {
    MR::openWipeFade();
    setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvFade));
}

void HoneyQueen::talkEntry() {
    MR::tryStartBckAndBtp(this, "Talk", nullptr);
    MR::tryTalkTimeKeepDemoMarioPuppetable(mMsgCtrl);
    setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvTalk));
}

void HoneyQueen::switchFunc() {
    setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvEvent));
}

void HoneyQueen::exeWait() {
    if (MR::tryTalkNearPlayerAtEnd(mMsgCtrl)) {
        setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvReady));
    }

    if (MR::isTalkTalking(mMsgCtrl)) {
        MR::tryStartBckAndBtp(this, "Talk", nullptr);
    } else {
        MR::tryStartBckAndBtp(this, "Wait", nullptr);
    }
}

void HoneyQueen::exeReady() {
    if (!MR::isNearPlayer(mMsgCtrl, cNum_exeReady_isNearPlayer_1)) {
        setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvWait));
    }
}

void HoneyQueen::exeDemo() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBckAndBtp(this, "Wait", nullptr);
    }
}

void HoneyQueen::exeFade() {
    NPCActor::turnToPlayer();
}

void HoneyQueen::exeTalk() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBckAndBtp(this, "Talk", nullptr);
    }

    if (MR::isDemoLastStep()) {
        setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvItch));
    }
}

void HoneyQueen::exeItch() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBckAndBtp(this, "EventWait", nullptr);
    }

    Vec3f vec;
    mInitPoseQuat.getZDir(vec);
    MR::faceToVector(&mPoseQuat, vec, cNum_exeItch_f1);
    MR::tryTalkNearPlayer(mMsgCtrl);
}

void HoneyQueen::exeEvent() {
    if (MR::tryTalkForceAtEnd(mMsgCtrl)) {
        MR::onSwitchB(this);
        setNerve(GET_NERVE(HoneyQueen, HoneyQueenNrvAfter));
    }
}

void HoneyQueen::exeAfter() {
    MR::tryTalkNearPlayer(mMsgCtrl);
}
