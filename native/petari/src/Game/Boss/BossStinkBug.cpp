#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Boss/BossStinkBugActionSequencer.hpp"
#include "Game/Boss/BossStinkBugBomb.hpp"
#include "Game/Boss/BossStinkBugBombHolder.hpp"
#include "Game/Boss/BossStinkBugFollowValidater.hpp"
#include "Game/LiveActor/ActorJointCtrl.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/GroundChecker.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_BossStinkBug_f1, "BossStinkBug.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f5, "initSensor.f5", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f6, "initSensor.f6", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f7, "initSensor.f7", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f8, "initSensor.f8", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f9, "initSensor.f9", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePose_f1, "updatePose.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePose_f2, "updatePose.f2", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointControl_f1, "updateJointControl.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointControl_f2, "updateJointControl.f2", 0.08f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointControl_f3, "updateJointControl.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointControl_f4, "updateJointControl.f4", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPiece_f1, "appearStarPiece.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStarPiece_f2, "appearStarPiece.f2", 40.0f);

namespace {
    const char* const sSensorNameList[] = {"Body", "BodyHipDrop"};

    struct CollisionInfo {
        const char* mCollisionName;
        const char* mJointName;
        const char* mSensorName;
    };

    const CollisionInfo sCollisionInfo[] = {{"Body", "Body", "Body"}, {"BodyGround", "Body", "Body"}, {"BodyHipDrop", "Body", "BodyHipDrop"}};

    const char* const sLegJointNameList[] = {"RightFootA3", "LeftFootA3", "RightFootB3", "LeftFootB3", "RightFootC3", "LeftFootC3"};

    const s32 sBodyCollisionNum = ARRAY_SIZE(sCollisionInfo);
    const s32 sLegNum = ARRAY_SIZE(sLegJointNameList);

    static Vec3f sAppearStarPieceOffset(0.0f, 200.0f, 0.0f);
};  // namespace

BossStinkBug::BossStinkBug(const char* pName)
    : LiveActor(pName), mBodyCollisions(), mBombHolder(), mJointCtrl(), mCameraInfo(), mActionSequencer(), mWingModel(), mBombLauncher(), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mFront(0.0f, 0.0f, 1.0f),
      mCameraRegisterPos(0.0f, 0.0f, 0.0f), mCameraRegisterId(-1), mLegFriction(0.15f), mLegIKRate(1.0f), mIsPoseUpdated(), mIsLegBound() {
    lumacore::setClass(this, lumacore::kClassId< BossStinkBug >);
    mFollowMtx.identity();
    mLegGroundCheckers = new GroundChecker*[::sLegNum];

    for (s32 i = 0; i < ::sLegNum; i++) {
        mLegGroundCheckers[i] = new GroundChecker("地面チェック", cNum_BossStinkBug_f1, 0.0f);
        mLegGroundCheckers[i]->makeActorDead();
        MR::setBinderExceptActor(mLegGroundCheckers[i], this);
    }
}

void BossStinkBug::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    invalidateCollision();
}

void BossStinkBug::makeActorDead() {
    LiveActor::makeActorDead();
    invalidateCollision();
}

void BossStinkBug::kill() {
    LiveActor::kill();

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void BossStinkBug::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("BossStinkBug", nullptr, false);
    MR::connectToSceneCollisionEnemy(this);
    MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mFront, this);

    mRotation.zero();
    initSensor();
    initCollision();
    initRailRider(rIter);
    initBinder(cNum_init_f1, cNum_init_f2, false);
    MR::setBinderExceptActor(this, this);
    initEffectKeeper(0, nullptr);
    MR::onCalcGravity(this);
    initCamera(rIter);
    MR::initShadowFromCSV(this, "Shadow");
    MR::excludeCalcShadowToActorAll(this, this);
    mJointCtrl = new ActorJointCtrl(this);
    MR::addBaseMatrixFollowTarget(this, rIter, &mFollowMtx, new BossStinkBugFollowValidater(this));
    MR::invalidateClipping(this);
    mActionSequencer = new BossStinkBugActionSequencer(this, rIter);
    mBombHolder = new BossStinkBugBombHolder(this);
    mWingModel = MR::createPartsModelMapObj(this, "羽モデル", "BossStinkBugWing", nullptr);
    mWingModel->initFixedPosition("Switch");
    mBombLauncher = MR::createPartsModelMapObj(this, "ボム発射口", "BossStinkBugBombLauncher", nullptr);
    mBombLauncher->initFixedPosition("Body");
    MR::declarePowerStar(this);
    initSound(12, false);
    MR::startBrk(this, "Normal");

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &BossStinkBug::start));
        makeActorDead();
    } else {
        makeActorAppeared();
    }

    MR::useStageSwitchWriteDead(this, rIter);
}

void BossStinkBug::start() {
    if (mActionSequencer->startAction()) {
        appear();
    }
}

void BossStinkBug::initCamera(const TableIter& rIter) {
    MR::getPlacementArg7WithInit(rIter, &mCameraRegisterId);

    if (mCameraRegisterId != -1) {
        MR::declareCameraRegisterVec(this, mCameraRegisterId, &mCameraRegisterPos);
        MR::declareCameraRegisterMtx(this, mCameraRegisterId, mFollowMtx.toMtxPtr());
    }

    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "Fall");
}

void BossStinkBug::initSensor() {
    initHitSensor(5);
    MR::addHitSensorEye(this, "eye", 8, cNum_initSensor_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateHitSensor(this, "eye");
    MR::addHitSensorEnemyAttack(this, "attack_left", 8, cNum_initSensor_f2, Vec3f(-cNum_initSensor_f3, cNum_initSensor_f4, cNum_initSensor_f5));
    MR::addHitSensorEnemyAttack(this, "attack_right", 8, cNum_initSensor_f6, Vec3f(cNum_initSensor_f7, cNum_initSensor_f8, cNum_initSensor_f9));
    MR::invalidateHitSensor(this, "attack_left");
    MR::invalidateHitSensor(this, "attack_right");

    for (int i = 0; i < ARRAY_SIZE(::sSensorNameList); i++) {
        MR::addMessageSensorReceiver(this, ::sSensorNameList[i]);
    }
}

void BossStinkBug::initCollision() {
    mBodyCollisions = new CollisionParts*[::sBodyCollisionNum];

    for (s32 i = 0; i < ::sBodyCollisionNum; i++) {
        mBodyCollisions[i] = MR::createCollisionPartsFromLiveActor(this, ::sCollisionInfo[i].mCollisionName, getSensor(::sCollisionInfo[i].mSensorName),
                                                       MR::getJointMtx(this, ::sCollisionInfo[i].mJointName), MR::CollisionScaleType_AutoEqualScale);
    }
}

void BossStinkBug::disposeBomb() {
    mBombHolder->killAll();
}

void BossStinkBug::reuestMovementOnParts() {
    MR::requestMovementOn(mWingModel);
    MR::requestMovementOn(mBombLauncher);
}

LiveActor* BossStinkBug::getWingModel() {
    return mWingModel;
}

LiveActor* BossStinkBug::getBombLauncher() {
    return mBombLauncher;
}

void BossStinkBug::control() {
    updateAction();
    updatePose();
    mIsPoseUpdated = false;

    if (mIsLegBound) {
        updateJointControl();
    }

    updateCamera();
}

void BossStinkBug::calcAndSetBaseMtx() {
    mJointCtrl->setCallBackFunction();
    Pos3f pos;
    pos.setQuat(mPoseQuat);
    pos.setTrans(mPosition);

    mFollowMtx.setInline(pos);

    MR::rotateMtxLocalXDegree(pos.toMtxPtr(), mRotation.x);
    MR::rotateMtxLocalYDegree(pos.toMtxPtr(), mRotation.y);
    MR::rotateMtxLocalZDegree(pos.toMtxPtr(), mRotation.z);

    MR::setBaseTRMtx(this, pos);
    MR::setBaseScale(this, mScale);
}

void BossStinkBug::calcUpVec(Vec3f* pOut) const {
    Pos3f pos;
    pos.identity();
    pos.setQuat(mPoseQuat);

    MR::rotateMtxLocalXDegree(pos.toMtxPtr(), mRotation.x);
    MR::rotateMtxLocalYDegree(pos.toMtxPtr(), mRotation.y);
    MR::rotateMtxLocalZDegree(pos.toMtxPtr(), mRotation.z);

    pos.getYDir(*pOut);
}

void BossStinkBug::calcAnim() {
    LiveActor::calcAnim();

    for (s32 i = 0; i < ::sBodyCollisionNum; i++) {
        mBodyCollisions[i]->setMtx();
    }
}

void BossStinkBug::updateAction() {
    if (mActionSequencer != nullptr) {
        mActionSequencer->updateNerve();
    }
}

void BossStinkBug::updatePose() {
    if (!mIsPoseUpdated) {
        mFront.orthogonalize(mGravity);

        if (MR::isNearZero(mFront)) {
            mPoseQuat.getZDir(mFront);
        } else {
            MR::normalize(&mFront);
        }

        MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, cNum_updatePose_f1, cNum_updatePose_f2);
        mIsPoseUpdated = true;
    }
}

void BossStinkBug::setPose(MtxPtr pMtx) {
    Pos3f pos(pMtx);
    pos.getQuat(mPoseQuat);
    pos.getTrans(mPosition);
    mPoseQuat.getZDir(mFront);
    MR::normalize(&mFront);
    mIsPoseUpdated = true;
}

void BossStinkBug::updateCamera() {
    mCameraRegisterPos = mPosition;
}

bool BossStinkBug::isValidFollowId(s32 id) const {
    if (mActionSequencer != nullptr) {
        return mActionSequencer->isValidFollowId(id);
    } else {
        return false;
    }
}

void BossStinkBug::validateCollisionGround() {
    MR::invalidateCollisionParts(mBodyCollisions[0]);
    MR::validateCollisionParts(mBodyCollisions[1]);
    MR::validateCollisionParts(mBodyCollisions[2]);
}

void BossStinkBug::validateCollisionFly() {
    MR::validateCollisionParts(mBodyCollisions[0]);
    MR::invalidateCollisionParts(mBodyCollisions[1]);
    MR::validateCollisionParts(mBodyCollisions[2]);
}

void BossStinkBug::validateCollisionHitOnly() {
    MR::invalidateCollisionParts(mBodyCollisions[0]);
    MR::invalidateCollisionParts(mBodyCollisions[1]);
    MR::validateCollisionParts(mBodyCollisions[2]);
}

void BossStinkBug::invalidateCollision() {
    for (s32 i = 0; i < ::sBodyCollisionNum; i++) {
        MR::invalidateCollisionParts(mBodyCollisions[i]);
    }
}

void BossStinkBug::onBindLeg() {
    if (!mIsLegBound) {
        for (s32 i = 0; i < ::sLegNum; i++) {
            MR::copyJointPos(this, ::sLegJointNameList[i], &mLegGroundCheckers[i]->mPosition);
            mLegGroundCheckers[i]->appear();
            MR::resetPosition(mLegGroundCheckers[i], mLegGroundCheckers[i]->mPosition);
        }

        mIsLegBound = true;
    }
}

void BossStinkBug::offBindLeg() {
    if (mIsLegBound) {
        mJointCtrl->endIKCtrlAll();

        for (s32 i = 0; i < ::sLegNum; i++) {
            mLegGroundCheckers[i]->kill();
        }

        mIsLegBound = false;
    }
}

bool BossStinkBug::isLegInWater() const {
    for (s32 i = 0; i < ::sLegNum; i++) {
        if (MR::isBindedGroundWater(mLegGroundCheckers[i])) {
            return true;
        }
    }

    return false;
}

void BossStinkBug::updateJointControl() {
    ActorJointCtrl* jointCtrl = mJointCtrl;
    jointCtrl->startUpdate();

    for (s32 i = 0; i < ::sLegNum; i++) {
        Vec3f legJointPos;
        MR::copyJointPos(this, ::sLegJointNameList[i], &legJointPos);
        Vec3f groundCheckPos = mLegGroundCheckers[i]->mPosition;
        Vec3f dirGroundCheckToLegJoint = legJointPos - groundCheckPos;

        if (MR::isBinded(mLegGroundCheckers[i])) {
            mLegGroundCheckers[i]->bringFriction(mLegFriction, cNum_updateJointControl_f1);
        } else {
            mLegGroundCheckers[i]->bringFriction(1.0f, cNum_updateJointControl_f2);
        }

        dirGroundCheckToLegJoint.scale(mLegGroundCheckers[i]->mFriction);
        mLegGroundCheckers[i]->mVelocity.set(dirGroundCheckToLegJoint);
        mLegGroundCheckers[i]->movement();
        jointCtrl->setIKEndPosition(::sLegJointNameList[i], mLegGroundCheckers[i]->mPosition, mLegIKRate);

        if (MR::isBinded(mLegGroundCheckers[i])) {
            Vec3f fix = *MR::getBindedFixReactionVector(mLegGroundCheckers[i]);

            if (!MR::isNearZero(fix)) {
                MR::normalize(&fix);
                jointCtrl->setIKEndDirection(::sLegJointNameList[i], fix, cNum_updateJointControl_f3 * mLegIKRate);
            }
        } else {
            Vec3f yDir;
            MR::extractMtxYDir(MR::getJointMtx(this, ::sLegJointNameList[i]), &yDir);
            jointCtrl->setIKEndDirection(::sLegJointNameList[i], yDir, cNum_updateJointControl_f4 * mLegIKRate);
        }
    }

    jointCtrl->endUpdate();
}

void BossStinkBug::appearStarPiece(s32 num) {
    Vec3f offsetVec;
    NebulaMtxMultVec(MR::getJointMtx(this, "Body"), ::sAppearStarPieceOffset, offsetVec);
    MR::appearStarPieceToDirection(this, offsetVec, -mGravity, num, cNum_appearStarPiece_f1, cNum_appearStarPiece_f2, false);
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
}

void BossStinkBug::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mActionSequencer != nullptr) {
        mActionSequencer->attackSensor(pSender, pReceiver);
    }
}

bool BossStinkBug::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mActionSequencer != nullptr) {
        return mActionSequencer->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    } else {
        return false;
    }
}

bool BossStinkBug::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mActionSequencer != nullptr) {
        return mActionSequencer->receiveOtherMsg(msg, pSender, pReceiver);
    } else {
        return false;
    }
}

void BossStinkBug::startEventCamera(const char* pName) {
    MR::startMultiActorCameraTargetSelf(this, mCameraInfo, pName);
}

void BossStinkBug::endEventCamera(const char* pName) {
    MR::endMultiActorCamera(this, mCameraInfo, pName, false);
}

bool BossStinkBug::isSensorBody(const HitSensor* pSensor) const {
    return pSensor == getSensor("BodyHipDrop");
}

bool BossStinkBug::throwBomb(f32 speed, f32 spread) {
    BossStinkBugBomb* throwBomb = mBombHolder->getDeadMember();

    if (throwBomb == nullptr) {
        return false;
    }

    Pos3f jointMtx(MR::getJointMtx(mBombLauncher, "DischargePosition"));

    Vec3f trans;
    Vec3f yDir;

    jointMtx.getTrans(trans);
    jointMtx.getYDir(yDir);
    yDir *= -speed;
    MR::addRandomVector(&yDir, yDir, spread);

    throwBomb->start(trans, yDir);

    MR::startSound(this, "SE_BM_BOSS_BUG_BOMB_EMIT");
    MR::startBck(mBombLauncher, "Discharge");
    return true;
}
