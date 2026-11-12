#include "Game/NPC/TicoComet.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Screen/GalaxyMapController.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace NrvTicoEat {
    NEW_NERVE(TicoEatNrvEatPre, TicoEat, EatPre);
    NEW_NERVE(TicoEatNrvEatNow, TicoEat, EatNow);
    NEW_NERVE(TicoEatNrvEatPst, TicoEat, EatPst);
    NEW_NERVE(TicoEatNrvEatEnd, TicoEat, EatEnd);
    NEW_NERVE(TicoEatNrvReaction, TicoEat, Reaction);
};  // namespace NrvTicoEat

namespace NrvTicoComet {
    NEW_NERVE(TicoCometNrvDemoAnim, TicoComet, DemoAnim);
    NEW_NERVE(TicoCometNrvDemoFade, TicoComet, DemoFade);
    NEW_NERVE(TicoCometNrvDemoEnd, TicoComet, DemoEnd);
};  // namespace NrvTicoComet

namespace {
    class TicoEatParam : public AnimScaleParam {
    public:
        TicoEatParam();
    };

    TicoEatParam::TicoEatParam() : AnimScaleParam() {
        mHitReactionVelocity = 0.12f;
        mHitReactionSpring = 0.12f;
        mHitReactionDamping = 0.9f;
    }

    static TicoEatParam sParam;
};  // namespace

TicoEat::TicoEat(const char* pName) : Tico(pName) {
    lumacore::setClass(this, lumacore::kClassId< TicoEat >);
    mStarPieceNeed = 0;
    mStarPieceLeft = 0;
}

void TicoEat::init(const TableIter& /*rIter*/) {
    ::sParam = TicoEatParam();  // again, with a mod's numbers
    mScaleController = new AnimScaleController(&::sParam);
    mReactionNerve = GET_NERVE(TicoEat, TicoEatNrvReaction);
    mLightJointMtx = MR::getJointMtx(this, "Center");
    mParam.setMoveAction("Wait", "Wait");
    mParam.setTalkAction("Talk", "Talk");
    mParam.mNearDistance = 3000.0f;
    setDefaults();
    mSwingReactDist = 1000.0f;
}

void TicoEat::control() {
    if (MR::isBckPlaying(this, "Joy2")) {
        MR::startLevelSound(this, "SE_SM_LV_TICOFAT_GLAD", 0);
    }

    Tico::control();
}

bool TicoEat::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceGift(msg)) {
        if (mStarPieceLeft == mStarPieceNeed) {
            MR::startAction(this, "Eat0");
            MR::startBva(this, "Big0");
        }

        // At two thirds and one third left the Tico grows a size, keeping its eating frame.
        auto growAt = [&](f32 leftRate, const char* pAction, const char* pBva) {
            if (mStarPieceLeft == (s32)(leftRate * mStarPieceNeed)) {
                f32 frame = MR::getBckFrame(this);
                MR::startAction(this, pAction);
                MR::startBva(this, pBva);
                MR::setBckFrame(this, frame);
                mScaleController->startHitReaction();
            }
        };
        growAt(0.66f, "Eat1", "Big1");
        growAt(0.33f, "Eat2", "Big2");

        mStarPieceLeft--;
        return true;
    }

    return Tico::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

void TicoEat::initStarPiece(s32 num) {
    mStarPieceNeed = num;
    mStarPieceLeft = num;
    MR::declareStarPieceReceiver(this, num);
}

bool TicoEat::tryEat() {
    if (isEmptyNerve()) {
        pushNerve(GET_NERVE(TicoEat, TicoEatNrvEatPre));
    }

    if (isNerve(GET_NERVE(TicoEat, TicoEatNrvEatEnd))) {
        popNerve();
        return true;
    }

    return false;
}

bool TicoEat::hasEnoughStarPiece() const {
    return MR::getStarPieceNum() >= mStarPieceNeed;
}

bool TicoEat::eventFunc(u32 event) {
    if (event == 0) {
        return tryEat();
    }

    if (event == 1) {
        MR::forceAppearStarPieceCounterForTicoFat();
    }

    if (event == 2) {
        MR::disappearStarPieceCounterForTicoFat();
    }

    return true;
}

void TicoEat::exeReaction() {
    if (MR::isFirstStep(this) && (mIsTrampledStart || mIsReactionStart)) {
        mScaleController->startHitReaction();
    }

    startReactionSound();

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

void TicoEat::exeEatPre() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Excite");
        MR::startBva(this, "Big0");
    }

    if (MR::turnQuatZDirRad(&mPoseQuat, mPoseQuat, -MR::getCamZdir(), MR::toRadian(2.0f))) {
        popAndPushNerve(GET_NERVE(TicoEat, TicoEatNrvEatNow));
    }
}

void TicoEat::exeEatNow() {

    if (mStarPieceLeft != 0 && MR::isIntervalStep(this, 100 / mStarPieceNeed)) {
        MR::giftStarPieceToTarget(getSensor("Mouth"), 1);
    }

    MR::startLevelSound(this, "SE_SM_LV_TICOFAT_ABSORB");
    MR::startLevelSound(this, "SE_SM_LV_TICOFAT_EATING");
    MR::startLevelSound(this, "SE_SM_LV_TICOFAT_EATING", 0);

    if (mStarPieceLeft == 0) {
        popAndPushNerve(GET_NERVE(TicoEat, TicoEatNrvEatPst));
    }
}

void TicoEat::exeEatPst() {
    if (MR::isGreaterEqualStep(this, 4)) {
        mParam.mTalkAction = "Joy2";
        mParam.mTalkTurnAction = "Joy2";
        popAndPushNerve(GET_NERVE(TicoEat, TicoEatNrvEatEnd));
    }
}

void TicoEat::exeEatEnd() {
}

TicoComet::TicoComet(const char* pName) : TicoEat(pName) {
    lumacore::setClass(this, lumacore::kClassId< TicoComet >);
}

void TicoComet::init(const TableIter& rIter) {
    NPCActorCaps caps("TicoComet");
    NPCActorItem item("TicoComet");
    caps.setDefault();
    caps.mSensorSize = 100.0f;
    caps.mSensorOffset.y = 100.0f;
    caps.mUseShadow = true;
    caps.mSoundSize = 6;
    caps.mSensorMax = 2;
    caps.mSensorJoint = "Center";
    caps.mPointerJoint = "Center";
    caps.mBinder = 0;
    initialize(rIter, caps);
    MR::addHitSensorAtJointEye(this, "Mouth", "Mouth", 8, 30.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::getNPCItemData(&item, 0);
    equipment(item, true);
    MR::startAction(mGoods0, "LeftRotate");
    MR::startAction(mGoods1, "RightRotate");
    MR::startBtk(this, "TicoComet");
    mStarPieceNeed = 20;
    mStarPieceLeft = 20;
    MR::declareStarPieceReceiver(this, 20);
    MR::setMessageArg(mMsgCtrl, mStarPieceNeed);
    MR::registerBranchFunc(getMsgCtrl(), TalkMessageFunc(this, &TicoComet::branchFunc));
    MR::registerEventFunc(getMsgCtrl(), TalkMessageFunc(this, &TicoComet::eventFunc));
    MR::setDistanceToTalk(mMsgCtrl, 350.0f);
    if (!MR::isRosettaTalkTrickComet()) {
        makeActorDead();
    }

    AstroDemoFunction::tryRegisterSimpleCastIfAstroGalaxy(this);
    MR::startBva(this, "Small0");
    MR::startBrk(this, "Normal");
    MR::setBrkFrameAndStop(this, 0.0f);
    TicoEat::init(rIter);
    mLightColor.set(0xC8, 0, 0xFF, 0xFF);
}

bool TicoComet::branchFunc(u32 /*val*/) {
    return MR::getStarPieceNum() >= mStarPieceNeed;
}

bool TicoComet::tryDemo() {
    if (isEmptyNerve()) {
        pushNerve(GET_NERVE(TicoComet, TicoCometNrvDemoAnim));
    }

    if (isNerve(GET_NERVE(TicoComet, TicoCometNrvDemoEnd))) {
        popNerve();
        return true;
    }

    return false;
}

bool TicoComet::eventFunc(u32 event) {
    if (event < 3) {
        return TicoEat::eventFunc(event);
    }

    if (event == 3) {
        return tryDemo();
    }

    return true;
}

void TicoComet::startReactionSound() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_TICOCOMET_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startPointerHitSound();
        MR::startSound(this, "SE_SV_TICOCOMET_POINT");
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SM_TICOCOMET_SPIN");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_TICOCOMET_STAR_PIECE_HIT");
    }
}

void TicoComet::exeDemoAnim() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Demo");
        MR::startSound(this, "SE_SM_TICOFAT_META_ITEM");
        MR::startSound(this, "SE_SM_TICOFAT_META");
    }

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");
        MR::startSystemSE("SE_DM_TICOFAT_MORPH_WIPE_IN");
        popAndPushNerve(GET_NERVE(TicoComet, TicoCometNrvDemoFade));
    }
}

void TicoComet::exeDemoFade() {
    if (MR::isFirstStep(this)) {
        MR::closeWipeWhiteFade(30);
        MR::startSystemSE("SE_DM_TICOFAT_MORPH_WIPE_OUT");
    }

    if (!MR::isWipeActive()) {
        mParam.mTalkAction = "Talk";
        mParam.mTalkTurnAction = "Talk";
        MR::forceOpenWipeWhiteFade();
        MR::forceToNextStateGalaxyCometScheduler();
        MR::startGalaxyMapLayoutForTicoComet();
        MR::startBva(this, "Small0");
        MR::startAction(this, "Wait");
        MR::startBckNoInterpole(this, "Wait");
        MR::clearGotCountStarPieceReceiver(this);
        mStarPieceLeft = mStarPieceNeed;
        popAndPushNerve(GET_NERVE(TicoComet, TicoCometNrvDemoEnd));
    }
}

void TicoComet::exeDemoEnd() {
}

void TicoEat::startReactionSound() {
}
