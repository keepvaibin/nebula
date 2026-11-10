#include "Game/MapObj/MorphItemObjNeo.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f1, "calcAndSetBaseMtx.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f2, "calcAndSetBaseMtx.f2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f3, "calcAndSetBaseMtx.f3", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f4, "calcAndSetBaseMtx.f4", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f5, "calcAndSetBaseMtx.f5", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f6, "calcAndSetBaseMtx.f6", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f7, "calcAndSetBaseMtx.f7", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f8, "calcAndSetBaseMtx.f8", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f2, "receiveOtherMsg.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 190.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f3, "exeWait.f3", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f4, "exeWait.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f5, "exeWait.f5", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f6, "exeWait.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f7, "exeWait.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f8, "exeWait.f8", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppear_f1, "exeAppear.f1", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppear_f2, "exeAppear.f2", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwitchAppear_f1, "exeSwitchAppear.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f1, "exeFly.f1", 0.999f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFly_f2, "exeFly.f2", 0.2f);

namespace {
    // Model per PlayerItemStatus.
    const char* const cxNameList[] = {"-----", "PowerupHopper", "PowerUpBee", "PowerupTeresa", "PowerupIce", "PowerupFire", "PowerupFoo", "END"};
    // A switch-appear spirals in for cSpiralFrames, one turn per cSpiralTurnFrames, the radius shrinking to cSpiralRadiusMin.
    NEBULA_TUNABLE(u16, cSpiralFrames, 60);
    NEBULA_TUNABLE(u16, cSpiralTurnFrames, 30);
    NEBULA_TUNABLE(f32, cSpiralRadiusPerFrame, 2.0f);
    NEBULA_TUNABLE(f32, cSpiralRadiusMin, 30.0f);

    HitSensor* getBodySensor(LiveActor* pActor) {
        return pActor->getSensor("body");
    }
};  // namespace

MorphItemObjNeo::MorphItemObjNeo(const char* pName, s32 type) : LiveActor(pName), mMorphType(type), mActorCameraInfo() {
    mSpiralTime = 0;
    mAppearFrames = 0;
    mIsDemoPending = false;
}

void MorphItemObjNeo::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    mSpawnPosition = mPosition;
    mAngle = 0.0f;
    mBoundCount = 0;

    initModelManagerWithAnm(::cxNameList[mMorphType], nullptr, false);

    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_MORPH_ITEM, 4, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));

    MR::initStarPointerTargetAtPos(this, cNum_init_f3, &mPosition, Vec3f(0.0f, cNum_init_f4, 0.0f));

    initBinder(cNum_init_f5, cNum_init_f6, 0);
    MR::offBind(this);

    s32 spiralAppear = -1;
    MR::getPlacementArg0NoInit(rIter, &spiralAppear);

    s32 bind = -1;
    MR::getPlacementArg1NoInit(rIter, &bind);

    s32 noFloat = -1;
    MR::getPlacementArg2NoInit(rIter, &noFloat);

    s32 container = -1;
    MR::getPlacementArg3NoInit(rIter, &container);

    s32 shadow = -1;
    MR::getPlacementArg4NoInit(rIter, &shadow);

    s32 cameras = -1;
    MR::getPlacementArg5NoInit(rIter, &cameras);

    s32 rotate = -1;
    MR::getPlacementArg6NoInit(rIter, &rotate);

    s32 needsRedDriver = -1;
    MR::getPlacementArg7NoInit(rIter, &needsRedDriver);

    NebulaMtxCopy(getBaseMtx(), mBaseMtx);

    mIsSwitchAppearOnce = spiralAppear == 1;

    // -1 (unset) binds on appear but not at placement.
    mIsBind = bind;

    if (mIsBind == 1) {
        MR::onBind(this);
    } else {
        MR::offBind(this);
    }

    mIsFloating = noFloat != 1;

    if (mMorphType == PlayerItemStatus_Hopper) {
        MR::connectToSceneIndirectMapObjStrongLight(this);
    } else {
        MR::connectToSceneNoSilhouettedMapObjStrongLight(this);
    }

    // Unset: a crystal box; 0: a bubble; anything else: bare.
    switch (container) {
    case 0:
        mContainerType = Container_Bubble;
        break;
    case -1:
        mContainerType = Container_CrystalBox;
        break;
    default:
        mContainerType = Container_None;
        break;
    }

    if (mContainerType == Container_CrystalBox) {
        mContainerModel = new ModelObj("クリスタルボックス", "CrystalBox", mBaseMtx, MR::DrawBufferType_CrystalBox, -2, -2, false);
        mContainerModel->initWithoutIter();
        mContainerModel->appear();
        MR::startBck(mContainerModel, "CrystalBox");

        mContainerBreakModel = new ModelObj("クリスタル破壊", "CrystalBoxBreak", mBaseMtx, MR::DrawBufferType_CrystalBox, -2, -2, false);
        mContainerBreakModel->makeActorDead();
    } else if (mContainerType == Container_Bubble) {
        mContainerModel = new ModelObj("アイテム泡", "ItemBubble", nullptr, -2, -2, -2, false);
        mContainerModel->initWithoutIter();
        mContainerModel->mPosition = mPosition;
        mContainerModel->mRotation = mRotation;
        mContainerModel->appear();

        mContainerBreakModel = nullptr;

        MR::startBck(mContainerModel, "Move");
    } else {
        mContainerModel = nullptr;
        mContainerBreakModel = nullptr;
    }

    // The shadow drops along the item's own gravity or the scene's, once or every frame; 3 and 4 also clip at 2000.
    bool hasShadow = true;
    bool ownGravity = false;
    bool everyFrame = false;
    switch (shadow) {
    case -1:
        ownGravity = true;
        break;
    case 1:
        everyFrame = true;
        break;
    case 2:
        ownGravity = true;
        everyFrame = true;
        break;
    case 3:
    case 4:
        MR::setClippingTypeSphere(this, cNum_init_f7);
        ownGravity = true;
        break;
    default:
        hasShadow = false;
        break;
    }

    if (hasShadow) {
        MR::initShadowVolumeSphere(this, cNum_init_f8);
        MR::setShadowDropLength(this, nullptr, cNum_init_f9);
        MR::onShadowVolumeCutDropLength(this, nullptr);

        if (ownGravity) {
            if (everyFrame) {
                MR::onCalcShadowDropPrivateGravity(this, nullptr);
            } else {
                MR::onCalcShadowDropPrivateGravityOneTime(this, nullptr);
            }
        } else if (everyFrame) {
            MR::onCalcShadow(this, nullptr);
        } else {
            MR::onCalcShadowOneTime(this, nullptr);
        }
    }

    initEffectKeeper(4, "MorphItemObj");
    initSound(4, false);

    mCameraMode = 0;

    if (cameras > 0) {
        if (cameras == 1) {
            MR::initMultiActorCamera(this, rIter, &mActorCameraInfo, "出現1");
            mCameraMode = 1;
        } else {
            MR::initMultiActorCamera(this, rIter, &mActorCameraInfo, "出現1");
            MR::initMultiActorCamera(this, rIter, &mActorCameraInfo, "出現2");

            mCameraMode = 2;
        }

        mCameraTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    }

    mRotateMode = rotate;

    MR::getRotatedAxisZ(&mZRotation, mRotation);

    initNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait));

    MR::validateClipping(this);

    makeActorAppeared();

    MR::validateHitSensors(this);

    mIsUseSwitch = false;

    if (MR::isValidInfo(rIter)) {
        MR::useStageSwitchSleep(this, rIter);
        MR::useStageSwitchWriteDead(this, rIter);

        if (MR::useStageSwitchReadAppear(this, rIter)) {
            makeActorDead();

            MR::syncStageSwitchAppear(this);

            mIsUseSwitch = true;
        }
    }

    if (mMorphType == PlayerItemStatus_Foo && needsRedDriver == 1 && !MR::isOnGameEventFlagRedDriver()) {
        makeActorDead();
    }

    MR::tryRegisterDemoCast(this, rIter);
}

void MorphItemObjNeo::initAfterPlacement() {
}

void MorphItemObjNeo::appear() {
    LiveActor::appear();

    if (mCameraMode != 0) {
        mIsDemoPending = true;
    }

    mSpiralTime = mIsSwitchAppearOnce ? ::cSpiralFrames : 0;

    const bool switchAppear = mCameraMode != 0 || mIsSwitchAppearOnce;

    if (switchAppear) {
        setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvSwitchAppear));

        MR::invalidateHitSensors(this);
        MR::invalidateClipping(this);
    } else {
        setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2));
    }

    mIsSwitchAppearOnce = false;
    mIsUseSwitch = false;

    if (mIsBind != 0) {
        MR::onBind(this);
    }

    if (!switchAppear) {
        switch (mMorphType) {
        case PlayerItemStatus_Ice:
            MR::emitEffect(this, "AppearIceFlower");
            MR::startSound(this, "SE_OJ_MORPH_FLOWER_APPEAR");
            break;
        case PlayerItemStatus_Fire:
            MR::emitEffect(this, "AppearFireFlower");
            MR::startSound(this, "SE_OJ_MORPH_FLOWER_APPEAR");
            break;
        case PlayerItemStatus_Hopper:
            MR::emitEffect(this, "AppearHopper");
            MR::startSound(this, "SE_OJ_MORPH_ITEM_APPEAR");
            break;
        case PlayerItemStatus_Foo:
            MR::emitEffect(this, "AppearFoo");
            MR::startSound(this, "SE_OJ_MORPH_FLOWER_APPEAR");
            break;
        default:
            break;
        }
    }

    if (mMorphType >= PlayerItemStatus_Hopper && mMorphType <= PlayerItemStatus_Foo) {
        MR::startBck(this, "Appear");
    }
}

void MorphItemObjNeo::control() {
    if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvFly))) {
        return;
    }

    if (mIsDemoPending && MR::tryStartDemoWithoutCinemaFrame(this, "出現")) {
        mIsDemoPending = false;

        Pos3f* pMtx = &mCameraTargetMtx->mMatrix;
        pMtx->set(getBaseMtx());
        MR::startMultiActorCameraTargetSelf(this, mActorCameraInfo, "出現1");
        mAppearFrames = MR::getMultiActorCameraFrames(this, mActorCameraInfo, "出現1");

        if (mAppearFrames != 0) {
            mAppearFrames--;
        }

        MR::requestMovementOn(this);

        MR::pauseOffCameraDirector();
    }

    // mRotateMode bit 1: spins when free; bit 2: spins inside its container or while its switch is unread.
    if (!isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvAppear)) && !isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvSwitchAppear))) {
        if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait))) {
            if (mContainerType == Container_None && !mIsUseSwitch) {
                if ((mRotateMode & 1) != 0) {
                    mAngle += cNum_control_f1;
                }
            } else if ((mRotateMode & 2) != 0) {
                mAngle += cNum_control_f2;
            }
        } else if ((mRotateMode & 1) != 0) {
            mAngle += cNum_control_f3;
        }
    }

    mAngle = MR::repeatDegree(mAngle);
}

void MorphItemObjNeo::calcAndSetBaseMtx() {
    if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait))) {
        if (mContainerType == Container_CrystalBox) {
            MtxPtr pMtx = MR::getJointMtx(mContainerModel, "CrystalBox");
            NebulaMtxCopy(pMtx, getBaseMtx());

            Vec3f yDir;
            MR::extractMtxYDir(getBaseMtx(), &yDir);
            MR::addTransMtx(getBaseMtx(), yDir * -cNum_calcAndSetBaseMtx_f1);

            pMtx = MR::tmpMtxScale(cNum_calcAndSetBaseMtx_f2, cNum_calcAndSetBaseMtx_f3, cNum_calcAndSetBaseMtx_f4);
            NebulaMtxConcat(getBaseMtx(), pMtx, getBaseMtx());
        } else if (mContainerType == Container_Bubble) {
            MtxPtr pMtx = mContainerModel->getBaseMtx();
            NebulaMtxCopy(pMtx, getBaseMtx());

            Vec3f yDir;
            MR::extractMtxYDir(getBaseMtx(), &yDir);
            MR::addTransMtx(getBaseMtx(), yDir * -cNum_calcAndSetBaseMtx_f5);

            pMtx = MR::tmpMtxScale(cNum_calcAndSetBaseMtx_f6, cNum_calcAndSetBaseMtx_f7, cNum_calcAndSetBaseMtx_f8);
            NebulaMtxConcat(getBaseMtx(), pMtx, getBaseMtx());
        } else {
            LiveActor::calcAndSetBaseMtx();

            NebulaMtxConcat(getBaseMtx(), MR::tmpMtxRotYDeg(mAngle), getBaseMtx());
        }
    } else {
        LiveActor::calcAndSetBaseMtx();

        NebulaMtxConcat(getBaseMtx(), MR::tmpMtxRotYDeg(mAngle), getBaseMtx());

        if (mSpiralTime != 0) {
            Pos3f rotMtx;
            NebulaMtxRotAxisRad(rotMtx, mVelocity, mSpiralTime * (TWO_PI / ::cSpiralTurnFrames));

            Vec3f offset;
            NebulaMtxMultVec(rotMtx, mZRotation, offset);

            offset.setLength(mSpiralTime * ::cSpiralRadiusPerFrame + ::cSpiralRadiusMin);
            MR::addTransMtx(getBaseMtx(), offset);
        }
    }
}

void MorphItemObjNeo::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mContainerType != Container_CrystalBox || mContainerModel == nullptr) {
        return;
    }

    if (MR::isDead(mContainerModel)) {
        return;
    }

    MR::sendMsgPush(pReceiver, pSender);
}

bool MorphItemObjNeo::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvAppear))) {
        return false;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if ((MR::isMsgPlayerHitAll(msg) || MR::isMsgPlayerHipDrop(msg)) && isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait))) {
        if (mContainerModel != nullptr) {
            mContainerModel->kill();

            if (mContainerBreakModel != nullptr) {
                mContainerBreakModel->appear();

                MR::startBck(mContainerBreakModel, "Break");

                MR::startSound(this, "SE_OJ_CRYSTAL_CAGE_S_BREAK");

                setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvAppear));
            } else {
                MR::startSound(this, "SE_OJ_ITEM_BUBBLE_BREAK");

                setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2));
            }

            return true;
        } else {
            return false;
        }
    }

    return false;
}

bool MorphItemObjNeo::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isDead(this)) {
        return false;
    }

    if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvAppear))) {
        return false;
    }

    if (MR::isMsgItemGet(msg)) {
        if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait))) {
            if (mContainerType == Container_Bubble) {
                MR::startSound(this, "SE_OJ_ITEM_BUBBLE_BREAK");

                if (mContainerModel != nullptr) {
                    mContainerModel->kill();
                }

                setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2));

                if (mIsBind != 0) {
                    MR::onBind(this);
                }
            }

            if (mContainerType != Container_None) {
                return false;
            }
        }

        if (MR::isPlayerElementModeInvincible()) {
            if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2)) ||
                (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait)) && mContainerType == Container_None)) {
                setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvFly));

                mVelocity = MR::getSensorPos(pReceiver) - MR::getSensorPos(pSender);
                mVelocity.setLength(cNum_receiveOtherMsg_f1);

                MR::calcGravity(this);

                mVelocity += -mGravity * cNum_receiveOtherMsg_f2;
            }

            return false;
        }

        // The morphs the player already has only make the invalid sound.
        bool alreadyHas = false;
        switch (mMorphType) {
        case PlayerItemStatus_Normal:
            break;
        case PlayerItemStatus_Ice:
        case PlayerItemStatus_Fire:
        case PlayerItemStatus_Foo:
        case PlayerItemStatus_Invincible:
            MR::startSystemSE("SE_SY_POWER_UP_2");
            break;
        case PlayerItemStatus_Hopper:
            alreadyHas = MR::isPlayerElementModeHopper();
            break;
        case PlayerItemStatus_Bee:
            alreadyHas = MR::isPlayerElementModeBee();
            break;
        case PlayerItemStatus_Teresa:
            alreadyHas = MR::isPlayerElementModeTeresa();
            break;
        default:
            if (mMorphType > 0) {
                MR::startSystemSE("SE_SY_POWER_UP");
            }
            break;
        }

        if (mMorphType >= PlayerItemStatus_Hopper && mMorphType <= PlayerItemStatus_Teresa) {
            if (alreadyHas) {
                MR::startSound(this, "SE_OJ_MORPH_ITEM_INVALID");
            } else {
                MR::startSystemSE("SE_SY_MORPH");
            }
        }

        MR::changePlayerItemStatus(mMorphType);

        mAngle = 0.0f;

        calcAndSetBaseMtx();

        makeActorDead();

        return true;
    }

    return false;
}

void MorphItemObjNeo::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    makeActorDead();

    mPosition = mSpawnPosition;
    mVelocity.zero();

    mBoundCount = 0;

    mScale.set(1.0f);

    makeActorAppeared();

    MR::emitEffect(this, "ItemRevival");

    MR::startSound(this, "SE_OJ_MORPH_ITEM_RECOVER");

    if (mContainerModel != nullptr) {
        mContainerModel->appear();
    }

    setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait));

    if (mIsBind != 1) {
        MR::offBind(this);
    }
}

void MorphItemObjNeo::makeActorDead() {
    if (mContainerModel != nullptr) {
        mContainerModel->makeActorDead();
    }

    LiveActor::makeActorDead();
}

void MorphItemObjNeo::glowEffect() {
    switch (mMorphType) {
    case PlayerItemStatus_Hopper:
        MR::emitEffect(this, "GlowHopper");
        break;
    case PlayerItemStatus_Bee:
        MR::emitEffect(this, "GlowBee");
        break;
    case PlayerItemStatus_Teresa:
        MR::emitEffect(this, "GlowTeresa");
        break;
    case PlayerItemStatus_Ice:
        MR::emitEffect(this, "GlowIce");
        break;
    case PlayerItemStatus_Fire:
        MR::emitEffect(this, "GlowFire");
        break;
    case PlayerItemStatus_Foo:
        MR::emitEffect(this, "GlowFoo");
        break;
    default:
        break;
    }
}

void MorphItemObjNeo::exeWait() {
    if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvDemo))) {
        mAngle = 0.0f;
        return;
    }

    if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait))) {
        if (mContainerType == Container_CrystalBox) {
            ::getBodySensor(this)->mRadius = cNum_exeWait_f1;
            MR::setSensorOffset(this, "body", Vec3f(0.0f, cNum_exeWait_f2, 0.0f));
        } else if (mContainerType == Container_Bubble) {
            ::getBodySensor(this)->mRadius = cNum_exeWait_f3;
            MR::setSensorOffset(this, "body", Vec3f(0.0f, 0.0f, 0.0f));
        } else {
            glowEffect();
        }
    } else {
        ::getBodySensor(this)->mRadius = cNum_exeWait_f4;
        MR::setSensorOffset(this, "body", Vec3f(0.0f, cNum_exeWait_f5, 0.0f));

        glowEffect();
    }

    if (isDemo()) {
        return;
    }

    if (isNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2)) && mIsFloating && mBoundCount <= 3) {
        if (MR::isBindedGround(this) && mVelocity.dot(mGravity) > 0.0f) {
            if (mVelocity.length() < cNum_exeWait_f6) {
                mVelocity.zero();

                return;
            }

            if (mBoundCount < 3) {
                mVelocity = mVelocity * -cNum_exeWait_f7;
            } else {
                mVelocity.zero();
            }

            mBoundCount++;

            return;
        }

        MR::calcGravity(this);
        mVelocity += mGravity * cNum_exeWait_f8;

        return;
    }

    mVelocity.zero();

    if (MR::isInvalidClipping(this)) {
        MR::validateClipping(this);
    }
}

void MorphItemObjNeo::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::calcGravity(this);

        mVelocity = mGravity * -cNum_exeAppear_f1;

        if (mIsBind != 0) {
            MR::onBind(this);
        }
    }

    if (mContainerBreakModel != nullptr) {
        if (MR::isBckStopped(mContainerBreakModel) && getNerveStep() > NebulaFrameClock::stepsOfMilliseconds(500)) {
            setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2));
        }

        mVelocity += mGravity * cNum_exeAppear_f2;
    } else {
        setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2));
    }
}

void MorphItemObjNeo::exeSwitchAppear() {
    if (MR::isFirstStep(this)) {
        MR::calcGravity(this);

        if (mSpiralTime != 0) {
            MR::startBck(this, "Appear");

            if (mMorphType == PlayerItemStatus_Teresa) {
                MR::emitEffect(this, "AppearTeresa");
            } else if (mMorphType == PlayerItemStatus_Hopper) {
                MR::emitEffect(this, "AppearHopper");
            } else {
                MR::emitEffect(this, "AppearBee");
            }

            if (mMorphType != PlayerItemStatus_Ice && mMorphType != PlayerItemStatus_Fire) {
                MR::startSound(this, "SE_OJ_MORPH_ITEM_APPEAR");
            }
        } else {
            MR::hideModel(this);
        }

        MR::invalidateHitSensors(this);

        mVelocity = mGravity * -cNum_exeSwitchAppear_f1;
    }

    if (getNerveStep() == NebulaFrameClock::stepsOfSeconds(1)) {
        MR::showModel(this);

        if (mMorphType == PlayerItemStatus_Ice) {
            MR::startBck(this, "Appear");
            MR::emitEffect(this, "AppearIceFlower");
            MR::startSound(this, "SE_OJ_MORPH_FLOWER_APPEAR");
        }

        if (mMorphType == PlayerItemStatus_Fire) {
            MR::startBck(this, "Appear");
            MR::emitEffect(this, "AppearFireFlower");
            MR::startSound(this, "SE_OJ_MORPH_FLOWER_APPEAR");
        }
    }

    if (mCameraMode != 0 && mAppearFrames != 0 && --mAppearFrames == 0) {
        MR::endMultiActorCamera(this, mActorCameraInfo, "出現1", false);
        MR::endMultiActorCamera(this, mActorCameraInfo, "出現2", false);

        mCameraMode--;

        if (mCameraMode == 0) {
            MR::endDemo(this, "出現");
            MR::validateClipping(this);
        } else {
            Pos3f* pMtx = &mCameraTargetMtx->mMatrix;
            pMtx->set(getBaseMtx());

            MR::startMultiActorCameraTargetOther(this, mActorCameraInfo, "出現2", CameraTargetArg(nullptr, mCameraTargetMtx, nullptr, nullptr));

            mAppearFrames = MR::getMultiActorCameraFrames(this, mActorCameraInfo, "出現2");
        }
    }

    if (mSpiralTime != 0) {
        mSpiralTime--;
    }

    if (mSpiralTime == 0) {
        MR::validateHitSensors(this);
        mVelocity.zero();

        if (mCameraMode == 0) {
            setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait2));
        }
    }
}

void MorphItemObjNeo::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    mVelocity.mult(cNum_exeFly_f1);

    MR::calcGravity(this);

    mVelocity += mGravity * cNum_exeFly_f2;

    if (getNerveStep() == NebulaFrameClock::stepsOfSeconds(5)) {
        kill();
        setNerve(GET_NERVE(MorphItemObjNeo, MorphItemObjNeoNrvWait));
    }
}

bool MorphItemObjNeo::isDemo() const {
    return mSpiralTime != 0 || mAppearFrames != 0;
}
