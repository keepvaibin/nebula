#include "Game/Enemy/IceMerameraKing.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Effect/SpinPullParticleCallBack.hpp"
#include "Game/Enemy/Meramera.hpp"
#include "Game/Enemy/MoguStone.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>


// TODO: the symbol order for header funcs is out of order between JointControlDelegator funcs
//       and Array funcs. This needs to be fixed in order to link.

namespace NrvIceMerameraKing {
    NEW_NERVE(HostTypeNrvSearch, IceMerameraKing, Search);
    NEW_NERVE_ONEND(HostTypeNrvBeginDemo, IceMerameraKing, Throw, Throw);
    NEW_NERVE_ONEND(HostTypeNrvThrow, IceMerameraKing, Throw, Throw);
    NEW_NERVE_ONEND(HostTypeNrvThrow2nd, IceMerameraKing, Throw, Throw);
    NEW_NERVE(HostTypeNrvExtinguish, IceMerameraKing, Extinguish);
    NEW_NERVE_ONEND(HostTypeNrvEscape, IceMerameraKing, Escape, Escape);
    NEW_NERVE(HostTypeNrvEscapeJump, IceMerameraKing, EscapeJump);
    NEW_NERVE(HostTypeNrvDamage, IceMerameraKing, Damage);
    NEW_NERVE(HostTypeNrvPreRecover, IceMerameraKing, PreRecover);
    NEW_NERVE(HostTypeNrvRecover, IceMerameraKing, Recover);
    NEW_NERVE(HostTypeNrvPreAttack, IceMerameraKing, PreAttack);
    NEW_NERVE(HostTypeNrvAttack, IceMerameraKing, Attack);
    NEW_NERVE(HostTypeNrvAttackAfter, IceMerameraKing, AttackAfter);
    NEW_NERVE(HostTypeNrvAngryDemo1st, IceMerameraKing, AngryDemo);
    NEW_NERVE(HostTypeNrvAngryDemo2nd, IceMerameraKing, AngryDemo);
    NEW_NERVE(HostTypeNrvDeathDemoWait, IceMerameraKing, DeathDemoWait);
    NEW_NERVE(HostTypeNrvDeathDemo, IceMerameraKing, DeathDemo);
};  // namespace NrvIceMerameraKing

namespace {
    static const MR::ActorMoveParam hOnAirParam = {0.0f, 3.0f, 0.96f, 0.0f};
    static const MR::ActorMoveParam hOnGroundParam = {0.0f, 1.0f, 0.8f, 0.0f};
    static const MR::ActorMoveParam hFlyParam = {0.0f, 0.5f, 0.9f, 1.0f};
    static const MR::ActorMoveParam hAngryDemoParam = {0.0f, 0.5f, 0.9f, 1.5f};
    static const MR::ActorMoveParam hEscapeOnGroundParam = {-0.7f, 1.0f, 0.96f, 180.0f};
    static const MR::ActorMoveParam hEscapeOnAirParam = {0.0f, 3.0f, 0.96f, 0.0f};
    static const MR::ActorMoveParam hPreRecoverJumpParam = {0.0f, 1.0f, 0.96f, 0.0f};
    static const MR::ActorMoveParam hDamageJumpParam = {0.0f, 1.9f, 0.98f, 0.0f};
    static const MR::ActorMoveParam hAttackParam = {0.0f, 6.0f, 0.98f, 0.0f};
    static const MR::ActorMoveParam hExtinguishOnAirParam = {0.0f, 0.125f, 0.96f, 0.0f};
    static const MR::ActorMoveParam hExtinguishFallOnAirParam = {0.0f, 4.0f, 0.96f, 0.0f};
    static const Vec3f hBinderOffset(-100.586f, 222.65601f, -91.796898f);
    static const Vec3f hEscapeBinderOffset(-39.0625f, 91.796898f, -47.851601f);
};  // namespace

IceMerameraKing::IceMerameraKing(const char* pName)
    : LiveActor(pName), mFixedPos(nullptr), mThrowingIce(nullptr), mMeramera(nullptr), mIce(), mShockWave(nullptr), mBreakModel(nullptr), mFront(0, 0, 1), mUp(0, 1, 0),
      mInitPos(0, 0, 0), mInitRotation(0, 0, 0), mThrowNum(0), mWeaponIndex(0), mHitPoint(3), mWeaponNum(0), mModelArray(nullptr), mCameraTarget(nullptr), mJointController(nullptr),
      mRollQuat(0, 0, 0, 1), mBinderOffset(0.0f, 0.0f, 0.0f), mExtinguishHeight(0.0f), mIsPinchBgmOn(false), mIsPinchBgmAllowed(false) {
}

void IceMerameraKing::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        mInitPos.set(mPosition);
        mInitRotation.set(mRotation);
        MR::useStageSwitchWriteDead(this, rIter);
        MR::useStageSwitchReadA(this, rIter);
    }

    mScale.x = 1.5f;
    mScale.y = 1.5f;
    mScale.z = 1.5f;
    initModelManagerWithAnm("IceMerameraKing", nullptr, false);
    MR::calcUpVec(&mUp, this);
    MR::calcFrontVec(&mFront, this);
    MR::connectToSceneEnemy(this);
    f32 v5 = (mScale.y / 1.8f);
    initBinder((130.0f * v5), (130.0f * v5), 0);
    MR::setBinderOffsetVec(this, &mBinderOffset, true);
    initHitSensor(1);
    MR::addHitSensorAtJointEnemy(this, "body", "JointRoot", 32, (130.0f * v5), Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
    MR::calcGravity(this);
    MR::initShadowVolumeSphere(this, (130.0f * mScale.y));
    MR::setShadowDropLength(this, nullptr, 1500.0f);
    MR::declarePowerStar(this);
    MR::declareStarPiece(this, 24);
    mFixedPos = new FixedPosition(this, "Top", Vec3f(0, 0, 0), Vec3f(0.0f, 0.0f, 0.0f));
    MR::tryRegisterDemoCast(this, rIter);
    mIce.init(6);
    for (s32 i = 0; i < 6; i++) {
        ThrowingIce* newIce = new ThrowingIce("投擲用の氷");
        newIce->initWithoutIter();
        mIce.push_back(newIce);
        MR::tryRegisterDemoCast(newIce, rIter);
    }

    MR::initLightCtrl(this);
    mShockWave = new IceMerameraKingShockWave();
    mShockWave->initWithoutIter();
    mShockWave->mRotation.set(mRotation);
    mShockWave->makeActorDead();
    mBreakModel = new ModelObj("壊れモデル", "IceMerameraKingBreak", nullptr, -2, -2, -2, false);
    mBreakModel->initWithoutIter();
    mBreakModel->mScale.set(mScale);
    mBreakModel->makeActorDead();
    s32 childNum = MR::getChildObjNum(rIter);
    mWeaponNum = childNum;
    mModelArray = new Meramera*[childNum];

    for (s32 i = 0; i < mWeaponNum; i++) {
        mModelArray[i] = new Meramera("メラメラ");
        MR::initChildObj(mModelArray[i], rIter, i);
        mModelArray[i]->makeActorDead();
        MR::tryRegisterDemoCast(mModelArray[i], rIter);
    }

    MR::tryRegisterDemoCast(mBreakModel, rIter);
    MR::registerDemoActionNerve(this, GET_NERVE(IceMerameraKing, HostTypeNrvBeginDemo), nullptr);
    mSpinParticle = new MerameraParticleCallBack();
    mJointController = MR::createJointDelegatorWithNullChildFunc(this, &IceMerameraKing::calcJoint, "JointRoot");
    makeActorAppeared();
    MR::emitEffect(this, "BodyIce");
    mCameraTarget = new CameraTargetDemoActor(getBaseMtx(), "アクター注目");
    mCameraTarget->initWithoutIter();
    mCameraTarget->mPosition.set(mInitPos);
    mCameraTarget->mRotation.set(mInitRotation);
    mCameraTarget->setName("メラキン注目ターゲット");
    MR::tryRegisterDemoCast(mCameraTarget, rIter);
}

void IceMerameraKing::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
    CollisionParts* exCollisionParts = mBinder->mExCollisionParts;

    for (s32 i = 0; i < mWeaponNum; i++) {
        Binder* binder = mModelArray[i]->mBinder;
        binder->mExCollisionParts = exCollisionParts;

        if (exCollisionParts == nullptr) {
            binder->mIsExCollisionValid = false;
        } else {
            binder->mIsExCollisionValid = true;
        }
    }
}

void IceMerameraKing::kill() {
    LiveActor::kill();

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
    MR::startAfterBossBGM();
    s32 temp = mIce.mCount;

    for (s32 i = 0; i < temp; i++) {
        if (!MR::isDead(mIce[i])) {
            mIce[i]->kill();
        }
    }
}

void IceMerameraKing::control() {
    Vec3f v7;
    MR::copyJointScale(this, "Center", &v7);
    f32 v3 = 130.0f * v7.x;
    MR::setBinderRadius(this, v3);
    mBinderOffset.y = v3;
    mBinderOffset.x = 0.0f;
    mBinderOffset.z = 0.0f;
    getSensor("body")->mRadius = v3;
    MR::setShadowVolumeSphereRadius(this, nullptr, v3);

    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            MR::invalidateClipping(this);
        } else {
            MR::validateClipping(this);
        }
    }

    if (!isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape)) && !isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump))) {
        Quat4f quad(0, 0, 0, 1);
        mRollQuat.slerp(quad, 0.25f);
    }

    if (!isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape)) && !isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump))) {
        MR::startLevelSound(this, "SE_BM_LV_ICEMERAKING_WAIT");
    }

    if (MR::isDemoActive("メラキンオープニング")) {
        MR::startSystemLevelSE("SE_DM_LV_MUTE_BGM");
        if (MR::isDemoPartLastStep("カメラ寄る")) {
            MR::playLevelMarioPinchBGM(mIsPinchBgmOn);
            mIsPinchBgmOn = true;
            mIsPinchBgmAllowed = true;
        }
    } else if (!isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemoWait)) && !isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemo)) &&
               MR::isNearPlayer(this, 5000.0f) && mIsPinchBgmAllowed) {
        MR::playLevelMarioPinchBGM(mIsPinchBgmOn);
        mIsPinchBgmOn = true;
    }
}

void IceMerameraKing::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }
    addVelocityToInitPos();
    MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    MR::addVelocityKeepHeightUseShadow(this, 400.0f, 1.5f, 300.0f, nullptr);
    f32 shadow = MR::calcDistanceToPlayer(this);

    if (!(4500.0f < shadow)) {
        if (shadow < 1000.0f) {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreAttack));
        } else {
            if (isEnableThrow()) {
                if (!(mHitPoint > 2)) {
                    if (MR::isGreaterStep(this, 100) && isDeadAllIce()) {
                        mThrowNum = 0;
                        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow2nd));
                    }
                } else if (MR::isGreaterStep(this, 200) && isDeadAllIce()) {
                    mThrowNum = 0;
                    setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow));
                }
            }
        }
    }
}

void IceMerameraKing::exeThrow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "AttackThrow");
        if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvBeginDemo))) {
            MR::calcFrontVec(&mFront, mCameraTarget);
        }
    }
    MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    MR::addVelocityKeepHeightUseShadow(this, 400.0f, 1.5f, 300.0f, nullptr);
    addVelocityToInitPos();

    if (MR::isStep(this, 22)) {
        mThrowingIce = getDeadWeaponAndAppear();
        mFixedPos->mMtx.getTrans(mThrowingIce->mPosition);
        if (!mThrowingIce) {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
            return;
        }

        mThrowNum += 1;
        Vec3f& icePos = mThrowingIce->mPosition;
        mThrowingIce->emitIce(icePos, *MR::getPlayerCenterPos() + *MR::getPlayerVelocity() * 35.0f, -5.0f, mGravity);
        mThrowingIce = nullptr;
        MR::startSound(this, "SE_BM_ICEMERAKING_THROW");
    }

    if (MR::isActionEnd(this)) {
        if (MR::calcDistanceToPlayer(this) < 1000.0f) {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreAttack));
        } else {
            u32 v10 = 3;
            if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow2nd))) {
                v10 = 6;
            }

            if (v10 <= mThrowNum || !isEnableThrow()) {
                setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
            } else if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow2nd))) {
                setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow2nd));
            } else {
                setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow));
            }
        }
    }
}

void IceMerameraKing::endThrow() {
    tearDownThrow();
}

void IceMerameraKing::tearDownThrow() {
    if (mThrowingIce) {
        mThrowingIce->kill();
        mThrowingIce = nullptr;
    }

    if (mMeramera) {
        mMeramera->kill();
        mMeramera = nullptr;
    }
}

void IceMerameraKing::exeExtinguish() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "GoOut");
        MR::startSound(this, "SE_BM_ICEMERAKING_BLOW");
        MR::startSound(this, "SE_BM_ICEMERAKING_SMOKE");
        MR::deleteEffect(this, "BodyIce");
        MR::emitEffectWithParticleCallBack(this, "BodyIceOff", mSpinParticle);
        Vec3f v4 = mPosition - *MR::getPlayerCenterPos();
        MR::vecKillElement(v4, mGravity, &v4);
        MR::normalizeOrZero(&v4);
        MR::setVelocitySeparateHV(this, v4, 20.0f, 0.0f);
        mExtinguishHeight = 200.0f + MR::getShadowNearProjectionLength(this);
    }

    if (MR::isLessStep(this, 40)) {
        MR::addVelocityKeepHeightUseShadow(this, mExtinguishHeight, 1.3f, 700.0f, nullptr);
        MR::moveAndTurnToPlayer(this, &mFront, ::hExtinguishOnAirParam);
    } else if (MR::isOnGround(this) && MR::isActionEnd(this)) {
        MR::moveAndTurnToPlayer(this, &mFront, ::hOnGroundParam);
        MR::startSound(this, "SE_BM_ICEMERAKING_LAND");
        mEscapeStep = 0;
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape));
    } else {
        MR::moveAndTurnToPlayer(this, &mFront, ::hExtinguishFallOnAirParam);
    }
}

void IceMerameraKing::exeEscape() {
    if (MR::isFirstStep(this)) {
    }
    mEscapeStep++;

    if (mEscapeStep > 600 && MR::isOnGround(this)) {
        mEscapeStep = 0;
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreRecover));
    } else {
        MR::emitEffect(this, "Rolling");
        if (MR::isOnGround(this)) {
            f32 v11 = MR::getLinerValueFromMinMax(mVelocity.length(), 2.0f, 6.0f, 0.0f, 100.0f);
            MR::startLevelSound(this, "SE_BM_LV_ICEMERAKING_ROLL", v11, -1, 15);
        }

        if (MR::isOnGround(this)) {
            f32 radius = getSensor("body")->mRadius;
            MR::rotateQuatRollBall(&mRollQuat, mVelocity, *MR::getGroundNormal(this), (0.5f * radius));
            MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnGroundParam);
            if (MR::isBindedWallFront(this, -mFront, 0.25f)) {
                setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump));
            }
        } else {
            f32 sensor = getSensor("body")->mRadius;
            MR::rotateQuatRollBall(&mRollQuat, mVelocity, -mGravity, 0.5f * sensor);
            MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnAirParam);
        }
    }
}

void IceMerameraKing::endEscape() {
    MR::deleteEffect(this, "Rolling");
}

void IceMerameraKing::exeEscapeJump() {
    if (MR::isStep(this, 20)) {
        MR::addVelocitySeparateHV(this, mFront, 20.0f, 90.0f);
        MR::startSound(this, "SE_BM_ICEMERAKING_JUMP");
    }
    mEscapeStep++;

    if (MR::isOnGround(this)) {
        f32 radius = getSensor("body")->mRadius;
        MR::rotateQuatRollBall(&mRollQuat, mVelocity, *MR::getGroundNormal(this), (0.5f * radius));
        MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnGroundParam);

        if (MR::isGreaterStep(this, 20)) {
            MR::emitEffect(this, "Land");
            MR::startSound(this, "SE_BM_ICEMERAKING_LAND");
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape));
        }
    } else {
        f32 radius = getSensor("body")->mRadius;
        MR::rotateQuatRollBall(&mRollQuat, mVelocity, -mGravity, (0.5f * radius));
        MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnAirParam);
    }
}

void IceMerameraKing::exeDamage() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Damage");
        MR::emitEffect(this, "Damage");
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_BM_ICEMERAKING_DAMAGE");
        MR::setVelocityJump(this, 80.0f);
        MR::stopSceneForDefaultHit(4);
    }

    if (MR::isGreaterStep(this, 20)) {
        MR::addVelocityKeepHeightUseShadow(this, 900.0f, 3.0f, 300.0f, nullptr);
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, &mFront, ::hOnGroundParam);
    } else {
        MR::moveAndTurnToPlayer(this, &mFront, ::hDamageJumpParam);
    }

    if (MR::isGreaterStep(this, 60)) {
        if (mHitPoint == 1) {
            if (MR::tryStartDemoRegisteredMarioPuppetable(this, "怒りデモ")) {
                setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd));
            }
        } else if (MR::tryStartDemoRegisteredMarioPuppetable(this, "怒りデモ")) {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo1st));
        }
    }
}

void IceMerameraKing::exePreRecover() {
    if (MR::isFirstStep(this)) {
        MR::setVelocityJump(this, 70.0f);
        MR::startSound(this, "SE_BM_ICEMERAKING_JUMP");
    }
    addVelocityToInitPos();

    if (MR::isLessStep(this, 50)) {
        MR::moveAndTurnToPlayer(this, &mFront, ::hPreRecoverJumpParam);
    } else if (MR::isStep(this, 50)) {
        MR::startAction(this, "PreRecover");
        MR::setBinderRadius(this, 130.0f);
        HitSensor* sensor = getSensor("body");
        sensor->mRadius = 130.0f;
        MR::setShadowVolumeSphereRadius(this, nullptr, 130.0f);
        MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    } else if (MR::isLessStep(this, 120)) {
        MR::addVelocityKeepHeightUseShadow(this, 750.0f, 1.5f, 300.0f, nullptr);
        MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    } else {
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvRecover));
    }
}

void IceMerameraKing::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Recover");
        MR::emitEffect(this, "BodyIce");
        MR::startSound(this, "SE_BM_ICEMERAKING_PRE_RECOVER");
    }

    if (MR::isStep(this, 80)) {
        MR::startSound(this, "SE_BM_ICEMERAKING_RECOVER");
    }
    addVelocityToInitPos();
    MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    MR::addVelocityKeepHeightUseShadow(this, 750.0f, 1.5f, 300.0f, nullptr);

    if (MR::isGreaterStep(this, 160)) {
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
    }
}

void IceMerameraKing::exePreAttack() {
    if (MR::isFirstStep(this)) {
    }

    if (MR::isLessStep(this, 120)) {
        MR::addVelocityKeepHeightUseShadow(this, 800.0f, 1.5f, 300.0f, nullptr);
    } else if (MR::isDead(mShockWave)) {
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAttack));
        return;
    }
    addVelocityToInitPos();
    MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
}

void IceMerameraKing::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Attack");
        MR::startSound(this, "SE_BM_ICEMERAKING_PRE_ATTACK");
    }

    if (!MR::isActionEnd(this)) {
        MR::addVelocityKeepHeightUseShadow(this, 800.0f, 1.5f, 300.0f, nullptr);
        MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    } else {
        MR::moveAndTurnToPlayer(this, &mFront, ::hAttackParam);
    }

    if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAttackAfter));
    }
}

void IceMerameraKing::exeAttackAfter() {
    if (MR::isFirstStep(this)) {
        mShockWave->appear();
        mShockWave->mPosition.set(mPosition);
        mShockWave->mRotation.set(mInitRotation);
        MR::emitEffect(this, "Land");
        MR::startAction(this, "AttackEnd");
        MR::startSound(this, "SE_BM_ICEMERAKING_HIP_DROP");
        MR::tryRumblePadAndCameraDistanceStrong(this, 800.0f, 1200.0f, 2000.0f);
    }
    MR::moveAndTurnToPlayer(this, &mFront, ::hOnAirParam);

    if (MR::isGreaterStep(this, 90)) {
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
    }
}

void IceMerameraKing::exeAngryDemo() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "BodyIce");
        if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd))) {
            MR::startAction(this, "Summon");
            MR::startSound(this, "SE_BM_ICEMERAKING_PRE_ANGRY2");
        } else {
            MR::startAction(this, "Recover");
            MR::startSound(this, "SE_BM_ICEMERAKING_PRE_ANGRY1");
        }
    }

    if (MR::isStep(this, 1)) {
        MR::resetPosition(this, mInitPos);
    }
    MR::moveAndTurnToPlayer(this, &mFront, ::hAngryDemoParam);
    MR::addVelocityKeepHeightUseShadow(this, 400.0f, 1.5f, 300.0f, nullptr);

    if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd)) && MR::isStep(this, 90)) {
        for (int i = 0; i < mWeaponNum; i++) {
            mModelArray[i]->makeActorAppeared();
        }
    }

    if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd)) && MR::isStep(this, 85)) {
        MR::startSound(this, "SE_BM_ICEMERAKING_ANGRY2");
    } else if (MR::isStep(this, 85)) {
        MR::startSound(this, "SE_BM_ICEMERAKING_ANGRY1");
    }
    MR::playLevelMarioPinchBGM(true);

    if (MR::isDemoPartLastStep("怒りデモ")) {
        if (!(mHitPoint > 2)) {
            MR::appearStarPiece(this, mPosition - mGravity * 200.0f, 8, 15.0f, 70.0f, false);
        } else {
            MR::appearStarPiece(this, mPosition - mGravity * 200.0f, 16, 15.0f, 70.0f, false);
        }
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
    }
}

void IceMerameraKing::exeDeathDemoWait() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(30);
    }

    mVelocity.zero();
}

void IceMerameraKing::exeDeathDemo() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Death");
        MR::startSound(this, "SE_BM_ICEMERAKING_DEAD");
        mCameraTarget->mPosition.set(mPosition);
        mBreakModel->appear();
        mBreakModel->mRotation.set(mInitRotation);
        MR::resetPosition(mBreakModel, mPosition);
        MR::startAction(mBreakModel, "Break");
        MR::requestMovementOn(mBreakModel);

        for (s32 i = 0; i < mWeaponNum; i++) {
            mModelArray[i]->kill();
        }

        MR::invalidateShadow(this, nullptr);
    }

    s32 frame = MR::getBckFrameMax(this);

    if (MR::isOnGround(this)) {
        mVelocity.zero();
    } else {
        MR::moveAndTurnToPlayer(this, &mFront, ::hOnAirParam);
    }

    if (!MR::isHiddenModel(this) && MR::isGreaterStep(this, frame - 37)) {
        MR::hideModelAndOnCalcAnim(this);
    }

    if (!MR::isHiddenModel(mBreakModel) && MR::isActionEnd(mBreakModel)) {
        MR::hideModel(mBreakModel);
    }

    if (MR::isActionEnd(this) && MR::isHiddenModel(this) && MR::isHiddenModel(mBreakModel)) {
        MR::endDemo(this, "メラキンオープニング");
    }

    if (!MR::isDemoActive()) {
        kill();
        mBreakModel->kill();
        MR::requestAppearPowerStar(this, mPosition);
    }
}

void IceMerameraKing::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("body")) {
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDamage)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvExtinguish)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreRecover)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvRecover)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemoWait)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo1st)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemo)) ||
        MR::isPlayerElementModeIce()) {
        MR::sendMsgPush(pReceiver, pSender);
    } else {
        MR::sendMsgEnemyAttackFreeze(pReceiver, pSender);
    }
}

bool IceMerameraKing::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (!isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape)) && !isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump))) {
        return false;
    }

    if (MR::isMsgFreezeAttack(msg)) {
        mHitPoint--;

        if (mHitPoint == 0) {
            MR::stopMarioPinchBGMSoon();
            MR::requestStartTimeKeepDemo(this, "メラキンオープニング", GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemo),
                                         GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemoWait), "死亡デモ");
        } else {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDamage));
        }

        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        mHitPoint--;

        if (mHitPoint == 0) {
            MR::requestStartTimeKeepDemo(this, "メラキンオープニング", GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemo),
                                         GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemoWait), "死亡デモ");
        } else {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDamage));
        }

        return true;
    }

    return false;
}

bool IceMerameraKing::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isInSpinStormRange(msg, pSender, pReceiver, 600.0f)) {
        if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemo)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvExtinguish)) ||
            isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDamage)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo1st)) ||
            isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape)) ||
            isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreRecover)) ||
            isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvRecover))) {
            return false;
        } else {
            Vec3f v10 = *MR::getPlayerCenterPos() - mPosition;
            MR::vecKillElement(v10, mGravity, &v10);

            if (v10.length() < getSensor("body")->getRadius()) {
                return false;
            } else {
                setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvExtinguish));
                return true;
            }
        }
    }

    return false;
}

void IceMerameraKing::addVelocityToInitPos() {
    Vec3f v12 = mInitPos - mPosition;

    if (0.0f < mGravity.dot(v12)) {
        MR::vecKillElement(v12, mGravity, &v12);
    }
    v12.setLength(0.5f);
    mVelocity.add(v12);
}

void IceMerameraKing::calcAndSetBaseMtx() {
    Pos3f v3;
    MR::makeMtxUpFrontPos(&v3, mUp, mFront, mPosition);
    MR::setBaseTRMtx(this, v3);
    mFixedPos->calc();
    mJointController->registerCallBack();
}

bool IceMerameraKing::isDeadAllIce() {
    s32 count = mIce.mCount;
    for (s32 i = 0; i < count; i++) {
        if (!MR::isDead(mIce[i])) {
            return false;
        }
    }
    return true;
}

bool IceMerameraKing::isEnableThrow() {
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        return false;
    }

    if (MR::isPlayerInWaterMode() || MR::isPlayerOnWaterSurface()) {
        return false;
    }
    Vec3f v10 = *MR::getPlayerCenterPos() - mPosition;
    MR::normalizeOrZero(&v10);
    Vec3f v9;
    MR::calcUpVec(&v9, this);
    bool flag = false;
    flag = -0.60f < v9.dot(v10);
    return flag;
}

ThrowingIce* IceMerameraKing::getDeadWeaponAndAppear() {
    ThrowingIce* v7;
    s32 tempE4 = mWeaponIndex;
    s32 count = mIce.mCount;
    for (s32 i = 0; i < count; i++) {
        if (MR::isDead(mIce[tempE4])) {
            MR::resetPosition(mIce[tempE4], mPosition);
            LiveActor* weapon = mIce[tempE4];
            weapon->appear();
            mIce.begin();
            mWeaponIndex += count;
            mWeaponIndex++;
            mWeaponIndex %= count;
            v7 = mIce[tempE4];
            return v7;
        }

        tempE4 = (tempE4 + count + 1) % count;
    }
    return nullptr;
}

bool IceMerameraKing::calcJoint(Pos3f* a2, const JointControllerInfo& info) {
    Pos3f mtx;
    Vec3f v3;
    mtx.makeQuat(mRollQuat);
    a2->getTrans(v3);
    a2->zeroTransInline2();
    a2->concat(mtx, *a2);
    a2->setTrans(v3);
    return true;
}

namespace {
    static const char* hScaleJointName[] = {"WideInSide", "WideOutSide", "Hirgh1"};
};  // namespace

IceMerameraKingShockWave::IceMerameraKingShockWave() : ModelObj("衝撃", "IceMerameraKingShock", nullptr, -2, -2, -2, false) {
    initHitSensor(2);
    MR::addHitSensorEnemyAttack(this, "circle", 16, 15.0f, Vec3f(0.0f, -100.0f, 0.0f));
    MR::addHitSensorEnemyAttack(this, "circle_end", 16, 24.0f, Vec3f(0.0f, -100.0f, 0.0f));
}

void IceMerameraKingShockWave::appear() {
    LiveActor::appear();
    MR::startAction(this, "Shock");
    HitSensor* sensor = getSensor("circle");
    sensor->mRadius = 0.0f;
}

void IceMerameraKingShockWave::control() {
    Vec3f v7;
    MR::copyJointScale(this, ::hScaleJointName[2], &v7);
    f32 v3 = 15.0f * v7.y;
    getSensor("circle")->mRadius = v3;
    MR::copyJointScale(this, ::hScaleJointName[0], &v7);
    f32 v4 = 24.0f * v7.y;
    getSensor("circle_end")->mRadius = v4;
    if (MR::isActionEnd(this)) {
        kill();
    }
}

void IceMerameraKingShockWave::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("circle_end") && MR::isSensorPlayer(pReceiver)) {
        HitSensor* sensor = getSensor("circle_end");
        Vec3f v14 = sensor->mPosition - pReceiver->mPosition;
        if (v14.length() < sensor->getRadius() + pReceiver->getRadius()) {
            return;
        }

        Vec3f v15 = pReceiver->mPosition - pSender->mPosition;
        Vec3f v16;
        MR::calcUpVec(&v16, this);

        if (MR::abs(v15.dot(v16)) < 200.0f) {
            MR::vecKillElement(v15, v16, &v15);
            MR::sendMsgEnemyAttackFlipMaximumToDir(pReceiver, pSender, v15);
        }
    }
}
