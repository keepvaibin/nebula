#include "Game/Enemy/IceMerameraKing.hpp"
#include "Game/System/NebulaFrameClock.hpp"
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
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 1.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThrow_f1, "exeThrow.f1", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThrow_f2, "exeThrow.f2", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExtinguish_f1, "exeExtinguish.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExtinguish_f2, "exeExtinguish.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f1, "exeEscape.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f2, "exeEscape.f2", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f3, "exeEscape.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f4, "exeEscape.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f5, "exeEscape.f5", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscape_f6, "exeEscape.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscapeJump_f1, "exeEscapeJump.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscapeJump_f2, "exeEscapeJump.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackAfter_f1, "exeAttackAfter.f1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackAfter_f2, "exeAttackAfter.f2", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackAfter_f3, "exeAttackAfter.f3", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryDemo_f1, "exeAngryDemo.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryDemo_f2, "exeAngryDemo.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryDemo_f3, "exeAngryDemo.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addVelocityToInitPos_f1, "addVelocityToInitPos.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_isEnableThrow_f1, "isEnableThrow.f1", 0.60f);
NEBULA_TUNABLE_NAMED(f32, cNum_IceMerameraKingShockWave_f1, "IceMerameraKingShockWave.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_IceMerameraKingShockWave_f2, "IceMerameraKingShockWave.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_IceMerameraKingShockWave_f3, "IceMerameraKingShockWave.f3", 24.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_IceMerameraKingShockWave_f4, "IceMerameraKingShockWave.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 24.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 200.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_addVelocityKeepHeightUseShadow_1, "exeSearch.addVelocityKeepHeightUseShadow.1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_addVelocityKeepHeightUseShadow_2, "exeSearch.addVelocityKeepHeightUseShadow.2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_addVelocityKeepHeightUseShadow_3, "exeSearch.addVelocityKeepHeightUseShadow.3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThrow_addVelocityKeepHeightUseShadow_1, "exeThrow.addVelocityKeepHeightUseShadow.1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThrow_addVelocityKeepHeightUseShadow_2, "exeThrow.addVelocityKeepHeightUseShadow.2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThrow_addVelocityKeepHeightUseShadow_3, "exeThrow.addVelocityKeepHeightUseShadow.3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExtinguish_addVelocityKeepHeightUseShadow_1, "exeExtinguish.addVelocityKeepHeightUseShadow.1", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExtinguish_addVelocityKeepHeightUseShadow_2, "exeExtinguish.addVelocityKeepHeightUseShadow.2", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscapeJump_addVelocitySeparateHV_1, "exeEscapeJump.addVelocitySeparateHV.1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscapeJump_addVelocitySeparateHV_2, "exeEscapeJump.addVelocitySeparateHV.2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_setVelocityJump_1, "exeDamage.setVelocityJump.1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_addVelocityKeepHeightUseShadow_1, "exeDamage.addVelocityKeepHeightUseShadow.1", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_addVelocityKeepHeightUseShadow_2, "exeDamage.addVelocityKeepHeightUseShadow.2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_addVelocityKeepHeightUseShadow_3, "exeDamage.addVelocityKeepHeightUseShadow.3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_setVelocityJump_1, "exePreRecover.setVelocityJump.1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_addVelocityKeepHeightUseShadow_1, "exePreRecover.addVelocityKeepHeightUseShadow.1", 750.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_addVelocityKeepHeightUseShadow_2, "exePreRecover.addVelocityKeepHeightUseShadow.2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_addVelocityKeepHeightUseShadow_3, "exePreRecover.addVelocityKeepHeightUseShadow.3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityKeepHeightUseShadow_1, "exeRecover.addVelocityKeepHeightUseShadow.1", 750.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityKeepHeightUseShadow_2, "exeRecover.addVelocityKeepHeightUseShadow.2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_addVelocityKeepHeightUseShadow_3, "exeRecover.addVelocityKeepHeightUseShadow.3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreAttack_addVelocityKeepHeightUseShadow_1, "exePreAttack.addVelocityKeepHeightUseShadow.1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreAttack_addVelocityKeepHeightUseShadow_2, "exePreAttack.addVelocityKeepHeightUseShadow.2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreAttack_addVelocityKeepHeightUseShadow_3, "exePreAttack.addVelocityKeepHeightUseShadow.3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_addVelocityKeepHeightUseShadow_1, "exeAttack.addVelocityKeepHeightUseShadow.1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_addVelocityKeepHeightUseShadow_2, "exeAttack.addVelocityKeepHeightUseShadow.2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_addVelocityKeepHeightUseShadow_3, "exeAttack.addVelocityKeepHeightUseShadow.3", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryDemo_addVelocityKeepHeightUseShadow_1, "exeAngryDemo.addVelocityKeepHeightUseShadow.1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryDemo_addVelocityKeepHeightUseShadow_2, "exeAngryDemo.addVelocityKeepHeightUseShadow.2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAngryDemo_addVelocityKeepHeightUseShadow_3, "exeAngryDemo.addVelocityKeepHeightUseShadow.3", 300.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeSearch_1, "exeSearch.1", 100);
NEBULA_TUNABLE_NAMED(s32, cStep_exeSearch_2, "exeSearch.2", 200);
NEBULA_TUNABLE_NAMED(s32, cStep_exeThrow_1, "exeThrow.1", 22);
NEBULA_TUNABLE_NAMED(s32, cStep_exeExtinguish_1, "exeExtinguish.1", 40);
NEBULA_TUNABLE_NAMED(s32, cStep_exeEscapeJump_1, "exeEscapeJump.1", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exeEscapeJump_2, "exeEscapeJump.2", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDamage_1, "exeDamage.1", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exePreRecover_1, "exePreRecover.1", 50);
NEBULA_TUNABLE_NAMED(s32, cStep_exePreRecover_2, "exePreRecover.2", 50);
NEBULA_TUNABLE_NAMED(s32, cStep_exeRecover_1, "exeRecover.1", 80);
NEBULA_TUNABLE_NAMED(s32, cStep_exeRecover_2, "exeRecover.2", 160);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAngryDemo_1, "exeAngryDemo.1", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAngryDemo_2, "exeAngryDemo.2", 85);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAngryDemo_3, "exeAngryDemo.3", 85);

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
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hOnAirParam, {0.0f, 3.0f, 0.96f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hOnGroundParam, {0.0f, 1.0f, 0.8f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hFlyParam, {0.0f, 0.5f, 0.9f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hAngryDemoParam, {0.0f, 0.5f, 0.9f, 1.5f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hEscapeOnGroundParam, {-0.7f, 1.0f, 0.96f, 180.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hEscapeOnAirParam, {0.0f, 3.0f, 0.96f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hPreRecoverJumpParam, {0.0f, 1.0f, 0.96f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hDamageJumpParam, {0.0f, 1.9f, 0.98f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hAttackParam, {0.0f, 6.0f, 0.98f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hExtinguishOnAirParam, {0.0f, 0.125f, 0.96f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hExtinguishFallOnAirParam, {0.0f, 4.0f, 0.96f, 0.0f});
    static const Vec3f hBinderOffset(-100.586f, 222.65601f, -91.796898f);

    NEBULA_TUNABLE(s32, sIceNum, 6);
    NEBULA_TUNABLE(f32, sBodyRadius, 130.0f);  // at joint scale 1
    NEBULA_TUNABLE(f32, sAttackDistance, 1000.0f);
    NEBULA_TUNABLE(f32, sSearchDistance, 4500.0f);
    NEBULA_TUNABLE(f32, sPinchBgmDistance, 5000.0f);
    const u32 sEscapeTime = NebulaFrameClock::stepsOfSeconds(10);
    // Shock wave hit radii by the scale of these joints.
    const char* const sShockCircleJoint = "Hirgh1";
    const char* const sShockCircleEndJoint = "WideInSide";
};  // namespace

IceMerameraKing::IceMerameraKing(const char* pName)
    : LiveActor(pName), mFixedPos(nullptr), mThrowingIce(nullptr), mMeramera(nullptr), mIce(), mShockWave(nullptr), mBreakModel(nullptr), mFront(0, 0, 1), mUp(0, 1, 0),
      mInitPos(0, 0, 0), mInitRotation(0, 0, 0), mThrowNum(0), mWeaponIndex(0), mHitPoint(3), mWeaponNum(0), mModelArray(nullptr), mCameraTarget(nullptr), mJointController(nullptr),
      mRollQuat(0, 0, 0, 1), mBinderOffset(0.0f, 0.0f, 0.0f), mExtinguishHeight(0.0f), mIsPinchBgmOn(false), mIsPinchBgmAllowed(false) {
    lumacore::setClass(this, lumacore::kClassId< IceMerameraKing >);
}

void IceMerameraKing::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        mInitPos.set(mPosition);
        mInitRotation.set(mRotation);
        MR::useStageSwitchWriteDead(this, rIter);
        MR::useStageSwitchReadA(this, rIter);
    }

    mScale.set(cNum_init_f1, cNum_init_f2, cNum_init_f3);
    initModelManagerWithAnm("IceMerameraKing", nullptr, false);
    MR::calcUpVec(&mUp, this);
    MR::calcFrontVec(&mFront, this);
    MR::connectToSceneEnemy(this);
    f32 radiusScale = (mScale.y / cNum_init_f4);
    initBinder((sBodyRadius * radiusScale), (sBodyRadius * radiusScale), 0);
    MR::setBinderOffsetVec(this, &mBinderOffset, true);
    initHitSensor(1);
    MR::addHitSensorAtJointEnemy(this, "body", "JointRoot", 32, (sBodyRadius * radiusScale), Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
    MR::calcGravity(this);
    MR::initShadowVolumeSphere(this, (sBodyRadius * mScale.y));
    MR::setShadowDropLength(this, nullptr, cNum_init_f5);
    MR::declarePowerStar(this);
    MR::declareStarPiece(this, 24);
    mFixedPos = new FixedPosition(this, "Top", Vec3f(0, 0, 0), Vec3f(0.0f, 0.0f, 0.0f));
    MR::tryRegisterDemoCast(this, rIter);
    mIce.init(sIceNum);
    for (s32 i = 0; i < sIceNum; i++) {
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
    // The child objects are the Meramera the second angry demo summons.
    s32 childNum = MR::getChildObjNum(rIter);
    if (childNum < 0) {
        childNum = 0;
    }
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
        binder->mIsExCollisionValid = exCollisionParts != nullptr;
    }
}

void IceMerameraKing::kill() {
    LiveActor::kill();

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
    MR::startAfterBossBGM();
    s32 count = mIce.mCount;

    for (s32 i = 0; i < count; i++) {
        if (!MR::isDead(mIce[i])) {
            mIce[i]->kill();
        }
    }
}

// The body's binder, sensor and shadow follow the "Center" joint's scale; the binder sits one radius up.
void IceMerameraKing::control() {
    Vec3f jointScale;
    MR::copyJointScale(this, "Center", &jointScale);
    f32 radius = sBodyRadius * jointScale.x;
    MR::setBinderRadius(this, radius);
    mBinderOffset.y = radius;
    mBinderOffset.x = 0.0f;
    mBinderOffset.z = 0.0f;
    getSensor("body")->mRadius = radius;
    MR::setShadowVolumeSphereRadius(this, nullptr, radius);

    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            MR::invalidateClipping(this);
        } else {
            MR::validateClipping(this);
        }
    }

    // Out of the escape roll the body rights itself.
    if (!isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape)) && !isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump))) {
        Quat4f identity(0, 0, 0, 1);
        mRollQuat.slerp(identity, cNum_control_f1);
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
               MR::isNearPlayer(this, sPinchBgmDistance) && mIsPinchBgmAllowed) {
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
    MR::addVelocityKeepHeightUseShadow(this, cNum_exeSearch_addVelocityKeepHeightUseShadow_1, cNum_exeSearch_addVelocityKeepHeightUseShadow_2, cNum_exeSearch_addVelocityKeepHeightUseShadow_3, nullptr);
    f32 playerDist = MR::calcDistanceToPlayer(this);

    if (sSearchDistance < playerDist) {
        return;
    }

    if (playerDist < sAttackDistance) {
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreAttack));
        return;
    }

    // Full health throws 3 every 200 steps; once hit, 6 every 100.
    if (isEnableThrow()) {
        if (mHitPoint <= 2) {
            if (MR::isGreaterStep(this, cStep_exeSearch_1) && isDeadAllIce()) {
                mThrowNum = 0;
                setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow2nd));
            }
        } else if (MR::isGreaterStep(this, cStep_exeSearch_2) && isDeadAllIce()) {
            mThrowNum = 0;
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow));
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
    MR::addVelocityKeepHeightUseShadow(this, cNum_exeThrow_addVelocityKeepHeightUseShadow_1, cNum_exeThrow_addVelocityKeepHeightUseShadow_2, cNum_exeThrow_addVelocityKeepHeightUseShadow_3, nullptr);
    addVelocityToInitPos();

    if (MR::isStep(this, cStep_exeThrow_1)) {
        mThrowingIce = getDeadWeaponAndAppear();
        if (mThrowingIce == nullptr) {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
            return;
        }

        // Thrown from the "Top" joint at where the player will be in 35 steps.
        mFixedPos->mMtx.getTrans(mThrowingIce->mPosition);
        mThrowNum += 1;
        Vec3f& icePos = mThrowingIce->mPosition;
        mThrowingIce->emitIce(icePos, *MR::getPlayerCenterPos() + *MR::getPlayerVelocity() * cNum_exeThrow_f1, -cNum_exeThrow_f2, mGravity);
        mThrowingIce = nullptr;
        MR::startSound(this, "SE_BM_ICEMERAKING_THROW");
    }

    if (MR::isActionEnd(this)) {
        if (MR::calcDistanceToPlayer(this) < sAttackDistance) {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreAttack));
        } else {
            const s32 throwMax = isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvThrow2nd)) ? 6 : 3;

            if (throwMax <= mThrowNum || !isEnableThrow()) {
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
        Vec3f awayFromPlayer = mPosition - *MR::getPlayerCenterPos();
        MR::vecKillElement(awayFromPlayer, mGravity, &awayFromPlayer);
        MR::normalizeOrZero(&awayFromPlayer);
        MR::setVelocitySeparateHV(this, awayFromPlayer, cNum_exeExtinguish_f1, 0.0f);
        mExtinguishHeight = cNum_exeExtinguish_f2 + MR::getShadowNearProjectionLength(this);
    }

    if (MR::isLessStep(this, cStep_exeExtinguish_1)) {
        MR::addVelocityKeepHeightUseShadow(this, mExtinguishHeight, cNum_exeExtinguish_addVelocityKeepHeightUseShadow_1, cNum_exeExtinguish_addVelocityKeepHeightUseShadow_2, nullptr);
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

// Extinguished, it rolls away from the player (escape parameters move backward) for sEscapeTime steps over the escape
// and its jumps, jumping at a wall behind it.
void IceMerameraKing::exeEscape() {
    mEscapeStep++;

    if (mEscapeStep > sEscapeTime && MR::isOnGround(this)) {
        mEscapeStep = 0;
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreRecover));
        return;
    }

    MR::emitEffect(this, "Rolling");
    if (MR::isOnGround(this)) {
        f32 rollVolume = MR::getLinerValueFromMinMax(mVelocity.length(), cNum_exeEscape_f1, cNum_exeEscape_f2, 0.0f, cNum_exeEscape_f3);
        MR::startLevelSound(this, "SE_BM_LV_ICEMERAKING_ROLL", rollVolume, -1, 15);

        f32 radius = getSensor("body")->mRadius;
        MR::rotateQuatRollBall(&mRollQuat, mVelocity, *MR::getGroundNormal(this), (cNum_exeEscape_f4 * radius));
        MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnGroundParam);
        if (MR::isBindedWallFront(this, -mFront, cNum_exeEscape_f5)) {
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump));
        }
    } else {
        f32 radius = getSensor("body")->mRadius;
        MR::rotateQuatRollBall(&mRollQuat, mVelocity, -mGravity, cNum_exeEscape_f6 * radius);
        MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnAirParam);
    }
}

void IceMerameraKing::endEscape() {
    MR::deleteEffect(this, "Rolling");
}

void IceMerameraKing::exeEscapeJump() {
    if (MR::isStep(this, cStep_exeEscapeJump_1)) {
        MR::addVelocitySeparateHV(this, mFront, cNum_exeEscapeJump_addVelocitySeparateHV_1, cNum_exeEscapeJump_addVelocitySeparateHV_2);
        MR::startSound(this, "SE_BM_ICEMERAKING_JUMP");
    }
    mEscapeStep++;

    if (MR::isOnGround(this)) {
        f32 radius = getSensor("body")->mRadius;
        MR::rotateQuatRollBall(&mRollQuat, mVelocity, *MR::getGroundNormal(this), (cNum_exeEscapeJump_f1 * radius));
        MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnGroundParam);

        if (MR::isGreaterStep(this, cStep_exeEscapeJump_2)) {
            MR::emitEffect(this, "Land");
            MR::startSound(this, "SE_BM_ICEMERAKING_LAND");
            setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape));
        }
    } else {
        f32 radius = getSensor("body")->mRadius;
        MR::rotateQuatRollBall(&mRollQuat, mVelocity, -mGravity, (cNum_exeEscapeJump_f2 * radius));
        MR::moveAndTurnToPlayer(this, &mFront, ::hEscapeOnAirParam);
    }
}

void IceMerameraKing::exeDamage() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Damage");
        MR::emitEffect(this, "Damage");
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_BM_ICEMERAKING_DAMAGE");
        MR::setVelocityJump(this, cNum_exeDamage_setVelocityJump_1);
        MR::stopSceneForDefaultHit(4);
    }

    if (MR::isGreaterStep(this, cStep_exeDamage_1)) {
        MR::addVelocityKeepHeightUseShadow(this, cNum_exeDamage_addVelocityKeepHeightUseShadow_1, cNum_exeDamage_addVelocityKeepHeightUseShadow_2, cNum_exeDamage_addVelocityKeepHeightUseShadow_3, nullptr);
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToPlayer(this, &mFront, ::hOnGroundParam);
    } else {
        MR::moveAndTurnToPlayer(this, &mFront, ::hDamageJumpParam);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
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
        MR::setVelocityJump(this, cNum_exePreRecover_setVelocityJump_1);
        MR::startSound(this, "SE_BM_ICEMERAKING_JUMP");
    }
    addVelocityToInitPos();

    if (MR::isLessStep(this, cStep_exePreRecover_1)) {
        MR::moveAndTurnToPlayer(this, &mFront, ::hPreRecoverJumpParam);
    } else if (MR::isStep(this, cStep_exePreRecover_2)) {
        MR::startAction(this, "PreRecover");
        MR::setBinderRadius(this, sBodyRadius);
        getSensor("body")->mRadius = sBodyRadius;
        MR::setShadowVolumeSphereRadius(this, nullptr, sBodyRadius);
        MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    } else if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::addVelocityKeepHeightUseShadow(this, cNum_exePreRecover_addVelocityKeepHeightUseShadow_1, cNum_exePreRecover_addVelocityKeepHeightUseShadow_2, cNum_exePreRecover_addVelocityKeepHeightUseShadow_3, nullptr);
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

    if (MR::isStep(this, cStep_exeRecover_1)) {
        MR::startSound(this, "SE_BM_ICEMERAKING_RECOVER");
    }
    addVelocityToInitPos();
    MR::moveAndTurnToPlayer(this, &mFront, ::hFlyParam);
    MR::addVelocityKeepHeightUseShadow(this, cNum_exeRecover_addVelocityKeepHeightUseShadow_1, cNum_exeRecover_addVelocityKeepHeightUseShadow_2, cNum_exeRecover_addVelocityKeepHeightUseShadow_3, nullptr);

    if (MR::isGreaterStep(this, cStep_exeRecover_2)) {
        setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvSearch));
    }
}

// Hovers for 120 steps, then drops onto the player once the last shock wave is gone.
void IceMerameraKing::exePreAttack() {
    if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::addVelocityKeepHeightUseShadow(this, cNum_exePreAttack_addVelocityKeepHeightUseShadow_1, cNum_exePreAttack_addVelocityKeepHeightUseShadow_2, cNum_exePreAttack_addVelocityKeepHeightUseShadow_3, nullptr);
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
        MR::addVelocityKeepHeightUseShadow(this, cNum_exeAttack_addVelocityKeepHeightUseShadow_1, cNum_exeAttack_addVelocityKeepHeightUseShadow_2, cNum_exeAttack_addVelocityKeepHeightUseShadow_3, nullptr);
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
        MR::tryRumblePadAndCameraDistanceStrong(this, cNum_exeAttackAfter_f1, cNum_exeAttackAfter_f2, cNum_exeAttackAfter_f3);
    }
    MR::moveAndTurnToPlayer(this, &mFront, ::hOnAirParam);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
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

    if (MR::isStep(this, cStep_exeAngryDemo_1)) {
        MR::resetPosition(this, mInitPos);
    }
    MR::moveAndTurnToPlayer(this, &mFront, ::hAngryDemoParam);
    MR::addVelocityKeepHeightUseShadow(this, cNum_exeAngryDemo_addVelocityKeepHeightUseShadow_1, cNum_exeAngryDemo_addVelocityKeepHeightUseShadow_2, cNum_exeAngryDemo_addVelocityKeepHeightUseShadow_3, nullptr);

    if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd)) && MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        for (int i = 0; i < mWeaponNum; i++) {
            mModelArray[i]->makeActorAppeared();
        }
    }

    if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd)) && MR::isStep(this, cStep_exeAngryDemo_2)) {
        MR::startSound(this, "SE_BM_ICEMERAKING_ANGRY2");
    } else if (MR::isStep(this, cStep_exeAngryDemo_3)) {
        MR::startSound(this, "SE_BM_ICEMERAKING_ANGRY1");
    }
    MR::playLevelMarioPinchBGM(true);

    if (MR::isDemoPartLastStep("怒りデモ")) {
        MR::appearStarPiece(this, mPosition - mGravity * cNum_exeAngryDemo_f1, mHitPoint <= 2 ? 8 : 16, cNum_exeAngryDemo_f2, cNum_exeAngryDemo_f3, false);
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

bool IceMerameraKing::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
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
    // A spin storm within 600 blows the fire out, unless the player is under or over the body.
    if (!MR::isInSpinStormRange(msg, pSender, pReceiver, cNum_receiveOtherMsg_f1)) {
        return false;
    }

    if (isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDeathDemo)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvExtinguish)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvDamage)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo1st)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvAngryDemo2nd)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscape)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvEscapeJump)) || isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvPreRecover)) ||
        isNerve(GET_NERVE(IceMerameraKing, HostTypeNrvRecover))) {
        return false;
    }

    Vec3f toPlayer = *MR::getPlayerCenterPos() - mPosition;
    MR::vecKillElement(toPlayer, mGravity, &toPlayer);

    if (toPlayer.length() < getSensor("body")->getRadius()) {
        return false;
    }

    setNerve(GET_NERVE(IceMerameraKing, HostTypeNrvExtinguish));
    return true;
}

// Pulls 0.5 a step toward the placed position, only across gravity when that is below.
void IceMerameraKing::addVelocityToInitPos() {
    Vec3f toInit = mInitPos - mPosition;

    if (0.0f < mGravity.dot(toInit)) {
        MR::vecKillElement(toInit, mGravity, &toInit);
    }
    toInit.setLength(cNum_addVelocityToInitPos_f1);
    mVelocity.add(toInit);
}

void IceMerameraKing::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    MR::makeMtxUpFrontPos(&baseMtx, mUp, mFront, mPosition);
    MR::setBaseTRMtx(this, baseMtx);
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
    // Not when the player is far below the body's up (cos -0.6).
    Vec3f toPlayer = *MR::getPlayerCenterPos() - mPosition;
    MR::normalizeOrZero(&toPlayer);
    Vec3f up;
    MR::calcUpVec(&up, this);
    return -cNum_isEnableThrow_f1 < up.dot(toPlayer);
}

// The first dead ice from mWeaponIndex on, appeared at the body; the next search starts one past the last start.
ThrowingIce* IceMerameraKing::getDeadWeaponAndAppear() {
    s32 index = mWeaponIndex;
    s32 count = mIce.mCount;
    for (s32 i = 0; i < count; i++) {
        if (MR::isDead(mIce[index])) {
            MR::resetPosition(mIce[index], mPosition);
            mIce[index]->appear();
            mWeaponIndex = (mWeaponIndex + 1) % count;
            return mIce[index];
        }

        index = (index + 1) % count;
    }
    return nullptr;
}

// The escape roll turns the joint about its own position.
bool IceMerameraKing::calcJoint(Pos3f* pJointMtx, const JointControllerInfo& /*rInfo*/) {
    Pos3f rollMtx;
    Vec3f trans;
    rollMtx.makeQuat(mRollQuat);
    pJointMtx->getTrans(trans);
    pJointMtx->zeroTransInline2();
    pJointMtx->concat(rollMtx, *pJointMtx);
    pJointMtx->setTrans(trans);
    return true;
}

IceMerameraKingShockWave::IceMerameraKingShockWave() : ModelObj("衝撃", "IceMerameraKingShock", nullptr, -2, -2, -2, false) {
    lumacore::setClass(this, lumacore::kClassId< IceMerameraKingShockWave >);
    initHitSensor(2);
    MR::addHitSensorEnemyAttack(this, "circle", 16, cNum_IceMerameraKingShockWave_f1, Vec3f(0.0f, -cNum_IceMerameraKingShockWave_f2, 0.0f));
    MR::addHitSensorEnemyAttack(this, "circle_end", 16, cNum_IceMerameraKingShockWave_f3, Vec3f(0.0f, -cNum_IceMerameraKingShockWave_f4, 0.0f));
}

void IceMerameraKingShockWave::appear() {
    LiveActor::appear();
    MR::startAction(this, "Shock");
    HitSensor* sensor = getSensor("circle");
    sensor->mRadius = 0.0f;
}

void IceMerameraKingShockWave::control() {
    Vec3f jointScale;
    MR::copyJointScale(this, sShockCircleJoint, &jointScale);
    f32 circleRadius = cNum_control_f2 * jointScale.y;
    getSensor("circle")->mRadius = circleRadius;
    MR::copyJointScale(this, sShockCircleEndJoint, &jointScale);
    f32 endRadius = cNum_control_f3 * jointScale.y;
    getSensor("circle_end")->mRadius = endRadius;
    if (MR::isActionEnd(this)) {
        kill();
    }
}

// The ring is "circle" less "circle_end": a player in it within 200 along the up is flipped outward.
void IceMerameraKingShockWave::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender == getSensor("circle_end") || !MR::isSensorPlayer(pReceiver)) {
        return;
    }

    HitSensor* pEnd = getSensor("circle_end");
    Vec3f toEnd = pEnd->mPosition - pReceiver->mPosition;
    if (toEnd.length() < pEnd->getRadius() + pReceiver->getRadius()) {
        return;
    }

    Vec3f outward = pReceiver->mPosition - pSender->mPosition;
    Vec3f up;
    MR::calcUpVec(&up, this);

    if (MR::abs(outward.dot(up)) < cNum_attackSensor_f1) {
        MR::vecKillElement(outward, up, &outward);
        MR::sendMsgEnemyAttackFlipMaximumToDir(pReceiver, pSender, outward);
    }
}
