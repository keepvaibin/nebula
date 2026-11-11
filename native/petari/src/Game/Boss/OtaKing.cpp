#include "Game/Boss/OtaKing.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/Boss/OtaKingLongFoot.hpp"
#include "Game/Boss/OtaKingMagma.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/CocoNutBall.hpp"
#include "Game/Enemy/FireBall.hpp"
#include "Game/Enemy/FireBubble.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/CocoNut.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModel_f1, "initModel.f1", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModel_f2, "initModel.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModel_f3, "initModel.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModel_f4, "initModel.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_dirToPlayer_f1, "dirToPlayer.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_dirToPlayer_f2, "dirToPlayer.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_throwCocoNut_f1, "throwCocoNut.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_throwCocoNut_f2, "throwCocoNut.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_throwFireBall_f1, "throwFireBall.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_throwFireBall_f2, "throwFireBall.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearBubble_f1, "appearBubble.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearBubble_f2, "appearBubble.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f1, "initLongFoot.f1", 735.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f2, "initLongFoot.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f3, "initLongFoot.f3", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f4, "initLongFoot.f4", 9.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f5, "initLongFoot.f5", 266.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f6, "initLongFoot.f6", 959.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f7, "initLongFoot.f7", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f8, "initLongFoot.f8", 107.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f9, "initLongFoot.f9", 14.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f10, "initLongFoot.f10", 43.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f11, "initLongFoot.f11", 884.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f12, "initLongFoot.f12", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f13, "initLongFoot.f13", 159.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f14, "initLongFoot.f14", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f15, "initLongFoot.f15", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f16, "initLongFoot.f16", 896.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f17, "initLongFoot.f17", 9.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initLongFoot_f18, "initLongFoot.f18", 14.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWaitOnSwitch_f1, "exeWaitOnSwitch.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePowerUp_f1, "exePowerUp.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearStar_f1, "exeAppearStar.f1", 0.001f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAppearDemo_1, "exeAppearDemo.1", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDamage_1, "exeDamage.1", 2);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDamage_2, "exeDamage.2", 3);
NEBULA_TUNABLE_NAMED(s32, cStep_exePowerUp_1, "exePowerUp.1", 2);
NEBULA_TUNABLE_NAMED(s32, cStep_exePowerUp_2, "exePowerUp.2", 3);
NEBULA_TUNABLE_NAMED(s32, cStep_exePowerUp_3, "exePowerUp.3", 83);
NEBULA_TUNABLE_NAMED(s32, cStep_exePowerUp_4, "exePowerUp.4", 195);
NEBULA_TUNABLE_NAMED(s32, cStep_exePowerUp_5, "exePowerUp.5", 215);
NEBULA_TUNABLE_NAMED(s32, cStep_exePowerUp_6, "exePowerUp.6", 215);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDown_1, "exeDown.1", 2);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDown_2, "exeDown.2", 3);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDownDemo_1, "exeDownDemo.1", 1);


namespace NrvOtaKing {
    NEW_NERVE(OtaKingNrvWaitOnSwitch, OtaKing, WaitOnSwitch);
    NEW_NERVE(OtaKingNrvAppearDemo, OtaKing, AppearDemo);
    NEW_NERVE(OtaKingNrvWait, OtaKing, Wait);
    NEW_NERVE(OtaKingNrvThrowCocoNutWait, OtaKing, ThrowCocoNutWait);
    NEW_NERVE(OtaKingNrvThrowCocoNut, OtaKing, ThrowCocoNut);
    NEW_NERVE(OtaKingNrvThrowFireBallWait, OtaKing, ThrowFireBallWait);
    NEW_NERVE(OtaKingNrvThrowFireBall, OtaKing, ThrowFireBall);
    NEW_NERVE(OtaKingNrvDamage, OtaKing, Damage);
    NEW_NERVE(OtaKingNrvPowerUp, OtaKing, PowerUp);
    NEW_NERVE(OtaKingNrvDown, OtaKing, Down);
    NEW_NERVE(OtaKingNrvDownDemo, OtaKing, DownDemo);
    NEW_NERVE(OtaKingNrvAppearStar, OtaKing, AppearStar);
    NEW_NERVE(OtaKingNrvDead, OtaKing, Dead);
    NEW_NERVE(OtaKingNrvHitBackStart, OtaKing, HitBackStart);
    NEW_NERVE(OtaKingNrvHitBack, OtaKing, HitBack);
    NEW_NERVE(OtaKingNrvNearAttack, OtaKing, NearAttack);
    NEW_NERVE(OtaKingNrvBubbleAttack, OtaKing, BubbleAttack);
    NEW_NERVE(OtaKingNrvWaitStartDemo, OtaKing, WaitStartDemo);
};  // namespace NrvOtaKing

namespace {
    inline void makeTranslationMatrix(Pos3f* pMtx, const Vec3f& rTrans) {
        pMtx->mMtx[0][0] = 1.0f;
        pMtx->mMtx[1][0] = 0.0f;
        pMtx->mMtx[2][0] = 0.0f;

        pMtx->mMtx[0][1] = 0.0f;
        pMtx->mMtx[1][1] = 1.0f;
        pMtx->mMtx[2][1] = 0.0f;

        pMtx->mMtx[0][2] = 0.0f;
        pMtx->mMtx[1][2] = 0.0f;
        pMtx->mMtx[2][2] = 1.0f;

        pMtx->mMtx[0][3] = rTrans.x;
        pMtx->mMtx[1][3] = rTrans.y;
        pMtx->mMtx[2][3] = rTrans.z;
    }

    NEBULA_TUNABLE(s32, cHP, 3);
    const s32 cCocoNutBallNum = 3;
    const s32 cFireBallNum = 6;
    const s32 cFireBubbleNum = 6;
    NEBULA_TUNABLE(f32, cThrowDistance, 4000.0f);
    NEBULA_TUNABLE(s32, cWaitFrame, 75);
    NEBULA_TUNABLE(f32, cValidThrowCocoNutDistance, 500.0f);
    NEBULA_TUNABLE(s32, cThrowCocoNutStep, 70);
    NEBULA_TUNABLE(f32, cThrowAngleMin, 2.5f);
    NEBULA_TUNABLE(f32, cThrowAngleMax, 5.0f);
    NEBULA_TUNABLE(s32, cThrowCocoNutWaitFrame, 45);
    NEBULA_TUNABLE(s32, cThrowFireBallStep, 94);
    NEBULA_TUNABLE(f32, cFireBallThrowNum, 3);
    NEBULA_TUNABLE(f32, cFireBallThrowSpeed, 15.0f);
    NEBULA_TUNABLE(f32, cFireBallThrowAngle, 30.0f);
    const s32 cThrowFireBallWaitFrame = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, cBubbleAppearStep, 100);
    NEBULA_TUNABLE(s32, cBubbleAppearNum, 6);
    NEBULA_TUNABLE(f32, cBubbleAppearRandomAngle, 30.0f);
    NEBULA_TUNABLE(s32, cAppearDemoRippleStepS, 40);
    NEBULA_TUNABLE(s32, cAppearDemoRippleStepL, 190);
    const s32 cCocoNutRippleStep = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 cFireBallRippleStep = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 cDownRippleStep = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(s32, cNearAttackRippleStep, 20);
    NEBULA_TUNABLE(s32, cBubbleAttackRippleStep, 85);
    NEBULA_TUNABLE(s32, cDownSinkStartStep, 80);
    NEBULA_TUNABLE(f32, cBodySensorRadius, 400.0f);
    NEBULA_TUNABLE(f32, cEyeSensorRadius, 1500.0f);
    NEBULA_TUNABLE(f32, cTurnSpeedMax, 1.0f);
    NEBULA_TUNABLE(f32, cTurnSpeedAccel, 0.003f);
    NEBULA_TUNABLE(f32, cPowerStarAppearOffsetY, 100.0f);
    NEBULA_TUNABLE(f32, cStarPieceAppearOffsetY, 300.0f);
    NEBULA_TUNABLE(s32, cStarPieceAppearNum, 8);
    NEBULA_TUNABLE(f32, cStarPieceAppearRange, 60.0f);
    NEBULA_TUNABLE(f32, cStarPieceAppearVelocity, 50.0f);
    NEBULA_TUNABLE(s32, cDamageCrownFallFrame, 73);
    // const s32 cDamageAngryRumbleFrame;
    NEBULA_TUNABLE(f32, cDamageFrontFootClipingHeight, -600.0f);
    NEBULA_TUNABLE(s32, cDownDemoFrame, 300);
    NEBULA_TUNABLE(f32, cDownDemoCocoNutScale, 1.5f);
    NEBULA_TUNABLE(s32, cDownDemoRumbleFrame1, 105);
    NEBULA_TUNABLE(s32, cDownDemoRumbleFrame2, 150);
    NEBULA_TUNABLE(s32, cPowerStarExistBgmStartStep, 200);
    NEBULA_TUNABLE(s32, cAppearSeStep0, 10);
    NEBULA_TUNABLE(s32, cAppearSeStep1, 205);
    NEBULA_TUNABLE(s32, cAppearSeStep2, 230);
    NEBULA_TUNABLE(s32, cAppearSeStep3, 100);
    NEBULA_TUNABLE(s32, cAppearDemoRumblePad01, 50);
    NEBULA_TUNABLE(s32, cAppearDemoRumblePad02, 130);
    NEBULA_TUNABLE(s32, cAppearDemoRumblePad03, 160);
    NEBULA_TUNABLE(s32, cAppearDemoRumblePad04, 200);
    NEBULA_TUNABLE(s32, cAppearDemoRumblePad05, 250);
    NEBULA_TUNABLE(s32, cAppearDemoRumblePad06, 310);
    NEBULA_TUNABLE(s32, cBlurStartStep, 175);
    NEBULA_TUNABLE(s32, cBlurTotalFrame, 70);
    NEBULA_TUNABLE_RECORD(Vec, cLongFootScale, {1.0f, 0.9f, 0.8f});
    NEBULA_TUNABLE_RECORD(Vec, cBubbleAppearVelocity, {0.0f, 37.5f, 15.0f});
    NEBULA_TUNABLE_RECORD(Vec, cSensorOffset, {0.0f, 150.0f, 0.0f});
    NEBULA_TUNABLE_LIST(u32, cLongFootDemoBckStep, , {3, 8, 14, 18});
    NEBULA_TUNABLE_RECORD(Vec, cAppearDemoRotate, {0.0f, 30.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, cDownDemoRotate, {0.0f, 43.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, cDownDemoCocoNutPos, {0.0f, 630.0f, 250.0f});
};  // namespace

OtaKing::OtaKing(const char* pName)
    : LiveActor(pName), mMagma(), mCocoNutBallArray(), mFireBallArray(), mFireBubbleArray(), mThrowPos(), mHits(), mThrowNum(), mTurnSpeed(), mSoundObj(),
      mSoundObjPos(gZeroVec), mBubbleAppearPos(), mDownCocoNut(), mAnimCamera(), mIsLv2(), mScaleController() {
    lumacore::setClass(this, lumacore::kClassId< OtaKing >);
    MR::zeroMemory(mFeet, sizeof(mFeet));
    MR::zeroMemory(mLongFeet, sizeof(mLongFeet));
    mLongFootMtx.identity();
}

void OtaKing::init(const TableIter& rIter) {
    initMapToolInfo(rIter);
    initModel(rIter);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initSensor();
    initEffectKeeper(0, "OtaKing");
    MR::setClippingTypeSphere(this, cNum_init_f1);
    initSound(8, false);
    mSoundObj = new AudAnmSoundObject(&mSoundObjPos, 4, NebulaArena::current());
    mAnimCamera = MR::createActorCameraInfo(rIter);
    MR::initAnimCamera(this, mAnimCamera, "Appear");
    MR::initAnimCamera(this, mAnimCamera, "Down");
    MR::declarePowerStar(this);
    MR::declareStarPiece(this, 24);
    MR::createCenterScreenBlur();

    if (MR::isValidSwitchA(this)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &OtaKing::startAppearDemo));
    }

    MR::tryRegisterDemoCast(this, rIter);
    mScaleController = new AnimScaleController(nullptr);
    initNerve(GET_NERVE(OtaKing, OtaKingNrvWait));
    makeActorAppeared();
}

void OtaKing::movement() {
    LiveActor::movement();
    MR::copyJointPos(this, "Crown", &mSoundObjPos);
    mSoundObj->process();
}

void OtaKing::makeActorAppeared() {
    mHits = 0;
    LiveActor::makeActorAppeared();

    MR::startBrk(this, "Wait");
    MR::startBpk(this, "Wait");

    if (MR::isValidSwitchA(this)) {
        setNerve(GET_NERVE(OtaKing, OtaKingNrvWaitOnSwitch));
    } else {
        setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
    }
}

void OtaKing::startAppearDemo() {
    if (isNerve(GET_NERVE(OtaKing, OtaKingNrvWaitOnSwitch))) {
        invalidateClippingAndStartDemo("出現", GET_NERVE(OtaKing, OtaKingNrvAppearDemo), GET_NERVE(OtaKing, OtaKingNrvWaitStartDemo));
    }
}

void OtaKing::control() {
    if (isNerve(GET_NERVE(OtaKing, OtaKingNrvDown)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvAppearStar)) ||
        isNerve(GET_NERVE(OtaKing, OtaKingNrvDead)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvAppearDemo)) ||
        isNerve(GET_NERVE(OtaKing, OtaKingNrvDownDemo))) {
        mTurnSpeed = 0.0f;
    } else {
        dirToPlayer();
    }

    mScaleController->updateNerve();
}

void OtaKing::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

bool OtaKing::isSensorBodyOrFace(HitSensor* pSensor) {
    return pSensor == getSensor("body") || pSensor == getSensor("face");
}

void OtaKing::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && isSensorBodyOrFace(pSender)) {
        if (isNerveValidNearAttack() && MR::sendMsgEnemyAttackMaximum(pReceiver, pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
            setNerve(GET_NERVE(OtaKing, OtaKingNrvNearAttack));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool OtaKing::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (isSensorBodyOrFace(pReceiver) && MR::isMsgStarPieceReflect(msg)) {
        mScaleController->startHitReaction();
        return true;
    }

    return false;
}

bool OtaKing::receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* pSender, HitSensor* pReceiver) {
    if (!pSender->isType(ATYPE_COCO_NUT)) {
        return false;
    }

    CocoNutBall* rallyBall = static_cast< CocoNutBall* >(pSender->mHost);

    // The rally must outlast the hits taken (at least one on Lv2) before a ball hurts.
    s32 rallyToDamage = mHits;

    if (mIsLv2) {
        rallyToDamage = MR::max(rallyToDamage, 1);
    }

    bool isHitBack = rallyBall->mRallyNum <= rallyToDamage;

    bool isReceiverBody = pReceiver == getSensor("body");

    if (isReceiverBody) {
        if (isHitBack) {
            if (!isNerve(GET_NERVE(OtaKing, OtaKingNrvHitBack))) {
                setNerve(GET_NERVE(OtaKing, OtaKingNrvHitBack));
            }

            rallyBall->hitBackToPlayer();
        } else {
            if (MR::isPlayingStageBgm()) {
                MR::startSystemME("ME_RALLY_COMBO_LAST");
            }

            damage();
            return true;
        }
    } else {
        if (isHitBack && !isNerve(GET_NERVE(OtaKing, OtaKingNrvHitBackStart))) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvHitBackStart));
        }
    }

    return false;
}

bool OtaKing::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    switch (msg)
    case ACTMES_RUSH_END:
        if (pSender->isType(ATYPE_COCO_NUT)) {
            if (!isDamageNerve()) {
                if (isNerve(GET_NERVE(OtaKing, OtaKingNrvWait)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNutWait)) ||
                    isNerve(GET_NERVE(OtaKing, OtaKingNrvHitBackStart))) {
                    if (isValidBubbleAttack()) {
                        setNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
                    } else {
                        mHits > 0 && isValidThrowFireBall() ? setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall)) :
                                                              setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
                    }

                    return true;
                } else if (isNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNut)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvHitBack))) {
                    mThrowNum = 2;
                    return true;
                }
            }
        }

    return false;
}

void OtaKing::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchWriteA(this, rIter);
    MR::useStageSwitchWriteB(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);

    bool isLv2 = false;
    MR::getPlacementArg1NoInit(rIter, &isLv2);
    mIsLv2 = isLv2;
}

void OtaKing::initModel(const TableIter& rIter) {
    initModelManagerWithAnm(mIsLv2 ? "OtaKingLv2" : "OtaKing", nullptr, false);

    for (int i = 0; i < ARRAY_SIZE(mFeet); i++) {
        mFeet[i] = new PartsModel(this, "前足モデル", mIsLv2 ? "OtaKingFootLv2" : "OtaKingFoot", nullptr, MR::DrawBufferType_Enemy, false);
        mFeet[i]->mPosition.set(mPosition);
        mFeet[i]->initWithoutIter();
        MR::initLightCtrl(mFeet[i]);
    }

    mFeet[0]->initFixedPosition(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);
    mFeet[1]->initFixedPosition(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);

    initLongFoot(rIter);

    mCocoNutBallArray = new CocoNutBall[::cCocoNutBallNum];
    for (int i = 0; i < ::cCocoNutBallNum; i++) {
        mCocoNutBallArray[i].initWithoutIter();
        mCocoNutBallArray[i].mHost = this;
        mCocoNutBallArray[i].mIsHeightLimited = true;
        mCocoNutBallArray[i].mIsRallyBgm = true;
        mCocoNutBallArray[i].mKillDistance = cNum_initModel_f1;

        float arg;
        if (MR::getPlacementArg0NoInit(rIter, &arg)) {
            mCocoNutBallArray[i].mHeightMax = arg;
        }

        if (MR::getPlacementArg2NoInit(rIter, &arg)) {
            mCocoNutBallArray[i].mHeightMin = arg;
        }
    }

    mFireBallArray = new FireBall[::cFireBallNum];

    for (int i = 0; i < ::cFireBallNum; i++) {
        mFireBallArray[i].initWithoutIter();
        mFireBallArray[i].mHost = this;
    }

    mFireBubbleArray = new FireBubble[::cFireBubbleNum];
    for (int i = 0; i < ::cFireBubbleNum; i++) {
        mFireBubbleArray[i].initWithoutIter();
    }

    mThrowPos = new FixedPosition(this, "Lip", Vec3f(-cNum_initModel_f2, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    mBubbleAppearPos = new FixedPosition(this, Vec3f(0.0f, cNum_initModel_f3, cNum_initModel_f4), Vec3f(0.0f, 0.0f, 0.0f));

    mMagma = new OtaKingMagma(this, -1);
    mMagma->initWithoutIter();
    mDownCocoNut = new CocoNutBall;
    mDownCocoNut->mScale.set(::cDownDemoCocoNutScale);
    mDownCocoNut->initWithoutIter();
}

void OtaKing::initSensor() {
    initHitSensor(3);
    MR::addHitSensorEnemy(this, "body", 16, ::cBodySensorRadius, ::cSensorOffset);
    MR::addHitSensorAtJointEnemy(this, "face", "Face", 16, ::cBodySensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "search", 16, ::cEyeSensorRadius, ::cSensorOffset);
}

void OtaKing::dirToPlayer() {
    Vec3f vec;
    vec.sub(*MR::getPlayerPos(), mPosition);

    f32 angle = MR::repeat(MR::toDegree(MR::atan2(vec.x, vec.z)), mRotation.y - cNum_dirToPlayer_f1, cNum_dirToPlayer_f2);

    f32 angleMinRotY = angle - mRotation.y;

    if ((0.0f < angleMinRotY && mTurnSpeed < angleMinRotY) || (angleMinRotY < 0.0f && angleMinRotY < mTurnSpeed)) {
        if (angleMinRotY * mTurnSpeed < 0.0f) {
            mTurnSpeed = 0.0f;
        }

        mRotation.y += mTurnSpeed;

        if (0.0f < angleMinRotY) {
            mTurnSpeed = MR::min(mTurnSpeed + ::cTurnSpeedAccel, ::cTurnSpeedMax);
        } else {
            mTurnSpeed = MR::max(mTurnSpeed - ::cTurnSpeedAccel, -::cTurnSpeedMax);
        }

    } else {
        mRotation.y = angle;
        mTurnSpeed = 0.0f;
    }

    MR::repeatDegree(&mRotation.y);
}

CocoNutBall* OtaKing::getDisappearedCocoNut() {
    for (int i = 0; i < ::cCocoNutBallNum; i++) {
        if (MR::isDead(&mCocoNutBallArray[i])) {
            return &mCocoNutBallArray[i];
        }
    }

    return nullptr;
}

FireBall* OtaKing::getDisappearedFireBall() {
    for (int i = 0; i < ::cFireBallNum; i++) {
        if (MR::isDead(&mFireBallArray[i])) {
            return &mFireBallArray[i];
        }
    }

    return nullptr;
}

FireBubble* OtaKing::getDisappearedBubble() {
    for (int i = 0; i < ::cFireBubbleNum; i++) {
        if (MR::isDead(&mFireBubbleArray[i])) {
            return &mFireBubbleArray[i];
        }
    }

    return nullptr;
}

s32 OtaKing::getDisappearedCocoNutNum() const {
    s32 dissappearedNum = 0;
    for (int i = 0; i < ::cCocoNutBallNum; i++) {
        if (MR::isDead(&mCocoNutBallArray[i])) {
            dissappearedNum++;
        }
    }

    return dissappearedNum;
}

s32 OtaKing::getDisappearedFireBallNum() const {
    s32 dissappearedNum = 0;
    for (int i = 0; i < ::cFireBallNum; i++) {
        if (MR::isDead(&mFireBallArray[i])) {
            dissappearedNum++;
        }
    }

    return dissappearedNum;
}

s32 OtaKing::getDisappearedFireBubbleNum() const {
    s32 dissappearedNum = 0;
    for (int i = 0; i < ::cFireBubbleNum; i++) {
        if (MR::isDead(&mFireBubbleArray[i])) {
            dissappearedNum++;
        }
    }

    return dissappearedNum;
}

bool OtaKing::isValidThrowCocoNut() const {
    if (getDisappearedCocoNutNum() == 0) {
        return false;
    }

    f32 distToPlayer = MR::calcDistanceToPlayer(this);
    if (::cThrowDistance < distToPlayer) {
        return false;
    }

    for (int i = 0; i < ::cCocoNutBallNum; i++) {
        CocoNutBall* currentCocoNut = &mCocoNutBallArray[i];
        if (!MR::isDead(currentCocoNut) && MR::isNear(this, currentCocoNut, distToPlayer + ::cValidThrowCocoNutDistance)) {
            return false;
        }
    }

    return true;
}

bool OtaKing::isValidThrowFireBall() const {
    return ::cFireBallThrowNum <= getDisappearedFireBallNum() && MR::calcDistanceToPlayer(this) < ::cThrowDistance &&
           (!mIsLv2 || getDisappearedCocoNutNum() == 3);
}

bool OtaKing::isOneHP() const {
    return mHits + 1 >= ::cHP;
}

bool OtaKing::isValidBubbleAttack() const {
    if (getDisappearedFireBubbleNum() != ::cBubbleAppearNum) {
        return false;
    }

    if (mIsLv2) {
        return mHits > 0;
    } else {
        return isOneHP();
    }
}

bool OtaKing::isDamageNerve() {
    return isNerve(GET_NERVE(OtaKing, OtaKingNrvDamage)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvPowerUp)) ||
           isNerve(GET_NERVE(OtaKing, OtaKingNrvDown));
}

void OtaKing::damage() {
    if (isDamageNerve()) {
        return;
    }

    MR::deleteEffectAll(this);

    mHits++;

    if (mHits < ::cHP) {
        MR::startSound(this, "SE_BV_OTAKING_DAMAGE");
        MR::startSound(this, "SE_BM_OTAKING_DAMAGE");
        switch (mHits % ::cHP) {
        case 0:
            MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_3");
            break;
        case 1:
            MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_1");
            break;
        case 2:
            MR::startSystemSE("SE_SY_VS_BOSS_DAMAGE_2");
        }

        if (isOneHP()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvPowerUp));
        } else {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvDamage));
        }
    } else {
        setNerve(GET_NERVE(OtaKing, OtaKingNrvDown));
    }
}

void OtaKing::throwCocoNut() {
    CocoNutBall* cocoNut = getDisappearedCocoNut();
    if (cocoNut == nullptr) {
        return;
    }

    mThrowPos->calc();
    Vec3f trans;
    mThrowPos->copyTrans(&trans);

    Vec3f vec;
    vec.sub(*MR::getPlayerPos(), mPosition);

    f32 angle = mRotation.y - MR::repeat(MR::toDegree(MR::atan2(vec.x, vec.z)), mRotation.y - cNum_throwCocoNut_f1, cNum_throwCocoNut_f2);
    f32 random = MR::getRandom(::cThrowAngleMin, ::cThrowAngleMax);

    cocoNut->appearAndThrow(trans, angle + (mThrowNum % 2 == mHits % 2 ? random : -random));
    mThrowNum++;
}

void OtaKing::throwFireBall() {
    mThrowPos->calc();

    Vec3f trans;
    mThrowPos->copyTrans(&trans);

    Vec3f vec;
    vec.sub(*MR::getPlayerPos(), mPosition);

    f32 angle = mRotation.y - MR::repeat(MR::toDegree(MR::atan2(vec.x, vec.z)), mRotation.y - cNum_throwFireBall_f1, cNum_throwFireBall_f2);

    for (int i = 0; i < ::cFireBallThrowNum; i++) {
        FireBall* currentFireBall = getDisappearedFireBall();
        if (currentFireBall == nullptr) {
            break;
        }

        f32 throwAngle = angle;
        if (i != 0) {
            f32 angleOffset;
            if (i % 2 == 0) {
                angleOffset = ::cFireBallThrowAngle * ((i + 1) / 2);
            } else {
                angleOffset = -::cFireBallThrowAngle * ((i + 1) / 2);
            }

            throwAngle += angleOffset;
        }

        currentFireBall->appearAndThrow(trans, ::cFireBallThrowSpeed, throwAngle);
    }
}

void OtaKing::startBckWaitIfNotPlaying() {
    MR::tryStartBck(this, "Wait");

    for (int i = 0; i < ARRAY_SIZE(mFeet); i++) {
        MR::tryStartBck(mFeet[i], i == 0 ? "WaitR" : "WaitL");
    }
}

void OtaKing::startBckWithFrontFoot(const char* pBckName) {
    MR::startBck(this, pBckName);
    mBckLBuffer.format("%sR", pBckName);
    MR::startBck(mFeet[0], mBckLBuffer.c_str());
    mBckRBuffer.format("%sL", pBckName);
    MR::startBck(mFeet[1], mBckRBuffer.c_str());
}

bool OtaKing::isNerveValidNearAttack() const {
    return isNerve(GET_NERVE(OtaKing, OtaKingNrvWait)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNutWait)) ||
           isNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNut)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait)) ||
           isNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall)) || isNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
}

void OtaKing::appearBubble() {
    mBubbleAppearPos->calc();

    Vec3f trans;

    mBubbleAppearPos->copyTrans(&trans);

    for (int i = 0; i < ::cBubbleAppearNum; i++) {
        FireBubble* currentBubble = getDisappearedBubble();

        if (currentBubble != nullptr) {
            Vec3f dir;
            dir.sub(trans, mPosition);

            if (MR::normalizeOrZero(&dir)) {
                dir.set< f32 >(0.0f, 0.0f, 1.0f);
            }

            Pos3f rotate;

            rotate.makeRotate(Vec3f(0.0f, 1.0f, 0.0f),
                              MR::toRadian(((i + cNum_appearBubble_f1) * cNum_appearBubble_f2) + MR::getRandom(-::cBubbleAppearRandomAngle, ::cBubbleAppearRandomAngle)));

            rotate.mult33(dir, dir);
            currentBubble->appear(trans, dir, ::cBubbleAppearVelocity);
        }
    }
}

void OtaKing::startDemo() {
    for (int i = 0; i < ARRAY_SIZE(mFeet); i++) {
        MR::requestMovementOn(mFeet[i]);
    }

    if (!mIsLv2) {
        for (int i = 0; i < ARRAY_SIZE(mLongFeet); i++) {
            MR::requestMovementOn(mLongFeet[i]);
        }
    }

    MR::pauseOffEffectAll(this);
    MR::pauseOffEffectAll(mMagma);
}

void OtaKing::appearStarPiece() {
    Vec3f appearPos(mPosition);
    appearPos.y += ::cStarPieceAppearOffsetY;
    MR::appearStarPiece(this, appearPos, mHits * ::cStarPieceAppearNum, ::cStarPieceAppearRange, ::cStarPieceAppearVelocity, false);
    MR::startSoundObject(mSoundObj, "SE_OJ_STAR_PIECE_BURST");
}

void OtaKing::initLongFoot(const TableIter& rIter) {
    if (!mIsLv2) {
        for (int i = 0; i < ARRAY_SIZE(mLongFeet); i++) {
            mLongFeet[i] = new OtaKingLongFoot(this, ::cLongFootDemoBckStep[i], "飾り足");
            mLongFeet[i]->init(rIter);
            mLongFeet[i]->mScale.set(::cLongFootScale);
        }

        makeTranslationMatrix(&mLongFootMtx, mPosition);
        mLongFeet[0]->initFixedPosition(mLongFootMtx, Vec3f(cNum_initLongFoot_f1, cNum_initLongFoot_f2, -cNum_initLongFoot_f3), Vec3f(-cNum_initLongFoot_f4, cNum_initLongFoot_f5, 0.0f));
        mLongFeet[1]->initFixedPosition(mLongFootMtx, Vec3f(-cNum_initLongFoot_f6, cNum_initLongFoot_f7, 0.0f), Vec3f(0.0f, cNum_initLongFoot_f8, cNum_initLongFoot_f9));
        mLongFeet[2]->initFixedPosition(mLongFootMtx, Vec3f(0.0f, cNum_initLongFoot_f10, cNum_initLongFoot_f11), Vec3f(-cNum_initLongFoot_f12, cNum_initLongFoot_f13, 0.0f));
        mLongFeet[3]->initFixedPosition(mLongFootMtx, Vec3f(cNum_initLongFoot_f14, cNum_initLongFoot_f15, -cNum_initLongFoot_f16), Vec3f(-cNum_initLongFoot_f17, -cNum_initLongFoot_f18, 0.0f));
    }
}

bool OtaKing::tryThrowCocoNutOrFireBallIfWait(s32 waitFrame) {
    if (MR::isGreaterStep(this, waitFrame) && isValidThrowCocoNut()) {
        setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNut));
        return true;
    }

    if (MR::isGreaterStep(this, waitFrame * 2) && isValidThrowFireBall()) {
        setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall));
        return false;
    }

    return false;
}

void OtaKing::exeWaitOnSwitch() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);

        startBckWithFrontFoot("Appear");
        MR::setBckFrameAndStop(this, 0.0f);

        mScale.set(cNum_exeWaitOnSwitch_f1);

        MR::setBckFrameAndStop(mFeet[0], 0.0f);
        MR::hideModel(mFeet[0]);
        MR::setBckFrameAndStop(mFeet[1], 0.0f);
        MR::hideModel(mFeet[1]);

        if (!mIsLv2) {
            for (int i = 0; i < ARRAY_SIZE(mLongFeet); i++) {
                mLongFeet[i]->hide();
            }
        }
    }
}

void OtaKing::exeAppearDemo() {
    if (MR::isFirstStep(this)) {
        startDemo();
        startBckWithFrontFoot("Appear");
        mScale.set(1.0f);

        MR::showModel(mFeet[0]);
        MR::showModel(mFeet[1]);

        if (!mIsLv2) {
            for (int i = 0; i < ARRAY_SIZE(mLongFeet); i++) {
                mLongFeet[i]->startAppearDemo();
            }
        }

        MR::startAnimCameraTargetSelf(this, mAnimCamera, "Appear", 0, 1.0f);
        MR::stopStageBGM(0);
        MR::startBossBGM(0);
        mRotation.set(::cAppearDemoRotate);
        MR::overlayWithPreviousScreen(2);

        if (mIsLv2) {
            MR::forceToFrameCinemaFrame();
        }
    }

    if (MR::isStep(this, cStep_exeAppearDemo_1)) {
        MR::hidePlayer();
    }

    if (MR::isStep(this, ::cAppearSeStep0)) {
        MR::startSound(this, "SE_BM_OTAKING_MAGMA_HITBACK");
    }

    if (MR::isStep(this, ::cAppearSeStep1)) {
        MR::startSound(this, "SE_BM_OTAKING_MAGMA_DAMAGE");
    }

    if (MR::isStep(this, ::cAppearSeStep2)) {
        MR::startSound(this, "SE_BM_OTAKING_MAGMA_HITBACK");
    }

    if (MR::isStep(this, ::cAppearSeStep3)) {
        MR::startSound(this, "SE_BM_OTAKING_MAGMA_HITBACK");
    }

    if (MR::isStep(this, ::cAppearDemoRippleStepS)) {
        mMagma->attack();
    }

    if (MR::isStep(this, ::cAppearDemoRippleStepL)) {
        mMagma->appearDemo();
    }

    MR::tryRumblePadVeryWeak(this, PLAYER_1);

    if (MR::isStep(this, ::cAppearDemoRumblePad01)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, ::cAppearDemoRumblePad02)) {
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        MR::shakeCameraWeak();
    }

    if (MR::isStep(this, ::cAppearDemoRumblePad03)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, ::cAppearDemoRumblePad04)) {
        MR::shakeCameraStrong();
    }

    if (MR::isGreaterStep(this, ::cAppearDemoRumblePad04) && MR::isLessStep(this, ::cAppearDemoRumblePad05)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, ::cAppearDemoRumblePad06)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }

    if (MR::isBckStopped(this)) {
        MR::validateHitSensors(this);
        validateClippingAndEndDemo("出現");
        MR::showPlayer();
        MR::endAnimCamera(this, mAnimCamera, "Appear", 0, true);

        if (!mIsLv2) {
            for (int i = 0; i < ARRAY_SIZE(mLongFeet); i++) {
                mLongFeet[i]->endDemo();
            }
        }

        Vec3f vec;
        vec.sub(*MR::getPlayerPos(), mPosition);

        mRotation.y = MR::repeatDegree(MR::toDegree(MR::atan2(vec.x, vec.z)));

        setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
    }
}

void OtaKing::exeWait() {
    if (MR::isFirstStep(this)) {
        startBckWaitIfNotPlaying();
    }

    if (tryThrowCocoNutOrFireBallIfWait(::cWaitFrame)) {
        mThrowNum = 0;
    }
}

void OtaKing::exeThrowCocoNutWait() {
    if (MR::isFirstStep(this)) {
        startBckWaitIfNotPlaying();
    }

    tryThrowCocoNutOrFireBallIfWait(::cThrowCocoNutWaitFrame);
}

void OtaKing::exeThrowCocoNut() {
    if (MR::isFirstStep(this)) {
        startBckWithFrontFoot("Attack");
        MR::startSound(this, "SE_BV_OTAKING_PRE_RALLYBALL");
    }

    if (MR::isStep(this, ::cThrowCocoNutStep)) {
        throwCocoNut();
        MR::emitEffect(this, "OtaKingCocoAttack");
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    if (MR::isStep(this, ::cCocoNutRippleStep)) {
        mMagma->attack();
    }

    if (MR::isBckStopped(this)) {
        if (isValidBubbleAttack()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
            return;
        }

        if (mHits > 0) {
            if (isValidThrowFireBall()) {
                setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall));
            } else {
                setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
            }
        } else if (mThrowNum >= 2) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
        } else {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNutWait));
        }
    }
}

void OtaKing::exeThrowFireBallWait() {
    if (MR::isFirstStep(this)) {
        startBckWaitIfNotPlaying();
    }

    if (MR::isGreaterStep(this, ::cThrowFireBallWaitFrame) && isValidThrowFireBall()) {
        setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall));
    }
}

void OtaKing::exeThrowFireBall() {
    if (MR::isFirstStep(this)) {
        startBckWithFrontFoot("FireAttack");
        MR::startSound(this, "SE_BV_OTAKING_PRE_FIREBALL");
    }

    MR::startLevelSound(this, "SE_BM_LV_OTAKING_PRE_FIRE");

    if (MR::isStep(this, ::cThrowFireBallStep)) {
        throwFireBall();
        MR::emitEffect(this, "OtaKingFireAttack");
        MR::startSound(this, "SE_BM_OTAKING_FIRE_OUT");
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    if (MR::isStep(this, ::cFireBallRippleStep)) {
        mMagma->fireAttack();
    }

    if (MR::isBckStopped(this)) {
        if (isValidBubbleAttack()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
        } else if (mHits > 0 && isValidThrowCocoNut()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNut));
        } else {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvWait));
        }
    }
}

void OtaKing::exeDamage() {
    if (MR::isFirstStep(this)) {
        startBckWithFrontFoot("Damage");
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        appearStarPiece();
        mMagma->damage();
    }

    if (MR::isStep(this, cStep_exeDamage_1)) {
        MR::stopScene(mIsLv2 ? 6 : 16);
    }

    if (MR::isStep(this, cStep_exeDamage_2)) {
        MR::shakeCameraNormalStrong();
    }

    if (MR::isBckStopped(this)) {
        if (isValidBubbleAttack()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
        } else if (isValidThrowFireBall()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall));
        } else {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
        }
    }
}

void OtaKing::exePowerUp() {
    if (MR::isFirstStep(this)) {
        startBckWithFrontFoot("CrownDamage");
        MR::startBrk(this, "CrownDamage");
        MR::startBpk(this, "CrownDamage");
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        appearStarPiece();
        mMagma->damage();
    }

    if (MR::isStep(this, cStep_exePowerUp_1)) {
        MR::stopScene(mIsLv2 ? 6 : 16);
    }

    if (MR::isStep(this, cStep_exePowerUp_2)) {
        MR::shakeCameraNormalStrong();
    }

    if (MR::isStep(this, ::cDamageCrownFallFrame)) {
        MR::startSoundObject(mSoundObj, "SE_BM_OTAKING_CROWN_FALL");
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
        mMagma->damage();
    }

    if (MR::isGreaterStep(this, ::cDamageCrownFallFrame)) {
        MR::tryRumblePadVeryWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, cStep_exePowerUp_3)) {
        MR::hideMaterial(this, "lambert141_v");
        MR::hideMaterial(this, "Daiya_v");
    }

    if (MR::isStep(this, ::cBlurStartStep)) {
        mMagma->damage();
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, cStep_exePowerUp_4)) {
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, cStep_exePowerUp_5)) {
        MR::shakeCameraNormal();
    }

    if (MR::isGreaterStep(this, ::cBlurStartStep) && MR::isLessStep(this, cStep_exePowerUp_6)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, ::cBlurStartStep)) {
        MR::startCenterScreenBlur(::cBlurTotalFrame, cNum_exePowerUp_f1, 80, 5, 30);
    }

    if (MR::isBckStopped(this)) {
        MR::startBrk(this, "Wait");
        MR::startBpk(this, "Wait");

        MR::showModel(mFeet[0]);
        MR::showModel(mFeet[1]);

        if (MR::isValidSwitchB(this)) {
            MR::onSwitchB(this);
        }

        if (isValidBubbleAttack()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
            return;
        } else if (isValidThrowFireBall()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall));
            return;
        } else {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
            return;
        }
    }

    if (MR::getPlayerPos()->y < mPosition.y + ::cDamageFrontFootClipingHeight) {
        MR::hideModel(mFeet[0]);
        MR::hideModel(mFeet[1]);
    } else {
        // unneeded, as showmodel is already called unconditionally
        MR::showModel(mFeet[0]);
        MR::showModel(mFeet[1]);
    }
}

void OtaKing::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
        MR::startSound(this, "SE_BM_OTAKING_LAST_DAMAGE");
        MR::startSound(this, "SE_BM_OTAKING_DIE");
        MR::startSound(this, "SE_BV_OTAKING_DIE");
        MR::startSystemSE("SE_SY_VS_BOSS_LAST_HIT");
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        MR::invalidateHitSensors(this);
        MR::stopStageBGM(60);

        for (int i = 0; i < ::cCocoNutBallNum; i++) {
            if (!MR::isDead(&mCocoNutBallArray[i])) {
                mCocoNutBallArray[i].kill();
            }
        }

        for (int i = 0; i < ::cFireBallNum; i++) {
            if (!MR::isDead(&mFireBallArray[i])) {
                mFireBallArray[i].kill();
            }
        }

        for (int i = 0; i < ::cFireBubbleNum; i++) {
            if (!MR::isDead(&mFireBubbleArray[i])) {
                mFireBubbleArray[i].kill();
            }
        }

        if (!mIsLv2) {
            for (int i = 0; i < ARRAY_SIZE(mLongFeet); i++) {
                mLongFeet[i]->startDownDemo();
            }
        }
    }

    if (MR::isStep(this, cStep_exeDown_1)) {
        MR::stopScene(20);
    }

    if (MR::isStep(this, cStep_exeDown_2)) {
        invalidateClippingAndStartDemo("ダウン", GET_NERVE(OtaKing, OtaKingNrvDownDemo), GET_NERVE(OtaKing, OtaKingNrvWaitStartDemo));
    }
}

void OtaKing::exeDownDemo() {
    Pos3f rotate;
    rotate.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), MR::toRadian(::cDownDemoRotate.y));
    rotate.setTrans(mPosition);

    if (MR::isFirstStep(this)) {
        startDemo();
        startBckWithFrontFoot("Down");
        MR::startAnimCameraTargetSelf(this, mAnimCamera, "Down", 0, 1.0f);
        mRotation.set(::cDownDemoRotate);
        mMagma->damage();
        Vec3f demoBreakVec;
        rotate.mult(::cDownDemoCocoNutPos, demoBreakVec);
        mDownCocoNut->mScale.set(::cDownDemoCocoNutScale);
        mDownCocoNut->demoBreak(demoBreakVec);
        Vec3f starAppearPos(mPosition);
        starAppearPos.y += ::cPowerStarAppearOffsetY;
        MR::requestAppearPowerStar(this, starAppearPos);
    }

    if (MR::isStep(this, cStep_exeDownDemo_1)) {
        MR::hidePlayer();
    }

    if (MR::isStep(this, ::cDownRippleStep)) {
        mMagma->down();
    }

    if (MR::isGreaterStep(this, ::cDownSinkStartStep)) {
        MR::startLevelSound(this, "SE_BM_LV_OTAKING_SINK");
    }

    mRotation.set(::cDownDemoRotate);
    MR::tryRumblePadWeak(this, PLAYER_1);

    if (MR::isStep(this, 0)) {
        MR::shakeCameraStrong();
    }

    if (MR::isStep(this, ::cDownDemoRumbleFrame1)) {
        MR::shakeCameraNormal();
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    if (MR::isStep(this, ::cDownDemoRumbleFrame2)) {
        MR::shakeCameraNormal();
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    if (MR::isStep(this, ::cPowerStarExistBgmStartStep)) {
        MR::startAfterBossBGM();
    }

    if (MR::isStep(this, ::cDownDemoFrame)) {
        validateClippingAndEndDemo("ダウン");
        setNerve(GET_NERVE(OtaKing, OtaKingNrvAppearStar));
    }
}

void OtaKing::exeAppearStar() {
    if (MR::isFirstStep(this)) {
        mScale.set(cNum_exeAppearStar_f1);
        for (int i = 0; i < ARRAY_SIZE(mFeet); i++) {
            mFeet[i]->kill();
        }

        if (!mIsLv2) {
            for (int i = 0; i < ARRAY_SIZE(mLongFeet); i++) {
                mLongFeet[i]->kill();
            }
        }

        getSensor("body")->invalidate();
        getSensor("search")->invalidate();

        MR::validateClipping(this);
        MR::pauseOffEffectAll(mMagma);
    } else if (MR::isEndPowerStarAppearDemo(this)) {
        MR::showPlayer();
        setNerve(GET_NERVE(OtaKing, OtaKingNrvDead));
    }
}

void OtaKing::exeDead() {
    if (MR::isFirstStep(this) && MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void OtaKing::exeHitBackStart() {
    if (MR::isFirstStep(this)) {
        startBckWithFrontFoot("HitBackStart");
        MR::deleteEffectAll(this);
    }
}

void OtaKing::exeHitBack() {
    if (MR::isFirstStep(this)) {
        startBckWithFrontFoot("HitBack");
        MR::deleteEffectAll(this);
        MR::tryRumblePadStrong(this, PLAYER_1);
        mMagma->hitBack();
    }

    if (MR::isBckStopped(this)) {
        if (isValidBubbleAttack()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
            return;
        }

        if (mHits > 0) {
            if (isValidThrowFireBall()) {
                setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBall));
            } else {
                setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
            }
        } else if (mThrowNum >= 2) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowFireBallWait));
        } else {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvThrowCocoNutWait));
        }
    }
}

void OtaKing::exeNearAttack() {
    if (MR::isFirstStep(this)) {
        startBckWithFrontFoot("HitBack");
        MR::deleteEffectAll(this);
        mMagma->hitBack();
    }

    if (MR::isStep(this, ::cNearAttackRippleStep)) {
        mMagma->attack();
    }

    if (MR::isBckStopped(this)) {
        if (isValidBubbleAttack()) {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvBubbleAttack));
        } else {
            setNerve(GET_NERVE(OtaKing, OtaKingNrvWait));
        }
    }
}

void OtaKing::exeBubbleAttack() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BV_OTAKING_PRE_FIREBALL");
        startBckWithFrontFoot("BubbleAttack");
    }

    if (MR::isStep(this, ::cBubbleAppearStep)) {
        appearBubble();
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, ::cBubbleAttackRippleStep)) {
        MR::startSound(this, "SE_BM_OTAKING_MAGMA_BEAT");
        mMagma->attack();
    }

    MR::setNerveAtBckStopped(this, GET_NERVE(OtaKing, OtaKingNrvWait));
}

void OtaKing::exeWaitStartDemo() {
}
