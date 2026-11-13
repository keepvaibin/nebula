#include "Game/Animation/XanimeCore.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/CollectCounter.hpp"
#include "Game/MapObj/IceStep.hpp"
#include "Game/Player/FireMarioBall.hpp"
#include "Game/Player/JetTurtleShadow.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioNullBck.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/TornadoMario.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/MtxUtil.hpp"

void MarioActor::init2D() {
    if (!mExtra) {
        MR::getGameSceneLayoutHolder()->initLifeCount(mMaxHealth);
    }

    mTrampleCounter = new CollectCounter("マリオ連続踏み");

    mTrampleCounter->initWithoutIter();
}

void MarioActor::initParts() {
    mNullAnimation = new MarioNullBck("NULLアニメ");
    mNullAnimation->initWithoutIter();

    mSearchLight = 0;
    mSearchLightThrowPos = nullptr;

    initSearchLight();
    initThrowing();

    mFreezeModel = new MarioParts(this, "氷結モデル", "MarioFreezeIce", true, nullptr, nullptr);
    mFreezeModel->initWithoutIter();
    mFreezeModel->makeActorDead();
    mFreezeModel->initFixedPosition(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);

    mTurtleShadow = new JetTurtleShadow("カメシャドウモデル");
    mTurtleShadow->initWithoutIter();

    mSpinTico = new MarioParts(this, "スピンチコ", "SpinTico", false, getBaseMtx(), nullptr);
    mSpinTico->initWithoutIter();
    mSpinTico->kill();
}

void MarioActor::updateBeeWingAnimation() {
    if (mPlayerMode != 4) {
        getJointCtrl("HandR")->setLocalScale(1.0f);
        getJointCtrl("HandL")->setLocalScale(1.0f);

        return;
    }

    getJointCtrl("HandR")->setLocalScale(0.9f);
    getJointCtrl("HandL")->setLocalScale(0.9f);

    if (mMario->checkLvlA() && mMario->mAirWalkTimer && getMovementStates().jumping) {
        if (mBeeWingAnim != 1) {
            MR::startBck(mBeeWing, "Fly");
            MR::startBva(mBeeWing, "Fly");
            MR::startBtk(mBeeWing, "Fly");
        }

        mBeeWingAnim = 1;
        return;
    }

    s32 val;
    if (getMovementStates().onGround || mMario->isStatusActive(MarioStatus_Stick)) {
        val = 0;
    } else {
        val = mMario->mAirWalkTimer != 0 ? 2 : 3;
    }

    if (mBeeWingAnim == val) {
        return;
    }

    switch (val) {
    case 0:
        MR::startBck(mBeeWing, "Wait");
        MR::startBva(mBeeWing, "Wait");
        break;

    case 2:
        MR::startBck(mBeeWing, "FlyWait");
        MR::startBva(mBeeWing, "FlyWait");
        break;

    case 3:
        MR::startBck(mBeeWing, "FlyFall");
        MR::startBva(mBeeWing, "FlyFall");
        break;
    }

    MR::stopBtk(mBeeWing);
    mBeeWingAnim = val;
}

void MarioActor::updateTornado() {
    if (mTornadoMario == nullptr) {
        return;
    }

    if (mMario->getMovementStates().tornado && mMario->mTornadoTimer > 1) {
        mTornadoMario->show();
    } else if ((!isAnimationRun("空中ひねり") || mMario->mJumpKind != 8) && !mMario->isStatusActive(MarioStatus_Magic)) {
        if (mMario->getDrawStates().endTornado || mTornadoEnd != 0) {
            mTornadoMario->hideForce();
        } else {
            mTornadoMario->hide();
        }

        mTornadoEnd = 0;
    }

    mTornadoMario->setTrHeight(mPosition, mMario->mFrontVec, mMario->mTornadoTiltUp, mGravityVec);
}

void MarioActor::updateTakingPosition() {
    if (mTaking) {
        const HitSensor* sensor = mTakenSensor;
        if (!sensor)
            sensor = getCarrySensor();
        if (!sensor) {
            mTaking = false;
        } else {
            switch (sensor->mType) {
            case ATYPE_BOMBHEI:
                if (mMario->isAnimationTerminate("カブ抜き")) {
                    stopAnimation(nullptr);
                    mTaking = false;
                    mMario->changeAnimationUpper("カブウエイト");
                }
                break;
            case ATYPE_JET_TURTLE:
            case ATYPE_JET_TURTLE_SLOW:
                if (mMario->isAnimationTerminate(nullptr))
                    mTaking = false;
                break;
            }
        }
    }
    if (mNullAnimationKind < 0) {
        Mtx base;
        Vec3f position;
        Vec3f rotation;
        bool updateAnimation;
        if (mNullAnimationKind == -3) {
            mTakingFixedPos->calc();
            mTakingFixedPos->copyTrans(&position);
            mTakingFixedPos->copyRotate(&rotation);
            MarioAnimator* animator = mMarioAnim;
            s32 stopped = !isAnimationRun(nullptr);
            s32 terminated = mMario->isAnimationTerminate(nullptr);
            terminated |= stopped;
            s32 landing = animator->isLandingAnimationRun();
            landing |= terminated;
            updateAnimation = landing;
        } else {
            f32 frame = 0.0f;
            if (mNullAnimationKind == -1)
                frame = mMarioAnim->getUpperFrame();
            if (mNullAnimationKind == -2)
                frame = mMarioAnim->getFrame();
            NebulaMtxConcat(getBaseMtx(), mBaseCorrectionMtx.toMtxPtr(), base);
            Model* model = MR::getModel(mNullAnimation);
            NebulaMtxCopy(base, model->getBaseTRMtx());
            if (mNullAnimation->getFramePos(frame, &position, &rotation)) {
                clearNullAnimation(-3);
                if (mTakenSensor)
                    MR::sendArbitraryMsg(ACTMES_TAKE_TOUCH, mTakenSensor, getSensor("body"));
                else if (mTakeSensors[0])
                    MR::sendArbitraryMsg(ACTMES_TAKE_TOUCH, mTakeSensors[0], getSensor("body"));
                else {
                    mTaking = false;
                    clearNullAnimation(0);
                }
            }
            updateAnimation = false;
        }
        if (updateAnimation) {
            if (mTakenSensor)
                mMarioAnim->updateTakingAnimation(mTakenSensor);
            else if (mTakeCount)
                mMarioAnim->updateTakingAnimation(mTakeSensors[0]);
        }
        if (mTakenSensor) {
            mTakenSensor->mHost->mPosition = position;
            mTakenSensor->mHost->mRotation = rotation;
            return;
        }
        if (getCarrySensor()) {
            getCarrySensor()->mHost->mPosition = position;
            getCarrySensor()->mHost->mRotation = rotation;
        }
    } else if (mTakeCount) {
        if (mTakeSensors[0]->isType(ATYPE_COINTHROW) || mTakeSensors[0]->isType(ATYPE_JET_TURTLE) || mTakeSensors[0]->isType(ATYPE_JET_TURTLE_SLOW) ||
            mTakeSensors[0]->isType(ATYPE_BOMBHEI)) {
            Vec3f position;
            mTakingFixedPos->calc();
            mTakingFixedPos->copyTrans(&position);
            Vec3f& carryPosition = mTakeSensors[0]->mHost->mPosition;
            const Vec3f& velocity = mVelocity;
            carryPosition = position + velocity;
            mTakingFixedPos->copyRotate(&mTakeSensors[0]->mHost->mRotation);
            return;
        }
        Vec3f position;
        mNullAnimation->getLastPos(&position);
        NebulaMtxMultVec(mBaseCorrectionMtx.toMtxPtr(), &position, &position);
        NebulaMtxMultVec(getBaseMtx(), &position, &position);
        f32 offset = 10.0f * (mMario->mWalkSpeed * mMario->mWalkSpeed);
        offset *= Angle::sinRadian(mTakingWobble);
        mTakingWobble += MR::getRandom(0.1f, 1.0f);
        position.y += offset;
        mTakeSensors[0]->mHost->mPosition = position;
        Vec3f& carryRotation = mTakeSensors[0]->mHost->mRotation;
        const Vec3f& rotation = mRotation;
        carryRotation = mTakeRotations[0] + rotation;
    }
}

const HitSensor* MarioActor::getCarrySensor() const {
    if (mTakeCount == 0) {
        return nullptr;
    }

    return mTakeSensors[0];
}

void MarioActor::changeSpecialModeAnimation(const char* pAnimName) {
    switch (mPlayerMode) {
    case 6:
        if (MR::isEqualString(pAnimName, "特殊ウエイト1A")) {
            changeTeresaAnimation("SleepStart", -1);
            return;
        }

        if (!MR::isEqualString(pAnimName, "特殊ウエイト1B")) {
            return;
        }

        changeTeresaAnimation("Sleep", 16);
        MR::emitEffect(mTeresaModel, "Sleep");
    }
}

void MarioActor::updateSpecialModeAnimation() {
    if (!mMario->mMovementStates.squat && mMario->getCurrentStatus() == MarioStatus_None) {
        if (mMario->mMovementStates.onGround && mMario->mFloorCode == 0x20 && mMarioAnim->isAnimationStop()) {
            mMarioAnim->mXanimePlayer->changeTrackAnimation(0, "泥低速歩行");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(1, "泥高速歩行");
            mMudWalkTimer = 2;
        }
    } else {
        mMudWalkTimer = 0;
    }
    switch (mPlayerMode) {
    case PlayerMode_Teresa:
        updateTeresaAnimation();
        break;
    case PlayerMode_Bee:
        if (mBeeWallWalk && !isJumping() && mMarioAnim->isAnimationStop()) {
            mMarioAnim->mXanimePlayer->changeTrackAnimation(0, "ハチ匍匐前進");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(1, "ハチ匍匐前進");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(2, "ハチ匍匐前進");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(3, "ハチ匍匐ウエイト");
        }
        break;
    default:
        mMario->mTeresaWallThroughTimer = 0;
        break;
    }
    if (mMudWalkTimer) {
        mMudWalkTimer--;
        if (!mMudWalkTimer && mMario->mMovementStates.onGround && mMarioAnim->isAnimationStop()) {
            mMarioAnim->mXanimePlayer->changeTrackAnimation(0, "鈍行");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(1, "歩行");
        }
    }
}

void MarioActor::initFireBall() {
    for (u32 idx = 0; idx < ARRAY_SIZE(mFireBalls); idx++) {
        mFireBalls[idx] = new FireMarioBall("マリオ炎球");
        mFireBalls[idx]->initWithoutIter();
    }
}

void MarioActor::shootFireBall() {
    if (isAnimationRun("ファイア投げ"))
        return;
    if (isAnimationRun("ファイアスピン空中"))
        return;
    if (isAnimationRun("ファイアスピン"))
        return;
    if (getMovementStates().frontWall) {
        sendMsgToSensor(mMario->getWallPolygon()->mSensor, ACTMES_FIREBALL_ATTACK);
        changeAnimation("ファイアスピン");
        return;
    }
    u32 index;
    for (index = 0; index < ARRAY_SIZE(mFireBalls); index++) {
        if (MR::isDead(mFireBalls[index]))
            break;
    }
    if (index == ARRAY_SIZE(mFireBalls))
        return;
    FireMarioBall* ball = mFireBalls[index];
    Vec3f direction;
    direction = mMario->mFrontVec;
    direction += mMario->mHeadVec;
    MR::normalize(&direction);
    Vec3f position;
    getRealPos("HandR", &position);
    position += mMario->mFrontVec * 30.0f;
    ball->appearAndThrow(position, direction);
    playSound("声投げ", -1);
    if (!isJumping()) {
        mMario->mMoveLockTimer = 45;
        changeAnimation("ファイアスピン");
        return;
    }
    if (mMario->mFireShotCount >= 3) {
        changeAnimation("ファイア投げ");
        return;
    }
    if (mMario->mFireShotCount == 0)
        changeAnimation("ファイアスピン空中");
    else
        changeAnimation("ファイア投げ");
    jumpHop();
    mMario->mFireShotCount++;
    f32 gravitySpeed = mMario->cutGravityElementFromJumpVec(true);
    Mario* mario = mMario;
    mario->mJumpVec.x *= 0.5f;
    mario->mJumpVec.y *= 0.5f;
    mario->mJumpVec.z *= 0.5f;
    mMario->mJumpVec += mGravityVec * gravitySpeed;
}

void MarioActor::showFreezeModel() {
    mFreezeModel->appear();
    MR::onCalcAnim(mFreezeModel);
    MR::startBva(mFreezeModel, "Nomal");
}

void MarioActor::hideFreezeModel() {
    MR::startBck(mFreezeModel, "Break");
    MR::startBva(mFreezeModel, "Break");

    mMario->startFreezeEnd();
}

void MarioActor::updateFairyStar() {
    if (mHidden)
        return;
    if (!mSwingPermission)
        return;
    if (!isEnableNerveChange())
        return;
    bool enabled = true;
    if (selectAction("スピン回復エフェクト") != 1)
        enabled = false;
    if (mSpinStarTimer && enabled) {
        if (MR::isDead(mSpinTico)) {
            mSpinTico->appear();
            mSpinTico->mRotation.set(0.0f, 0.0f, 0.0f);
            MR::startBck(mSpinTico, "SpinTimer");
            playSound("スピン許可", -1);
        }
        Vec3f position;
        MR::copyJointPos(mSpinTico, "Center", &position);
        Color8 color(255, 225, 225, 255);
        f32 power = 0.001f;
        MR::requestPointLight(mSpinTico, position, color, power, 0);
        return;
    }
    if (!MR::isDead(mSpinTico))
        mSpinTico->kill();
}

void MarioActor::update2D() {
    // The life meter is the scene's player's.
    if (mExtra) {
        return;
    }
    GameSceneLayoutHolder* layoutHolder = MR::getGameSceneLayoutHolder();
    layoutHolder->setLifeCount(mHealth);

    if (mMario->getPlayerMode() == 4) {
        layoutHolder->setBeePowerRatio((f32)mMario->mAirWalkTimer / mConst->getTable()->mAirWalkTime);
    }

    if (mMario->isSwimming()) {
        layoutHolder->setOxygenRatio((f32)mMario->mSwim->mOxygen / mConst->getTable()->mOxygenMax);
    }

    if (mTrampleCombo == 0) {
        mTrampleCounter->kill();
    }
}

void MarioActor::updateThrowVector() {
    HitSensor* sensor = mTakeCandidate;
    if (sensor && !MR::isExistInAttributeGroupSearchTurtle(sensor->mHost))
        sensor = nullptr;
    if (sensor && mTakeCount) {
        if (!getDrawStates().homingLockOn) {
            if (mThrowTarget && mTakeCandidate != mThrowTarget)
                mThrowTarget = nullptr;
        }
        if (!getDrawStates().homingLockOn) {
            if (!mThrowVecTimer)
                mThrowProbePos = mCenterPos;
            mThrowVecTimer++;
            Vec3f direction = mTakeCandidate->mPosition - mThrowProbePos;
            if (direction.length() <= 40.0f + mTakeCandidate->getRadius()) {
                mThrowProbePos = mTakeCandidate->mPosition;
                mThrowTarget = mTakeCandidate;
                if (MR::isSensorEnemy(mTakeCandidate)) {
                    mThrowVecTimer = 60;
                    return;
                }
                mThrowVecTimer = 2;
                return;
            }
            MR::normalize(&direction);
            mThrowProbePos += direction * 40.0f;
            if (MR::checkStrikePointToMap(mThrowProbePos, nullptr)) {
                mThrowVecTimer = 0;
                mThrowTarget = nullptr;
            }
        }
    } else if (!getDrawStates().homingLockOn) {
        if (mThrowVecTimer)
            mThrowVecTimer--;
        if (!mThrowVecTimer)
            mThrowTarget = nullptr;
    }
}

void MarioActor::createIceFloor(const Vec3f& rVec) {
    Pos3f mtx;
    mtx.identity();

    Vec3f upVec;
    getUpVec(&upVec);

    MR::makeMtxUpFront(&mtx, -getAirGravityVec(), mMario->mFrontVec);

    Vec3f vec;
    mtx.getEuler(vec);

    vec *= 180.0f / PI;
    createIceFloor(rVec, vec);
}

void MarioActor::createIceFloor(const Vec3f& rPos, const Vec3f& rRotation) {
    mIceSteps[mIceStepIndex]->setOn(mIceStepIndex, rPos, rRotation);

    mIceStepIndex = (mIceStepIndex + 1) % cIceStepNum;

    if (!MR::isDead(mIceSteps[mIceStepIndex])) {
        mIceSteps[mIceStepIndex]->destroy();
    }
}

void MarioActor::createIceWall(const Vec3f& rPos, const Vec3f& rNormal) {
    Pos3f mtx;
    mtx.identity();
    Vec3f upVec;
    getUpVec(&upVec);

    MR::makeMtxFrontUp(&mtx, getGravityVector(), rNormal);

    Vec3f vec;
    mtx.getEuler(vec);
    vec *= 180.0f / PI;
    mIceSteps[mIceStepIndex]->setOn(mIceStepIndex, rPos, vec);

    mIceStepIndex = (mIceStepIndex + 1) % cIceStepNum;

    if (!MR::isDead(mIceSteps[mIceStepIndex])) {
        mIceSteps[mIceStepIndex]->destroy();
    }
}

void MarioActor::updateBaseMtxTeresa(MtxPtr mtx) {
    Vec3f horizontal;
    f32 tilt = MR::clamp(MR::vecKillElement(mVelocity, getGravityVec(), &horizontal) / 10.0f, -1.0f, 1.0f);
    if (!MR::isNearZero(mMario->getWorldPadDir())) {
        const Vec3f& front = mMario->mFrontVec;
        if (MR::diffAngleAbsHorizontal(mMario->getWorldPadDir(), front, getGravityVec()) > 0.3926991f)
            tilt = 0.0f;
    }
    mTeresaTilt = 0.98f * mTeresaTilt + 0.02f * tilt;
    f32 angle = mConst->getTable()->mTeresaAngleDown;
    if (mTeresaTilt > 0.0f)
        angle = mConst->getTable()->mTeresaAngleUp;
    NebulaMtxConcat(mtx, MR::tmpMtxRotXRad(mTeresaTilt * angle), mtx);
}

bool MarioActor::finalizeFreezeModel() {
    if (MR::isBckStopped(mFreezeModel)) {
        mFreezeModel->kill();
        MR::offCalcAnim(mFreezeModel);

        return false;
    }

    return true;
}

void MarioActor::offTakingFlag() {
    mTaking = false;
}
