#include "Game/Ride/SpaceCocoon.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/PlantPoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_math.h>
#include "Game/System/NebulaInput.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sAimAnimInterpole = 10;
    const f32 sAimDistanceToStretch = 300.0f;
    const f32 sAimDistanceToWait = 200.0f;
    const f32 sAccelRate = 0.02f;
    const f32 sFrictionRate = 0.95f;
    const f32 sDistanceToStop = 1.0f;
    const f32 sSpeedMinToStop = 0.1f;
    const f32 sPointerSpeedMin = 5.0f;
    const f32 sPointerSpeedAccelRate = 5.0f;
    const f32 sDistanceBindInvalid = 300.0f;
    const s32 sTouchTimerMax = 20;
    const s32 sStepFreeInvalidMin = 5;
    const f32 sDistanceMax = 500.0f;
    const f32 sFollowRate = 0.03f;
    const f32 sAimDistanceMin = 100.0f;
    const f32 sAimRumbleDistanceWeak = 100.0f;
    const f32 sAimRumbleDistanceStrong = 500.0f;
    const f32 sBindAttackSpeed = 60.0f;
    const f32 sBindAttackFrictionRate = 0.98f;
    const s32 sDefaultStepBindAttack = 60;
    const s32 sStepFriction = 90;
    const s32 sBindAttackInvalidTime = 20;
    const s32 sStepToStopScene = 1;
    const s32 sStepStopScene = 8;
    const f32 sDistanceBindCancel = 3000.0f;
    const f32 sCancelJumpSpeedXZ = 0.0f;
    const f32 sCancelJumpSpeedY = 0.0f;
    const s32 sWidthBottomPointNum = 3;
    const f32 sWidthTop = 2.0f;
    const f32 sWidthBottom = 1.0f;
    const f32 sStarPieceAccel = 5.0f;
    const f32 sDefaultHeight = 100.0f;
    const f32 sPointInterval = 10.0f;
    const f32 sDrawScaleX = 25.0f;
    const f32 sDrawScaleZ0 = 10.0f;
    const f32 sDrawScaleZ1 = 25.0f;
    const f32 sTexDiffU = -0.5f;

};  // namespace

namespace NrvSpaceCocoon {
    NEW_NERVE(SpaceCocoonNrvStop, SpaceCocoon, Stop);
    NEW_NERVE(SpaceCocoonNrvFree, SpaceCocoon, Free);
    NEW_NERVE(SpaceCocoonNrvFreeInvalid, SpaceCocoon, FreeInvalid);
    NEW_NERVE(SpaceCocoonNrvKinopioLand, SpaceCocoon, BindLand);
    NEW_NERVE(SpaceCocoonNrvKinopioWait, SpaceCocoon, BindWait);
    NEW_NERVE(SpaceCocoonNrvWaitKinopioAimDemo, SpaceCocoon, WaitKinopioAimDemo);
    NEW_NERVE(SpaceCocoonNrvKinopioAim, SpaceCocoon, BindAim);
    NEW_NERVE(SpaceCocoonNrvKinopioAttack, SpaceCocoon, BindAttack);
    NEW_NERVE(SpaceCocoonNrvKinopioAttackSuccess, SpaceCocoon, BindAttackSuccess);
    NEW_NERVE(SpaceCocoonNrvBindLand, SpaceCocoon, BindLand);
    NEW_NERVE(SpaceCocoonNrvBindWait, SpaceCocoon, BindWait);
    NEW_NERVE(SpaceCocoonNrvBindAim, SpaceCocoon, BindAim);
    NEW_NERVE(SpaceCocoonNrvBindAttack, SpaceCocoon, BindAttack);
    NEW_NERVE(SpaceCocoonNrvBindAttackSuccess, SpaceCocoon, BindAttackSuccess);
};  // namespace NrvSpaceCocoon

namespace {
    const f32 sTexStartU = 0.0f;
    static Color8 sColor(0xC8, 0xC8, 0xD7, 0xFF);
};  // namespace

SpaceCocoon::SpaceCocoon(const char* pName)
    : LiveActor(pName), mIsKinopioCameraFocused(), mNeutralPos(0.0f, 0.0f, 0.0f), mBasePos(0.0f, 0.0f, 0.0f), mSide(1.0f, 0.0f, 0.0f),
      mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mHeight(::sDefaultHeight), mPointerPos(0.0f, 0.0f, 0.0f), mPadChannel(-1),
      mCocoonPos(0.0f, 0.0f, 0.0f), mSpringVel(0.0f, 0.0f, 0.0f), mAttackTime(::sDefaultStepBindAttack), mTouchTime(), mRider(), mNumPoints(),
      mPlantPoints(), mCameraTargetMtx(), mCameraInfo(), mCocoonModel(), mTexture() {
    lumacore::setClass(this, lumacore::kClassId< SpaceCocoon >);
    mBaseMtx.identity();
    mTopMtx.identity();
}

void SpaceCocoon::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_Ride, MR::CalcAnimType_Ride, MR::DrawBufferType_None, MR::DrawType_SpaceCocoon);

    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mAttackTime);
    MR::getPlacementArg1NoInit(rIter, &mHeight);
    MR::getPlacementArg3NoInit(rIter, &mIsKinopioCameraFocused);

    mBasePos.set(mPosition);
    MR::calcActorAxis(&mSide, &mUp, &mFront, this);

    mNeutralPos.set(mUp);
    mNeutralPos.scale(mHeight);
    mNeutralPos.add(mPosition);

    mPosition.set(mNeutralPos);
    mCocoonPos.set(mNeutralPos);
    mPointerPos.set(mNeutralPos);

    initHitSensor(3);
    MR::addHitSensorTransferableBinder(this, "Bind", 8, 200.0f, Vec3f(0.0f, 100.0f, 0.0f));
    MR::addHitSensorMapObj(this, "Push", 8, 50.0f, Vec3f(0.0f, -50.0f, 0.0f));
    MR::addHitSensor(this, "Attack", ATYPE_SPRING_ATTACKER, 8, 150.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(50.0f, 50.0f, 8);
    MR::offBind(this);

    initSound(6, false);

    if (mIsKinopioCameraFocused) {
        MR::needStageSwitchWriteA(this, rIter);
    }

    initNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvStop));

    mNumPoints = mHeight / ::sPointInterval;
    // At least one: the top point is read every frame (an arg1 height below the interval would leave none).
    if (mNumPoints < 1) {
        mNumPoints = 1;
    }
    mPlantPoints = new PlantPoint*[mNumPoints];

    f32 delta = mHeight / (mNumPoints + 1);

    for (s32 idx = 0; idx < mNumPoints; idx++) {
        Vec3f spawnPos = mUp;
        spawnPos.scale((mNumPoints - idx) * delta);
        spawnPos.add(mBasePos);

        f32 thickness = 1.0f;
        if ((mNumPoints - 1) - idx < ::sWidthBottomPointNum) {
            thickness = MR::getEaseOutValue(static_cast< f32 >((mNumPoints - 1) - idx) / ::sWidthBottomPointNum, ::sWidthTop, ::sWidthBottom, 1.0f);
        }

        mPlantPoints[idx] = new PlantPoint(spawnPos, mUp, thickness);
    }

    PlantPoint* topPoint = mPlantPoints[0];
    mTopMtx.setXYZDir(topPoint->mSide, topPoint->mUp, topPoint->mFront);
    mTopMtx.setTrans(topPoint->mPosition);

    mCocoonModel = new PartsModel(this, "先端", "SpaceCocoon", mTopMtx, MR::DrawBufferType_NoSilhouettedMapObj, false);
    mCocoonModel->initWithoutIter();

    updateDrawPoints();
    MR::initStarPointerTarget(this, 300.0f, Vec3f(0.0f, 50.0f, 0.0f));

    MR::calcGravity(this);

    MR::initShadowVolumeSphere(this, 100.0f);
    MR::setClippingFar200m(this);

    mCameraTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "ウェイト");  // "wait"
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "狙い中");    // "aim"
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "攻撃中");    // "attack"

    mTexture = new GameTexture(MR::loadTexFromArc("SpaceCocoon", "SpaceCocoon.bti"));
    makeActorAppeared();
}

void SpaceCocoon::exeStop() {
    if (MR::isFirstStep(this)) {
        mTouchTime = 0;
    }

    tryTouch();
}

void SpaceCocoon::exeFree() {
    if (MR::isFirstStep(this)) {
        MR::validateHitSensor(this, "Bind");
    }

    tryTouch();

    if (updateSpringPoint()) {
        mSpringVel.zero();
        mCocoonPos.set(mNeutralPos);
        mPosition.set(mNeutralPos);
        setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvStop));
    } else {
        mPosition.set(mCocoonPos);
    }
}

void SpaceCocoon::exeFreeInvalid() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
    }

    updateSpringPoint();
    mPosition.set(mCocoonPos);

    if (MR::isGreaterStep(this, ::sStepFreeInvalidMin)) {
        if (mRider->mPosition.distance(mPosition) > ::sDistanceBindInvalid || MR::isOnGroundPlayer()) {
            mRider = nullptr;
            MR::validateClipping(this);
            MR::validateHitSensors(this);
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFree));
        }
    }
}

void SpaceCocoon::exeBindLand() {
    if (MR::isFirstStep(this)) {
        if (!isKinopioAttached()) {
            MR::startBckPlayer("CocoonLand", 1);
            MR::startMultiActorCameraTargetOther(this, mCameraInfo, "ウェイト", CameraTargetArg(mCameraTargetMtx));
            MR::startSound(mRider, "SE_PV_CATCH");
        } else {
            MR::startBckWithInterpole(mRider, "CocoonLand", 1);
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioWait));
            return;
        }
        MR::startSound(this, "SE_OJ_SPACE_COCOON_ATTACH");
    }

    if (isKinopioAttached()) {
        setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindWait));
        return;
    }

    if (!updateBindWait()) {
        tryTouch();
        if (MR::isBckStopped(mRider)) {
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindWait));
        }
    }
}

void SpaceCocoon::exeBindWait() {
    if (MR::isFirstStep(this)) {
        if (!isKinopioAttached()) {
            MR::startBckPlayer("CocoonWait");
        } else {
            MR::startBck(mRider, "CocoonWait");
            MR::validateClipping(this);
            MR::validateClipping(mRider);
            MR::sendSimpleMsgToActor(ACTMES_NPC_EVENT_TALK_ENABLE, mRider);
        }
        mVelocity.zero();
    }

    if (updateBindWait()) {
        return;
    }

    if (tryTouch()) {
        if (!isKinopioAttached()) {
            MR::startBckPlayer("CocoonReaction");
        }
    } else {
        if (!isKinopioAttached() && MR::isBckOneTimeAndStopped(mRider)) {
            MR::startBckPlayer("CocoonWait");
        }
    }
}

void SpaceCocoon::exeWaitKinopioAimDemo() {
    if (MR::tryStartDemoWithoutCinemaFrameValidStarPointer(this, "キノピオ狙い中")) {
        MR::startMultiActorCameraTargetPlayer(this, mCameraInfo, "狙い中");
        setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAim));
    }
}

void SpaceCocoon::exeBindAim() {
    if (MR::isFirstStep(this)) {
        if (isKinopioAttached()) {
            MR::sendSimpleMsgToActor(ACTMES_NPC_EVENT_TALK_DISABLE, mRider);
        }

        MR::startSound(this, "SE_OJ_SPACE_COCOON_DRAG_ST");

        if (mPadChannel == PLAYER_2) {
            MR::startCSSound2P("CS_DPD_HIT", nullptr);
        }
    }

    f32 dist = mPosition.distance(mNeutralPos);
    if (dist >= ::sAimDistanceMin) {
        MR::startLevelSound(this, "SE_OJ_LV_SPACE_COCOON_DRAG", ((dist - ::sAimDistanceMin) / (::sDistanceMax - ::sAimDistanceMin)) * 100.0f);
    }

    if (!tryRelease()) {
        updateHang();
    }
}

void SpaceCocoon::exeBindAttack() {
    if (MR::isFirstStep(this)) {
        if (!isKinopioAttached()) {
            MR::startBckPlayer("CocoonFly");
            MR::startSound(mRider, "SE_PV_JUMP_JOY");
        } else {
            MR::startBck(mRider, "CocoonFly");
            MR::invalidateClipping(this);
            MR::invalidateClipping(mRider);
            MR::startSound(mRider, "SE_SV_KINOPIO_TALK_GLAD_FLY");
        }
        MR::startSound(this, "SE_OJ_SPACE_COCOON_LAUNCH");
        MR::onBind(this);
    }

    updateBindAttack();

    if (tryAttackMap() || MR::isStep(this, mAttackTime)) {
        MR::endMultiActorCamera(this, mCameraInfo, "攻撃中", true);

        if (MR::isStep(this, mAttackTime) && !isKinopioAttached()) {
            MR::startBckPlayer("AirRotation");
        }

        endBind(Vec3f(0.0f, 0.0f, 0.0f), false);
    }
}

void SpaceCocoon::exeBindAttackSuccess() {
    updateBindAttack();

    if (MR::isStep(this, ::sStepToStopScene)) {
        MR::stopScene(::sStepStopScene);
    }

    if (MR::isStep(this, ::sStepToStopScene + 1)) {
        if (isKinopioAttached()) {
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAttack));
        } else {
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindAttack));
        }
    }
}

void SpaceCocoon::calcAnim() {
    if (isKinopioAttached()) {
        updateActorMtx();
        MR::setBaseTRMtx(mRider, mBaseMtx);
        mRider->mPosition.set(mPosition);
    }
}

void SpaceCocoon::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isKinopioAttached() && mRider == nullptr && MR::isSensorNpc(pReceiver) && pReceiver->receiveMessage(ACTMES_NPC_EVENT_START, pSender)) {
        mRider = pReceiver->mHost;
        MR::emitEffect(mCocoonModel, "Stick");
        Vec3f side = mSide;
        Vec3f front = mFront;
        side.scale(mSide.dot(mRider->mVelocity));
        front.scale(mFront.dot(mRider->mVelocity));
        mSpringVel.set(side);
        mSpringVel.add(front);
        mRider->mVelocity.zero();
        setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioWait));
        return;
    }

    if (mRider == nullptr || isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioWait)) ||
        isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFreeInvalid))) {
        if (!MR::isSensorType(pSender, ATYPE_SPRING_ATTACKER) &&
            (isKinopioAttached() || isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFreeInvalid)))) {
            MR::sendMsgPush(pReceiver, pSender);
        }
        return;
    }

    MR::tryGetItem(pSender, pReceiver);
}

bool SpaceCocoon::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isMsgStarPieceReflect(msg)) {
        return false;
    }

    if (isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFree)) || isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFreeInvalid))) {
        MR::addVelocitySeparateHV(this, pSender, pReceiver, ::sStarPieceAccel, 0.0f);
        mSpringVel.add(mVelocity);
        MR::zeroVelocity(this);
    }

    return true;
}

bool SpaceCocoon::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mRider != nullptr && MR::isSensor(pReceiver, "Bind")) {
        MR::endBindAndPlayerDamageMsg(this, msg);
        endBind(Vec3f(0.0f, 0.0f, 0.0f), true);
        return true;
    }

    return false;
}

bool SpaceCocoon::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFreeInvalid)) || isKinopioAttached()) {
            return false;
        }

        mRider = pSender->mHost;
        MR::invalidateClipping(this);
        Vec3f side = mSide;
        Vec3f front = mFront;
        side.scale(mSide.dot(*MR::getPlayerVelocity()));
        front.scale(mFront.dot(*MR::getPlayerVelocity()));
        mSpringVel.set(side);
        mSpringVel.add(front);
        MR::emitEffect(mCocoonModel, "Stick");
        mPosition.set(mNeutralPos);
        mCameraTargetMtx->mMatrix.set(mTopMtx);
        mCameraTargetMtx->mMatrix.setTrans(mPosition);
        MR::invalidateHitSensor(this, "Bind");
        if (MR::isStarPointerInScreen(PLAYER_1)) {
            MR::calcStarPointerPosOnPlane(&mPointerPos, mPosition, mUp, PLAYER_1, false);
        }
        setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindLand));
        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        updateActorMtx();
        MR::setBaseTRMtx(mRider, mBaseMtx);
        return true;
    }

    if (MR::isMsgIsRushTakeOver(msg)) {
        return !isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFreeInvalid));
    }

    if (MR::isMsgRushCancel(msg)) {
        endBind(Vec3f(::sCancelJumpSpeedXZ, ::sCancelJumpSpeedY, ::sCancelJumpSpeedXZ), false);
        return true;
    }

    return false;
}

bool SpaceCocoon::updateBindWait() {
    if (MR::getPlayerPos()->distance(mPosition) < ::sDistanceBindCancel && (!isKinopioAttached() || MR::isOnGroundPlayer())) {
        MR::reportStarPointerGrabTarget(this);
        if (MR::isStarPointerPointing(this, PLAYER_1, true, "弱")) {
            MR::requestStarPointerModeBlueStarReady(this);
        }

        if (MR::isStarPointerPointing(this, PLAYER_1, true, "弱") && MR::isPressedJump(PLAYER_1) &&
            MR::tryStartStarPointerCommandStream(this, &mPosition, PLAYER_1, false)) {
            mPadChannel = PLAYER_1;
        }

        if (mPadChannel >= 0) {
            if (isKinopioAttached()) {
                setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvWaitKinopioAimDemo));
            } else {
                MR::endMultiActorCamera(this, mCameraInfo, "ウェイト", true);
                MR::startMultiActorCameraTargetOther(this, mCameraInfo, "狙い中", CameraTargetArg(mCameraTargetMtx));
                setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindAim));
            }
            return true;
        }
    }

    updateSpringPoint();
    mPosition.set(mCocoonPos);
    return false;
}

bool SpaceCocoon::updateSpringPoint() {
    mSpringVel.mult(::sFrictionRate);

    Vec3f springDiff = mNeutralPos - mCocoonPos;

    mSpringVel.add(springDiff * ::sAccelRate);
    mCocoonPos.add(mSpringVel);

    updateDrawPoints();

    if (mSpringVel.length() < ::sSpeedMinToStop && mCocoonPos.distance(mNeutralPos) < ::sDistanceToStop) {
        mCocoonPos.set(mNeutralPos);
        mSpringVel.zero();
        return true;
    }

    return false;
}

void SpaceCocoon::updateHang() {
    f32 dist = mPosition.distance(mNeutralPos);

    if (dist >= ::sAimRumbleDistanceStrong) {
        MR::startLevelSound(this, "SE_OJ_LV_SPACE_COCOON_DRAG3");
    } else if (dist >= ::sAimRumbleDistanceWeak) {
        MR::startLevelSound(this, "SE_OJ_LV_SPACE_COCOON_DRAG2");
    }

    if (MR::isStarPointerInScreen(mPadChannel)) {
        MR::calcStarPointerPosOnPlane(&mPointerPos, mNeutralPos, mUp, mPadChannel, false);
    }

    Vec3f pos = mPointerPos;
    if (pos.distance(mNeutralPos) > ::sDistanceMax) {
        pos.sub(mNeutralPos);
        MR::normalize(&pos);
        pos.scale(::sDistanceMax);
        pos.add(mNeutralPos);
    }

    mPosition = pos * ::sFollowRate + mPosition * (1.0f - ::sFollowRate);
    mCocoonPos.set(mPosition);
    updateDrawPoints();

    if (mRider == nullptr) {
        return;
    }

    if (dist < ::sAimDistanceToWait && !MR::isBckPlaying(mRider, "CocoonWait")) {
        if (isKinopioAttached()) {
            MR::startBckWithInterpole(mRider, "CocoonWait", ::sAimAnimInterpole);
        } else {
            MR::startBckPlayer("CocoonWait", ::sAimAnimInterpole);
        }
    }

    if (dist > ::sAimDistanceToStretch && !MR::isBckPlaying(mRider, "CocoonStretch")) {
        if (isKinopioAttached()) {
            MR::startBckWithInterpole(mRider, "CocoonStretch", ::sAimAnimInterpole);
        } else {
            MR::startBckPlayer("CocoonStretch", ::sAimAnimInterpole);
        }
    }
}

void SpaceCocoon::updateBindAttack() {
    if (getNerveStep() >= mAttackTime - ::sStepFriction) {
        mVelocity.mult(::sBindAttackFrictionRate);
    }
    updateSpringPoint();
}

void SpaceCocoon::updateActorMtx() {
    if (isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindAttack)) || isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindAttackSuccess)) ||
        isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAttack)) || isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAttackSuccess))) {
        mBaseMtx.setTrans(mPosition);
    } else {
        mBaseMtx.set(mTopMtx);
    }
}

void SpaceCocoon::updateDrawPoints() {
    Vec3f prevPos = mCocoonPos;
    f32 delta = mHeight / (mNumPoints + 1);

    for (s32 idx = 0; idx < mNumPoints; idx++) {
        f32 t = MR::getEaseOutValue(static_cast< f32 >(idx + 1) / static_cast< f32 >(mNumPoints + 1), 0.0f, 1.0f, 1.0f);
        t = MR::getEaseOutValue(t, 0.0f, 1.0f, 1.0f);

        Vec3f baseUp = mUp * ((idx + 1) * delta);

        Vec3f pos;
        pos.x = mCocoonPos.x * (1.0f - t) + mNeutralPos.x * t;
        pos.y = mCocoonPos.y * (1.0f - t) + mNeutralPos.y * t;
        pos.z = mCocoonPos.z * (1.0f - t) + mNeutralPos.z * t;

        pos.sub(baseUp);

        Vec3f up = prevPos - pos;
        MR::normalize(&up);

        Vec3f side = mSide;
        Vec3f front = mFront;

        if (MR::isNearZero(side.dot(up))) {
            side.cross(up, front);
            MR::normalize(&side);
            front.cross(side, up);
            MR::normalize(&front);
        } else {
            front.cross(side, up);
            MR::normalize(&front);
            side.cross(up, front);
            MR::normalize(&side);
        }

        mPlantPoints[idx]->setAxisAndPos(side, up, front, pos);

        prevPos.set(pos);
    }

    const PlantPoint* top = mPlantPoints[0];
    mTopMtx.setXYZDir(top->mSide, top->mUp, top->mFront);
    mTopMtx.setTrans(top->mPosition);
}

namespace {
    bool tryTouchPointer(LiveActor* pActor, s32 padChannel) {
        return MR::isStarPointerPointing(pActor, padChannel, false, "弱") && MR::getStarPointerScreenSpeed(padChannel) > ::sPointerSpeedMin;
    }
};  // namespace

bool SpaceCocoon::tryTouch() {
    if (mTouchTime > 0) {
        mTouchTime--;
        return false;
    }

    s32 touchChannel = -1;
    if (::tryTouchPointer(this, PLAYER_1)) {
        touchChannel = PLAYER_1;
    } else if (::tryTouchPointer(this, PLAYER_2)) {
        touchChannel = PLAYER_2;
    }

    if (touchChannel >= 0) {
        Vec3f pointerVel(0.0f, 0.0f, 0.0f);
        MR::calcStarPointerWorldVelocityDirectionOnPlane(&pointerVel, mNeutralPos, mUp, touchChannel);
        pointerVel.scale(::sPointerSpeedAccelRate);
        mSpringVel.add(pointerVel);

        MR::startSound(this, "SE_OJ_SPACE_COCOON_BOUND");

        if (isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvStop))) {
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFree));
        }

        mTouchTime = ::sTouchTimerMax;
        return true;
    }

    return false;
}

bool SpaceCocoon::tryRelease() {
    if (MR::isHeldJump(mPadChannel)) {
        return false;
    }

    endCommandStream();
    MR::endMultiActorCamera(this, mCameraInfo, "狙い中", true);

    if (mPosition.distance(mNeutralPos) < ::sAimDistanceMin) {
        if (isKinopioAttached()) {
            MR::endDemo(this, "キノピオ狙い中");
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioWait));
        } else {
            MR::startMultiActorCameraTargetOther(this, mCameraInfo, "ウェイト", CameraTargetArg(mCameraTargetMtx));
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindWait));
        }
        return true;
    }

    MR::emitEffect(mCocoonModel, "Stick");

    if (mIsKinopioCameraFocused) {
        MR::onSwitchA(this);
        mIsKinopioCameraFocused = false;
    }

    mVelocity.set(mNeutralPos);
    mVelocity.sub(mPosition);
    MR::normalize(&mVelocity);

    // turn mario so he flies feet-first
    Vec3f reaxisUp = mVelocity * -1.0f;
    Vec3f reaxisFront = mUp;
    Vec3f reaxisSide = reaxisUp.cross(reaxisFront);
    if (!MR::isNearZero(reaxisSide)) {
        MR::normalize(&reaxisSide);
        reaxisFront.cross(reaxisSide, reaxisUp);
        MR::normalize(&reaxisFront);
        mBaseMtx.setXYZDir(reaxisSide, reaxisUp, reaxisFront);
    }

    mVelocity.mult(::sBindAttackSpeed);
    MR::emitEffect(mRider, "SpaceCocoonBlur");

    MR::startMultiActorCameraNoTarget(this, mCameraInfo, "攻撃中");

    if (isKinopioAttached()) {
        setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAttack));
    } else {
        setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindAttack));
    }

    return true;
}

bool SpaceCocoon::tryAttackMap() {
    if (!MR::isBinded(this)) {
        return false;
    }

    if (MR::isLessStep(this, ::sBindAttackInvalidTime)) {
        return false;
    }

    bool attackSuccess = true;
    for (s32 plane = 0; plane < MR::getBindedPlaneNum(this); plane++) {
        HitSensor* planeSensor = MR::getBindedPlaneSensor(this, plane);
        if (!MR::sendMsgEnemyAttack(planeSensor, getSensor("Attack"))) {
            if (!planeSensor->receiveMessage(ACTMES_IS_BROKEN, getSensor("Attack"))) {
                attackSuccess = false;
            }
        }
    }

    if (attackSuccess) {
        if (isKinopioAttached()) {
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAttackSuccess));
        } else {
            setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvBindAttackSuccess));
        }
        return false;
    }

    return true;
}

bool SpaceCocoon::isKinopioAttached() const {
    return isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioWait)) || isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvWaitKinopioAimDemo)) ||
           isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAim)) || isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAttack)) ||
           isNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvKinopioAttackSuccess));
}

void SpaceCocoon::endBind(const Vec3f& rJumpVec, bool attackSuccess) {
    MR::endMultiActorCamera(this, mCameraInfo, "ウェイト", true);
    MR::endMultiActorCamera(this, mCameraInfo, "狙い中", true);
    MR::endMultiActorCamera(this, mCameraInfo, "攻撃中", true);
    MR::deleteEffect(mRider, "SpaceCocoonBlur");

    if (!attackSuccess) {
        if (isKinopioAttached()) {
            MR::validateClipping(mRider);
            mRider->mVelocity.set(mVelocity);
            mRider->receiveMessage(ACTMES_NPC_EVENT_END, MR::getMessageSensor(), MR::getMessageSensor());
            MR::endDemo(this, "キノピオ狙い中");
        } else {
            MR::endBindAndPlayerJump(this, rJumpVec, 0);
        }
    }

    endCommandStream();
    mVelocity.zero();
    mPosition.set(mNeutralPos);
    setNerve(GET_NERVE(SpaceCocoon, SpaceCocoonNrvFreeInvalid));
}

void SpaceCocoon::endCommandStream() {
    if (mPadChannel >= 0 && MR::isStarPointerCommandStream(this, mPadChannel)) {
        MR::tryEndStarPointerCommandStream(this, mPadChannel);
        mPadChannel = -1;
    }
}

void SpaceCocoon::draw() const {
    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    f32 texU2 = ::sTexStartU;
    f32 texU1 = texU2 + ::sTexDiffU;

    drawPlane(site, ::sDrawScaleX, -::sDrawScaleZ0, 0.0f, -::sDrawScaleZ1, ::sColor, ::sColor, texU1, texU2);
    texU2 = texU1;
    texU1 += ::sTexDiffU;
    drawPlane(site, ::sDrawScaleX, ::sDrawScaleZ0, ::sDrawScaleX, -::sDrawScaleZ0, ::sColor, ::sColor, texU1, texU2);
    texU2 = texU1;
    texU1 += ::sTexDiffU;
    drawPlane(site, 0.0f, ::sDrawScaleZ1, ::sDrawScaleX, ::sDrawScaleZ0, ::sColor, ::sColor, texU1, texU2);
    texU2 = texU1;
    texU1 += ::sTexDiffU;
    drawPlane(site, -::sDrawScaleX, ::sDrawScaleZ0, 0.0f, ::sDrawScaleZ1, ::sColor, ::sColor, texU1, texU2);
    texU2 = texU1;
    texU1 += ::sTexDiffU;
    drawPlane(site, -::sDrawScaleX, -::sDrawScaleZ0, -::sDrawScaleX, ::sDrawScaleZ0, ::sColor, ::sColor, texU1, texU2);
    texU2 = texU1;
    texU1 += ::sTexDiffU;
    drawPlane(site, 0.0f, -::sDrawScaleZ1, -::sDrawScaleX, -::sDrawScaleZ0, ::sColor, ::sColor, texU1, texU2);
}

namespace {
    void drawPoints(VertexPCT*& rVtx, const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rFront, f32 thickness, f32 x1, f32 y1, f32 x2, f32 y2, Color8 color1,
                    Color8 color2, f32 texX1, f32 texX2, f32 texY) {
        Vec3f side = rSide;
        Vec3f front = rFront;
        side.scale(thickness);
        front.scale(thickness);

        *rVtx++ = {rPos.x + side.x * x1 + front.x * y1, rPos.y + side.y * x1 + front.y * y1, rPos.z + side.z * x1 + front.z * y1, Rgba8(color1), texX1, texY};

        *rVtx++ = {rPos.x + side.x * x2 + front.x * y2, rPos.y + side.y * x2 + front.y * y2, rPos.z + side.z * x2 + front.z * y2, Rgba8(color2), texX2, texY};
    }
};  // namespace

void SpaceCocoon::drawPlane(TypedDraw& rDraw, f32 x1, f32 y1, f32 x2, f32 y2, Color8 color1, Color8 color2, f32 texX1, f32 texX2) const {
    s32 numPoints = mNumPoints + 2;
    f32 delta = 1.0f / numPoints;

    VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, numPoints * 2);
    {
        ::drawPoints(vtx, mCocoonPos, mSide, mFront, ::sWidthBottom, x1, y1, x2, y2, color1, color2, texX1, texX2, 0.0f);

        for (s32 idx = 0; idx < mNumPoints; idx++) {
            f32 thickness = mPlantPoints[idx]->mThickness;

            ::drawPoints(vtx, mPlantPoints[idx]->mPosition, mPlantPoints[idx]->mSide, mPlantPoints[idx]->mFront, thickness, x1, y1, x2, y2, color1, color2,
                         texX1, texX2, (idx + 1) * delta);
        }

        ::drawPoints(vtx, mBasePos, mSide, mFront, ::sWidthTop, x1, y1, x2, y2, color1, color2, texX1, texX2, 1.0f);
    }
}

SiteMaterial SpaceCocoon::loadMaterial() const {
    // runtime/src/render/materials/tex_modulate.hlsl: the texture times the vertex colour, opaque; drawn where
    // the alpha is above 0, depth tested and written, back faces culled.
    SiteMaterial material;
    material.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_LEQUAL, true, NEBULA_CULL_BACK);
    material.constants[0] = 1.0f;
    material.constants[1] = 1.0f;
    material.setTexture(0, mTexture);
    return material;
}
