#include "Game/NPC/TicoFat.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/BenefitItemLifeUp.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Screen/FullnessMeter.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getScaledActionName_f1, "getScaledActionName.f1", 1.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_getScaledActionName_f2, "getScaledActionName.f2", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_TicoFat_f1, "TicoFat.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 220.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 220.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setCameraParam_f1, "setCameraParam.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setCameraParam_f2, "setCameraParam.f2", 1.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setCameraParam_f3, "setCameraParam.f3", 1300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setCameraParam_f4, "setCameraParam.f4", 1680.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.99864602f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcScale_f1, "calcScale.f1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f1, "setScale.f1", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f2, "setScale.f2", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f3, "setScale.f3", 1.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f4, "setScale.f4", 1.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f5, "setScale.f5", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f6, "setScale.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f7, "setScale.f7", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f8, "setScale.f8", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f9, "setScale.f9", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f10, "setScale.f10", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f11, "setScale.f11", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f12, "setScale.f12", 220.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setScale_f13, "setScale.f13", 220.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setMessage_f1, "setMessage.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setMessage_f2, "setMessage.f2", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_setMessage_f3, "setMessage.f3", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveStarPiece_f1, "receiveStarPiece.f1", 1.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_disappear_f1, "disappear.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScreenEffect_f1, "updateScreenEffect.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDemo_f1, "exeDemo.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f1, "exeFly.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f2, "exeFly.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f3, "exeFly.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f4, "exeFly.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f5, "exeFly.f5", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f6, "exeFly.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f7, "exeFly.f7", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f8, "exeFly.f8", 0.1f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_updateGuidance_isNearPlayer_1, "updateGuidance.isNearPlayer.1", -1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateGoods_isNearPlayer_1, "updateGoods.isNearPlayer.1", 2000.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeDemo_1, "exeDemo.1", 89);

namespace NrvTicoFat {
    NEW_NERVE(TicoFatNrvReaction, TicoFat, Reaction);
    NEW_NERVE(TicoFatNrvPrep, TicoFat, Prep);
    NEW_NERVE(TicoFatNrvWait, TicoFat, Wait);
    NEW_NERVE(TicoFatNrvPoint, TicoFat, Point);
    NEW_NERVE(TicoFatNrvEat, TicoFat, Eat);
    NEW_NERVE(TicoFatNrvChem, TicoFat, Chem);
    NEW_NERVE(TicoFatNrvTest, TicoFat, Test);
    NEW_NERVE(TicoFatNrvFullness, TicoFat, Fullness);
    NEW_NERVE(TicoFatNrvMeta, TicoFat, Meta);
    NEW_NERVE(TicoFatNrvDemo, TicoFat, Demo);
    NEW_NERVE(TicoFatNrvFly, TicoFat, Fly);
    NEW_NERVE(TicoFatNrvWipeOut, TicoFat, WipeOut);
    NEW_NERVE(TicoFatNrvWipeIn, TicoFat, WipeIn);
    NEW_NERVE(TicoFatNrvInfo, TicoFat, Info);
    NEW_NERVE(TicoFatNrvAfter, TicoFat, After);
};  // namespace NrvTicoFat

namespace {
    class TicoFatParam : public AnimScaleParam {
    public:
        TicoFatParam() {
            mHitReactionVelocity = 0.12f;
            mHitReactionSpring = 0.12f;
            mHitReactionDamping = 0.9f;
        }
    };

    static const char* sInfoMessageID = "InformationPlanet";
    static TicoFatParam sParam;
    const Color8 hPointLight(0xFF, 0x7D, 0xE6, 0xFF);

    enum ScaledAction {
        ACTION_EAT,
        ACTION_EAT_START,
        ACTION_WAIT,
        ACTION_EXCITE,
        ACTION_JOY,
        ACTION_COUNT
    };

    // Action names per size step (scale up to 1.3, up to 1.6, above 1.6), in ScaledAction order.
    const char* const sScaledActionNames[3][ACTION_COUNT] = {
        {"eat0", "eatstart0", "wait0", "excite0", "joy0"},
        {"eat1", "eatstart1", "wait1", "excite1", "joy1"},
        {"eat2", "eatstart2", "wait2", "excite2", "joy2"},
    };

    // The action name for the actor's current size step; a table lookup with no name compare.
    const char* getScaledActionName(TicoFat* pActor, ScaledAction action) {
        f32 scale = pActor->calcScale();
        s32 step = 0;
        if (scale > cNum_getScaledActionName_f1) {
            step = 2;
        } else if (scale > cNum_getScaledActionName_f2) {
            step = 1;
        }

        return sScaledActionNames[step][action];
    }
};  // namespace

TicoFat::TicoFat(const char* pName) : NPCActor(pName) {
    mCameraInfo = nullptr;
    mItem = nullptr;
    mShootPath = nullptr;
    mIsCameraOn = false;
    mItemType = -1;
    mTicoSeedId = -1;
    mEatInterval = 5;
    mFlySpeed = cNum_TicoFat_f1;
    mEatSoundStep = 0;
    mAbsorbSoundStep = 0;
    mScreenEffectMtx.identity();
    mScreenLightMtx.identity();
}

void TicoFat::init(const TableIter& rIter) {
    NPCActorCaps caps("TicoFat");
    NPCActorItem item("TicoFat");
    caps.setDefault();
    caps.mWaitNerve = GET_NERVE(TicoFat, TicoFatNrvPrep);
    caps.mMessage = 0;
    caps.mSensorSize = cNum_init_f1;
    caps.mSensorOffset.y = cNum_init_f2;
    caps.mSensorJoint = "Center";
    caps.mUseShadow = true;
    caps.mRailRider = false;
    caps.mPointerJoint = "Center";
    caps.mPointerSize = cNum_init_f3;
    caps.mBinder = 0;
    caps.mSoundSize = 6;
    caps.mSensorMax = 2;
    mPointingStep = 0;
    mNearStep = 0;
    mStartEat = false;
    mGuidanceId = -1;
    mScaleController = new AnimScaleController(&::sParam);
    NPCActor::initialize(rIter, caps);
    s32 itemType = mItemType;
    MR::getPlacementArg0NoInit(rIter, &itemType);
    mItemType = itemType;

    switch (mItemType) {
    case 0:
        mItem = MR::createKinokoOneUp();
        break;
    case 1:
        mItem = MR::createKinokoSuper();
        break;
    case -1:
    case 2:
        break;
    }

    if (mItem != nullptr) {
        mItem->mPosition.set(mPosition);
        MR::initDefaultPos(mItem, rIter);
    }

    s32 numItem = 10;
    MR::getPlacementArg1NoInit(rIter, &numItem);
    mStarPieceLeft = mStarPieceNeed = numItem;
    mCurrentFed = 0;
    MR::addHitSensorAtJointEye(this, "Mouth", "Mouth", 8, cNum_init_f4, Vec3f(0.0f, 0.0f, 0.0f));
    initStarPieceSaveData(rIter);
    MR::declareStarPieceReceiver(this, mStarPieceLeft);
    mMsgCtrl = MR::createTalkCtrlDirect(this, rIter, "Common_TicoFat001", Vec3f(0.0f, cNum_init_f5, 0.0f), nullptr);
    mHungryTalkCtrl = MR::createTalkCtrlDirect(this, rIter, "Common_TicoFat000", Vec3f(0.0f, cNum_init_f6, 0.0f), nullptr);
    MR::registerEventFunc(mMsgCtrl, TalkMessageFunc_Inline(this, &TicoFat::eventFunc));
    MR::setMessageArg(mMsgCtrl, mStarPieceLeft);
    MR::setMessageArg(mHungryTalkCtrl, mStarPieceLeft);
    MR::setDistanceToTalk(mMsgCtrl, cNum_init_f7);
    MR::setDistanceToTalk(mHungryTalkCtrl, cNum_init_f8);
    MR::onRootNodeAutomatic(mMsgCtrl);
    MR::onStartOnlyFront(mMsgCtrl);
    MR::onStartOnlyFront(mHungryTalkCtrl);
    // Arg 2 set: starts eating at once. Arg 3: the guidance message.
    s32 eatAtStart = -1;
    MR::getPlacementArg2NoInit(rIter, &eatAtStart);
    if (eatAtStart != -1) {
        startEat();
    }

    MR::getPlacementArg3NoInit(rIter, &mGuidanceId);
    mMeter = new FullnessMeter(this, mStarPieceNeed, mStarPieceNeed - mStarPieceLeft);
    mMeter->initWithoutIter();
    mMeter->setNumber(mStarPieceNeed - mStarPieceLeft);
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "変身");
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "飛行");
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "喜び");
    MR::declareEventCameraProgrammable("デブチコカメラ");
    MR::useStageSwitchWriteA(this, rIter);
    MR::getNPCItemData(&item, 0);
    equipment(item, 0);
    MR::startAction(mGoods0, "TicoFatGoodsStarPiece");
    MR::startAction(mGoods1, "TicoFatGoodsStarPiece");
    MR::setBpkFrame(mGoods0, 0.0f);
    MR::setBpkFrame(mGoods1, cNum_init_f9);
    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        if (mItemType == -1) {
            mShootPath = new SpinDriverShootPath();
            mShootPath->initUsingParabolic(rIter, mPosition);
            MR::getPlacementArg4NoInit(rIter, &mFlySpeed);
        }
    }

    MR::startBva(this, "Small0");
    MR::startBrk(this, "Normal");
    MR::setBrkFrameAndStop(this, 0.0f);
    setScale(calcScale());
    MR::joinToGroupArray(this, rIter, "惑星出現", 32);
    AstroDemoFunction::tryRegisterSimpleCastIfAstroGalaxy(this);
    setDefaults();
    mSwingReactDist = cNum_init_f10;
    const char* waitActionName = getScaledActionName(this, ACTION_WAIT);
    mParam.mWaitAction = waitActionName;
    mParam.mWaitTurnAction = waitActionName;
    const char* talkActionName = "Talk";
    mParam.mTalkAction = talkActionName;
    mParam.mTalkTurnAction = talkActionName;

    if (!enableAppear()) {
        makeActorDead();
    }
}

void TicoFat::initAfterPlacement() {
    MR::extractMtxYDir(getBaseMtx(), &mGravity);
    mGravity.mult(-1.0f);
    if (!mStarPieceLeft) {
        disappear(false);
        makeActorDead();
    }
}

void TicoFat::kill() {
    MR::validateClipping(this);
    mMeter->requestDisappear();
    NPCActor::kill();
}

void TicoFat::setCameraParam() {
    Vec3f yDir, zDir, xDir, trans;
    MR::extractMtxXYZDir(getBaseMtx(), &xDir, &yDir, &zDir);
    MR::extractMtxTrans(getBaseMtx(), &trans);
    mInitPoseQuat.getXDir(xDir);
    mInitPoseQuat.getYDir(yDir);
    mInitPoseQuat.getZDir(zDir);

    trans = mInitPos;
    Vec3f* ptr = &trans;
    MR::setProgrammableCameraParam(
        "デブチコカメラ", (*ptr + (xDir * 0.0f) + (yDir * 0.0f)) + (zDir * 0.0f),
        (*ptr + (xDir * 0.0f)) + ((yDir * cNum_setCameraParam_f1)) + (zDir * MR::getLinerValueFromMinMax(calcScale(), 1.0f, cNum_setCameraParam_f2, cNum_setCameraParam_f3, cNum_setCameraParam_f4)), yDir, false);
}

void TicoFat::control() {
    NPCActor::control();
    Vec3f trans;
    MR::extractMtxTrans(MR::getJointMtx(this, "Center"), &trans);
    MR::requestPointLight(this, Vec3f(trans), ::hPointLight, cNum_control_f1, -1);
    if (isNerve(GET_NERVE(TicoFat, TicoFatNrvPrep)) || isNerve(GET_NERVE(TicoFat, TicoFatNrvWait)) || isNerve(GET_NERVE(TicoFat, TicoFatNrvPoint)) ||
        isNerve(GET_NERVE(TicoFat, TicoFatNrvEat)) || isNerve(GET_NERVE(TicoFat, TicoFatNrvChem)) ||
        isNerve(GET_NERVE(TicoFat, TicoFatNrvFullness)) || isNerve(GET_NERVE(TicoFat, TicoFatNrvDemo))) {
        MR::startLevelSound(this, "SE_SM_LV_TICO_WAIT");
    }

    updateGoods();
    mAbsorbSoundStep--;
    if (mStartEat) {
        if (!mStarPieceLeft) {
            MR::invalidateClipping(this);
            if (isNerve(GET_NERVE(TicoFat, TicoFatNrvPoint)) || isNerve(GET_NERVE(TicoFat, TicoFatNrvEat)) ||
                isNerve(GET_NERVE(TicoFat, TicoFatNrvChem)) || isNerve(GET_NERVE(TicoFat, TicoFatNrvWait))) {
                setNerve(GET_NERVE(TicoFat, TicoFatNrvTest));
            }
        }

        if (!mStarPieceLeft || MR::isPlayerElementModeInvincible() || MR::isPlayerFlying()) {
            mPointingStep = 0;
            mNearStep = 0;
        } else {
            updateGuidance();
            if (mNearStep > 0) {
                updatePointing();
            } else {
                mPointingStep = 0;
            }
        }

        if (mNearStep > 0) {
            if (!mIsCameraOn) {
                mIsCameraOn = true;
                CameraTargetArg arg(this);
                MR::startGlobalEventCamera("デブチコカメラ", arg, 120);
                setCameraParam();
            }
        } else {
            if (mIsCameraOn) {
                mIsCameraOn = false;
                MR::endGlobalEventCamera("デブチコカメラ", 120, true);
            }
        }

        if (mPointingStep > 0) {
            if (MR::isIntervalStep(this, mEatInterval)) {
                if (MR::isHeldFire(PLAYER_1)) {
                    if (!MR::isDemoActive() && !isNerve(GET_NERVE(TicoFat, TicoFatNrvReaction))) {
                        if (mStarPieceLeft && MR::getStarPieceNum() > 0) {
                            shootStarPiece();
                        }
                    }
                }
            }

            mMeter->requestAppear();
        } else {
            mMeter->requestDisappear();
        }
    }
}

bool TicoFat::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::limitedStarPieceHitSound();
        mIsReaction = 1;
        return true;
    } else if (MR::isMsgStarPieceGift(msg)) {
        receiveStarPiece(MR::getNumStarPieceGift(msg));
        return true;
    } else {
        return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }
}

void TicoFat::calcAndSetBaseMtx() {
    NPCActor::calcAndSetBaseMtx();
}

bool TicoFat::eventFunc(u32 var) {
    switch (var) {
    case 0:
        startEat();
        break;
    case 1:
        return false;
    }

    return true;
}

f32 TicoFat::calcScale() {
    return (1.0f + ((f32)(mStarPieceNeed - mStarPieceLeft) / mStarPieceNeed) * cNum_calcScale_f1);
}

void TicoFat::setScale(f32 scale) {
    f32 x = mScale.x;
    mScale.y = scale;
    mScale.x = scale;
    mScale.z = scale;
    if ((x <= cNum_setScale_f1 && cNum_setScale_f2 < scale) || (x <= cNum_setScale_f3 && cNum_setScale_f4 < scale)) {
        mScaleController->startHitReaction();
    }

    // Reddens with fullness (scale 1 to 1.9); the talk distance grows with it up to scale 1.5.
    f32 fullness = (scale - 1.0f) / cNum_setScale_f5;
    if (fullness > 1.0f) {
        fullness = 1.0f;
    }

    f32 frameMax = fullness * MR::getBrkFrameMax(this);
    MR::startBrk(this, "FatRed");
    MR::setBrkFrameAndStop(this, frameMax);
    MR::setSensorRadius(this, "Body", (cNum_setScale_f6 * scale));
    MR::setStarPointerTargetRadius3d(this, cNum_setScale_f7 * scale);
    f32 talkScale = cNum_setScale_f8;
    if (scale < cNum_setScale_f9) {
        talkScale = scale;
    }

    MR::setDistanceToTalk(mMsgCtrl, (cNum_setScale_f10 * talkScale));
    MR::setDistanceToTalk(mHungryTalkCtrl, (cNum_setScale_f11 * talkScale));
    MR::setMessageBalloonFollowOffset(mMsgCtrl, Vec3f(0.0f, cNum_setScale_f12 * scale, 0.0f));
    MR::setMessageBalloonFollowOffset(mHungryTalkCtrl, Vec3f(0.0f, cNum_setScale_f13 * scale, 0.0f));
}

void TicoFat::setMessage(s32 msg) {
    s32 nodeCount = 0;
    f32 leftRate = ((f32)mStarPieceLeft / (f32)mStarPieceNeed);
    MR::resetNode(mHungryTalkCtrl);

    switch (msg) {
    case 1: {
        // The hungrier, the earlier the message node.
        if (1.0f == leftRate) {
            nodeCount = 1;
        } else if (leftRate >= cNum_setMessage_f1) {
            nodeCount = 2;
        } else if (leftRate >= cNum_setMessage_f2) {
            nodeCount = 3;
        } else if (leftRate >= cNum_setMessage_f3) {
            nodeCount = 4;
        } else {
            nodeCount = 5;
        }
    } break;
    case 2:
        nodeCount = 6;
        break;
    case 0:
        break;
    }

    for (s32 i = 0; i < nodeCount; i++) {
        MR::forwardNode(mHungryTalkCtrl);
    }
}

void TicoFat::shootStarPiece() {
    if (MR::giftStarPieceToTarget(getSensor("Mouth"), 1)) {
        mCurrentFed++;
        MR::tryRumblePadVeryWeak(this, PLAYER_1);
    }
}

void TicoFat::receiveStarPiece(s32 num) {
    s32 taken = mStarPieceLeft;
    if (taken >= num) {
        taken = num;
    }

    mCurrentFed -= taken;
    mStarPieceLeft -= taken;
    addStarPieceSaveData(taken);
    setScale(calcScale());
    mMeter->setNumber(mStarPieceNeed - mStarPieceLeft);
    MR::tryRumblePadVeryWeak(this, PLAYER_1);
    startAbsorbSound();
    MR::setMessageArg(mMsgCtrl, mStarPieceLeft);
    MR::setMessageArg(mHungryTalkCtrl, mStarPieceLeft);
    if (mNearStep > 0) {
        setCameraParam();
    }

    if (isNerve(GET_NERVE(TicoFat, TicoFatNrvEat))) {
        MR::setBckRate(this, MR::calcNerveValue(this, 0, NebulaFrameClock::stepsOfSeconds(3), 1.0f, cNum_receiveStarPiece_f1));
    } else {
        if (!isEmptyNerve()) {
            popNerve();
        }

        setNerve(GET_NERVE(TicoFat, TicoFatNrvEat));
    }
}

void TicoFat::startEat() {
    mStartEat = true;
}

bool TicoFat::enableAppear() {
    return true;
}

void TicoFat::initStarPieceSaveData(const TableIter& rIter) {
    if (MR::getPlacementArg7WithInit(rIter, &mTicoSeedId)) {
        s32 starPieceNum = MR::getStarPieceNumGivingToTicoSeed(mTicoSeedId);
        mStarPieceLeft = (mStarPieceNeed - starPieceNum <= 0) ? 0 : mStarPieceNeed - starPieceNum;
    }
}

void TicoFat::addStarPieceSaveData(s32) {
}

void TicoFat::appearInformation() const {
    MR::appearInformationMessage(MR::getGameMessageDirect(::sInfoMessageID), true);
}

// isJustFed: fed in this visit (gives the pieces to the seed and brings the group in); false when it was fed before.
void TicoFat::disappear(bool isJustFed) {
    if (mItem != nullptr) {
        Pos3f pos;
        pos.setInline(getBaseMtx());
        Vec3f trans;
        MR::extractMtxTrans(MR::getJointMtx(this, "Spine1"), &trans);
        pos.mMtx[0][3] = trans.x;
        pos.mMtx[1][3] = trans.y;
        pos.mMtx[2][3] = trans.z;
        MR::appearKinokoOneUpPop(mItem, pos, cNum_disappear_f1);
    }

    if (isJustFed) {
        if (mTicoSeedId != -1) {
            MR::addStarPieceGivingToTicoSeed(mTicoSeedId, mStarPieceNeed);
        }
        MR::callAppearAllGroupMember(this);
    } else {
        MR::callMakeActorAppearedAllGroupMember(this);
    }

    if (MR::isValidSwitchA(this)) {
        MR::onSwitchA(this);
    }
}

bool TicoFat::tryMetamorphosis() {
    if (mItemType == -1) {
        setNerve(GET_NERVE(TicoFat, TicoFatNrvDemo));
        MR::callRequestMovementOnAllGroupMember(this);
        MR::requestMovementOn(mGoods0);
        MR::requestMovementOn(mGoods1);
    } else {
        setNerve(GET_NERVE(TicoFat, TicoFatNrvMeta));
    }

    return true;
}

void TicoFat::emitScreenEffect() {
    MR::emitEffect(this, "TicoFatScreenEffect")->setHostMtx(mScreenEffectMtx);
    MR::emitEffect(this, "TicoFatScreenEffectLight")->setHostMtx(mScreenLightMtx);
}

void TicoFat::updateScreenEffect() {
    // The screen effect faces the camera 500 in front of it toward the Tico.
    Vec3f toTico = mPosition - MR::getCamPos();
    MR::normalizeOrZero(&toTico);
    MR::makeMtxFrontUpPos(&mScreenEffectMtx, -MR::getCamZdir(), MR::getCamYdir(), MR::getCamPos() + (toTico * cNum_updateScreenEffect_f1));
    MR::makeMtxFrontUpPos(&mScreenLightMtx, -MR::getCamZdir(), MR::getCamYdir(), mPosition);
}

s32 TicoFat::getDanceSeTranspose() const {
    // The eating sound's transpose: one step up every 50 fed, at most 32.
    s32 transpose = (mStarPieceNeed - mStarPieceLeft - 1) / 50;
    if (transpose < 0) {
        transpose = 0;
    }

    if (transpose > 32) {
        return 32;
    }

    return transpose;
}

void TicoFat::startAbsorbSound() {
    if (mAbsorbSoundStep <= 0) {
        MR::startSound(this, "SE_SM_TICOFAT_ABSORB");
        mAbsorbSoundStep = 5;
    }
}

void TicoFat::updateGuidance() {
    bool isNear = MR::isNearPlayer(mHungryTalkCtrl, cNum_updateGuidance_isNearPlayer_1);
    if (isNear && MR::requestTicoSeedGuidanceForce()) {
        MR::requestCounterLayoutAppearanceForTicoEat(false);
    }

    Vec3f screenPos;
    if (MR::calcScreenPosition(&screenPos, mPosition)) {
        bool req = MR::requestTicoSeedGuidance(mGuidanceId);
        isNear = (isNear | req) != 0;
        if (req) {
            MR::requestCounterLayoutAppearanceForTicoEat(0);
        }
    }

    if (isNear) {
        mNearStep = 8;
    } else {
        s32 step = mNearStep - 1;
        mNearStep = step > 0 ? step : 0;
    }
}

void TicoFat::updatePointing() {
    bool isPointing;

    if (isNerve(GET_NERVE(TicoFat, TicoFatNrvEat))) {
        isPointing = MR::isStarPointerPointing1PWithoutCheckZ(this, nullptr, false, false);
    } else {
        isPointing = MR::isStarPointerPointing(this, 0, false, nullptr);
    }

    if (isPointing) {
        mPointingStep = 4;
    } else {
        s32 step = mPointingStep - 1;
        mPointingStep = step > 0 ? step : 0;
    }
}

void TicoFat::updateGoods() {
    if (MR::isNearPlayer(this, cNum_updateGoods_isNearPlayer_1)) {
        MR::tryStartBck(mGoods0, "TicoFatGoodsStarPiece");
        MR::tryStartBck(mGoods1, "TicoFatGoodsStarPiece");
    } else {
        MR::tryStartBck(mGoods0, "TicoFatGoodsStarPieceFar");
        MR::tryStartBck(mGoods1, "TicoFatGoodsStarPieceFar");
    }
}

void TicoFat::exeReaction() {
    if (MR::isFirstStep(this) && !mIsSpinAttackedStart) {
        mScaleController->startHitReaction();
    }

    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED");
        MR::startSound(this, "SE_SV_TICOFAT_TRAMPLED");
    }

    if (isPointingSe()) {
        MR::startPointerHitSound();
        MR::startSound(this, "SE_SV_TICOFAT_POINT");
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SM_TICOFAT_SPIN");
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SV_TICOFAT_STAR_PIECE_HIT");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

void TicoFat::exePrep() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, getScaledActionName(this, ACTION_WAIT));
    }

    if (!MR::tryStartReactionAndPushNerve(this, GET_NERVE(TicoFat, TicoFatNrvReaction)) && !MR::isPlayerElementModeInvincible()) {
        if (mStartEat) {
            setNerve(GET_NERVE(TicoFat, TicoFatNrvWait));
        } else if (mMsgCtrl != nullptr) {
            MR::tryTalkNearPlayerAndStartTalkAction(this);
        }
    }
}

void TicoFat::exeWait() {
    if (MR::isFirstStep(this)) {
        const char* exciteAction = getScaledActionName(this, ACTION_EXCITE);
        mParam.mWaitAction = getScaledActionName(this, ACTION_WAIT);
        mParam.mTalkAction = exciteAction;
        mParam.mWaitTurnAction = 0;
        mParam.mTalkTurnAction = 0;
        mParam.mTurnWhenNear = 0;
        mParam.mTurnOnTalk = 0;
        setMessage(1);
    }

    if (!MR::tryStartReactionAndPushNerve(this, GET_NERVE(TicoFat, TicoFatNrvReaction))) {
        if (mPointingStep > 0) {
            setNerve(GET_NERVE(TicoFat, TicoFatNrvPoint));
        } else {
            turnToDefault(mParam.mTurnSpeed);
            MR::tryTalkNearPlayer(mHungryTalkCtrl);
            MR::tryStartTalkAction(this);
        }
    }
}

void TicoFat::exePoint() {
    if (MR::isFirstStep(this)) {
        setMessage(0);
        MR::startAction(this, getScaledActionName(this, ACTION_EXCITE));
    }

    if (!MR::tryStartReactionAndPushNerve(this, GET_NERVE(TicoFat, TicoFatNrvReaction))) {
        if (mPointingStep <= 0) {
            setNerve(GET_NERVE(TicoFat, TicoFatNrvWait));
        } else {
            turnToDefault(mParam.mTurnSpeed);
            MR::tryTalkForce(mHungryTalkCtrl);
        }
    }
}

void TicoFat::exeEat() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, getScaledActionName(this, ACTION_EAT_START));
    } else {
        f32 frame = MR::getBckFrame(this);
        if (MR::tryStartAction(this, getScaledActionName(this, ACTION_EAT))) {
            MR::setBckFrame(this, frame);
        }
    }

    MR::startLevelSound(this, "SE_SM_LV_TICOFAT_EATING", getDanceSeTranspose());
    if (!mCurrentFed) {
        setNerve(GET_NERVE(TicoFat, TicoFatNrvChem));
    }
}

void TicoFat::exeChem() {
    if (MR::isFirstStep(this)) {
        mEatSoundStep = 5;
        MR::startAction(this, "Chem");
    }

    if (mEatSoundStep > 0) {
        mEatSoundStep--;
        MR::startLevelSound(this, "SE_SM_LV_TICOFAT_EATING", getDanceSeTranspose());
    }

    if (MR::isBckLooped(this)) {
        if (mPointingStep > 0) {
            mNearStep = 8;
            setNerve(GET_NERVE(TicoFat, TicoFatNrvPoint));
        } else {
            mNearStep = 0;
            setNerve(GET_NERVE(TicoFat, TicoFatNrvWait));
        }
    }
}

void TicoFat::exeTest() {
    if (MR::isFirstStep(this)) {
        MR::tryStartAction(this, getScaledActionName(this, ACTION_EXCITE));
        setMessage(2);
    }

    if (MR::tryStartDemo(this, "変身")) {
        MR::invalidateHitSensors(this);
        MR::stopPlayerFpView();
        MR::tryTalkForceWithoutDemo(mHungryTalkCtrl);

        if (mTicoSeedId == 6) {
            MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "喜び");
        }

        MR::requestMovementOn(mGoods0);
        MR::requestMovementOn(mGoods1);
        mMeter->pauseOff();
        setNerve(GET_NERVE(TicoFat, TicoFatNrvFullness));
    }
}

void TicoFat::exeFullness() {
    if (MR::isFirstStep(this)) {
        MR::tryStartAction(this, getScaledActionName(this, ACTION_JOY));
        MR::startSound(this, "SE_SM_TICOFAT_MAX");
    }

    MR::startLevelSound(this, "SE_SM_LV_TICOFAT_GLAD", getDanceSeTranspose());
    if (MR::tryTalkForceWithoutDemoAtEnd(mHungryTalkCtrl)) {
        tryMetamorphosis();
    }
}

void TicoFat::exeMeta() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Metamorphosis");
        MR::startSound(this, "SE_SM_TICOFAT_META");
        MR::startSound(this, "SE_SM_TICOFAT_META_ITEM");
    }

    if (MR::isBckStopped(this)) {
        MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");
        disappear(true);
        kill();
    }
}

void TicoFat::exeDemo() {
    if (MR::isFirstStep(this)) {
        MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "変身");
        MR::startAction(this, "Demo");
        MR::startSound(this, "SE_SM_TICOFAT_META");
    }

    MR::turnPlayerToActor(this, cNum_exeDemo_f1);
    if (MR::isStep(this, cStep_exeDemo_1)) {
        MR::startBrk(this, "FatRed");
        MR::setBrkFrameAndStop(this, 0.0f);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");
        MR::hideModelAndOnCalcAnim(this);
        if (mGoods0) {
            MR::hideModelAndOnCalcAnim(mGoods0);
        }

        if (mGoods1) {
            MR::hideModelAndOnCalcAnim(mGoods1);
        }
    }

    if (MR::isBckStopped(this)) {
        MR::endMultiActorCamera(this, mCameraInfo, "変身", false);
        if (mItemType == -1 && MR::isExistRail(this)) {
            setNerve(GET_NERVE(TicoFat, TicoFatNrvFly));
        } else {
            MR::setNPCActorPos(this, mPosition);
            setNerve(GET_NERVE(TicoFat, TicoFatNrvWipeOut));
        }
    }
}

void TicoFat::exeFly() {
    if (MR::isFirstStep(this)) {
        mFlyCoord = 0.0f;
        MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "飛行");
        MR::startAction(this, "Fly");
        MR::startSound(this, "SE_DM_TICOFAT_MORPH_FLY");
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    MR::tryRumblePadVeryWeak(this, PLAYER_1);
    // Eases in over the first half of the path and out over the second.
    f32 pathRate = mFlyCoord / mShootPath->getTotalLength();
    if (pathRate > 1.0f) {
        pathRate = 1.0f;
    }

    f32 easeIn;

    if (pathRate < cNum_exeFly_f1) {
        easeIn = MR::getEaseInValue((cNum_exeFly_f2 * pathRate) * (cNum_exeFly_f3 * pathRate), 0.0f, cNum_exeFly_f4, 1.0f);
    } else {
        f32 restRate = (cNum_exeFly_f5 * (1.0f - pathRate));
        easeIn = (1.0f - MR::getEaseInValue((restRate * restRate), 0.0f, cNum_exeFly_f6, 1.0f));
    }

    Vec3f shootPos, direction;
    mShootPath->calcPosition(&shootPos, easeIn);
    mShootPath->calcDirection(&direction, easeIn, cNum_exeFly_f7);
    Vec3f up;
    MR::calcUpVec(&up, this);
    MR::blendQuatFrontUp(&mPoseQuat, mPoseQuat, direction, up, cNum_exeFly_f8, 0.0f);
    MR::setNPCActorPos(this, shootPos);
    mFlyCoord += mFlySpeed;
    if (easeIn >= 1.0f) {
        setNerve(GET_NERVE(TicoFat, TicoFatNrvWipeOut));
    }
}

void TicoFat::exeWipeOut() {
    if (MR::isFirstStep(this)) {
        MR::stopBck(this);
        MR::startSystemSE("SE_DM_TICOFAT_MORPH_WIPE_OUT");
        emitScreenEffect();
        MR::closeWipeWhiteFade(60);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    updateScreenEffect();

    if (!MR::isWipeActive()) {
        MR::emitEffect(this, "TicoFatScreenEffectFog")->setHostMtx(mScreenEffectMtx);
        MR::deleteEffect(this, "TicoFatScreenEffect");
        setNerve(GET_NERVE(TicoFat, TicoFatNrvWipeIn));
    }
}

void TicoFat::exeWipeIn() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_DM_TICOFAT_MORPH_WIPE_IN");
    }

    if (!MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
            if (MR::isValidSwitchA(this)) {
                MR::onSwitchA(this);
            }

            MR::startBck(this, "Dummy");
            MR::hideModelAndOnCalcAnim(this);
            MR::deleteEffect(this, "TicoFatLight");
            if (mGoods0) {
                MR::stopBck(mGoods0);
                mGoods0->kill();
            }

            if (mGoods1) {
                MR::stopBck(mGoods1);
                mGoods1->kill();
            }

            MR::openWipeWhiteFade(60);
        }

        if (!MR::isWipeActive()) {
            setNerve(GET_NERVE(TicoFat, TicoFatNrvInfo));
        }
    }
}

void TicoFat::exeInfo() {
    if (MR::isFirstStep(this)) {
        MR::startSubBGM("BGM_APPEAR_PLANET", false);
        appearInformation();
    }

    if (MR::isPressedJump(PLAYER_1)) {
        MR::startSystemSE("SE_SY_TALK_OK");
        MR::disappearInformationMessage();
        setNerve(GET_NERVE(TicoFat, TicoFatNrvAfter));
    }
}

void TicoFat::exeAfter() {

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::endMultiActorCamera(this, mCameraInfo, "飛行", false);
        MR::endDemo(this, "変身");
        disappear(true);
        kill();
    }
}
