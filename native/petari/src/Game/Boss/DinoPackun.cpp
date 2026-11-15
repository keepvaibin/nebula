#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunBall.hpp"
#include "Game/Boss/DinoPackunDemoPosition.hpp"
#include "Game/Boss/DinoPackunEggShell.hpp"
#include "Game/Boss/DinoPackunSequencer.hpp"
#include "Game/Boss/DinoPackunTail.hpp"
#include "Game/Boss/DinoPackunTailNode.hpp"
#include "Game/Boss/DinoPackunTailPart.hpp"
#include "Game/Boss/DinoPackunTailRoot.hpp"
#include "Game/Boss/DinoPackunVs1.hpp"
#include "Game/Boss/DinoPackunVs2.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    template < bool IsRight >
    inline void addDinoFootPrint(DinoPackun* pActor, const Vec3f& rStart) {
        Vec3f position;
        Vec3f normal;
        Vec3f ray = pActor->mGravity * 200.0f;
        if (MR::getFirstPolyNormalOnLineToMap(&normal, rStart, ray, &position, nullptr)) {
            pActor->mFootPrint->addPrint(position, pActor->mFront, normal, IsRight);
        }
    }

    const Vec3f sHeadHitOffset = Vec3f(140.0f, -110.0f, 0.0f);
    const Vec3f sBodyHitOffset = Vec3f(0.0f, 200.0f, 0.0f);
    const Vec3f sEggHitOffset = Vec3f(0.0f, 200.0f, 0.0f);
    const Vec3f sMarioSetLocalPos = Vec3f(0.0f, -550.0f, 800.0f);
    static const char* sEventCameraName = "ダメージ";

    // The tail is a root, sTailJointNum parts and the ball.
    const u32 sTailJointNum = 7;
    const char* const sJointName[sTailJointNum] = {"Tail1", "Tail2", "Tail3", "Tail4", "Tail5", "Tail6", "Tail7"};
    const f32 sKeepBendPower[sTailJointNum] = {30.0f, 25.0f, 20.0f, 15.0f, 10.0f, 10.0f, 10.0f};
    const f32 sJointRadius[sTailJointNum] = {80.0f, 70.0f, 60.0f, 55.0f, 50.0f, 45.0f, 50.0f};

    class DinoPackunParam : public AnimScaleParam {
    public:
        DinoPackunParam() {
            mAnimSpring = 0.1f;
            mPointerTime = 30.0f;
            mPointerBase = 0.60f;
            mPointerAmplitude = 0.06f;
            mPointerFreq = 3.0f;
            mHitReactionVelocity = 0.07f;
            mHitReactionSpring = 0.12f;
            mHitReactionDamping = 0.92f;
        }
    };

    static DinoPackunParam sParam = DinoPackunParam();

};  // namespace

DinoPackun::DinoPackun(const char* pName)
    : LiveActor(pName), mTail(), mBall(), mFootPrint(), mShell(), mDemoPos(), mShellBreakModel(), mTailBall(), mCamTargetMtx(), mCameraInfo(),
      mSequence(), mScaleJointCtrl(), mAnimScaleCtrl(), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mFront(0.0f, 0.0f, 1.0f), mCameraRegisterId(-1),
      mCameraVec(0.0f, 0.0f, 0.0f), mAimTailRateSpeed(0.0f), mAimTailRate(0.0f), mDamageCameraState(-1), mIsPreCalcAnim(false) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackun >);
}

// Vs1 and Vs2 share the models and pick their look by animation name.
const char* DinoPackun::getVsAnimName() const {
    return mSequence->getVsCount() == 1 ? "Vs1" : "Vs2";
}

void DinoPackun::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm(mSequence->getVsCount() == 1 ? "DinoPackun" : "DinoPackun2", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mFront, this);
    mStartQuat.set(mPoseQuat);
    mStartPos.set(mPosition);
    initSound(8, false);
    MR::onCalcGravity(this);
    initHitSensor(3);
    MR::addHitSensorAtJointEnemy(this, "head", "Head", 8, 270.0f, ::sHeadHitOffset);
    MR::addHitSensorEnemy(this, "body", 8, 200.f, ::sBodyHitOffset);

    if (mSequence->isUseEggShell()) {
        MR::addHitSensorEnemy(this, "egg", 8, 400.0f, ::sEggHitOffset);
    }

    initBinder(150.0f, 150.0f, 0);
    initEffectKeeper(0, nullptr);
    initCamera(rIter);
    initTail();
    MR::initShadowFromCSV(this, "Shadow");
    initFootPrint();

    if (mSequence->isUseEggShell()) {
        initEggShell();
    }

    initBall();
    initScaleJointController();
    initDemoPosition(rIter);
    MR::declarePowerStar(this);
    MR::needStageSwitchReadA(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::startBck(this, "OpeningDemo");
    MR::stopBck(this);
    MR::startBtp(this, "FlowerAll");
    MR::startBrk(this, "Normal");
    MR::listenStageSwitchOnA(this, MR::Functor(this, &DinoPackun::startSequence));
    MR::addBaseMatrixFollowTarget(this, rIter, nullptr, nullptr);

    if (mSequence != nullptr) {
        mSequence->init();
    }

    makeActorAppeared();
    mTail->deactivate();
}

void DinoPackun::initTail() {
    mTail = new DinoPackunTail(sTailJointNum + 2);
    DinoPackunTailRoot* root = new DinoPackunTailRoot("尻尾ルート", this);
    root->createJointController(this, "TailRoot");
    root->initWithoutIter();
    root->mKeepBendPower = ::sKeepBendPower[0];
    mTail->addTailNode(root);

    for (u32 i = 0; i < sTailJointNum; i++) {
        DinoPackunTailPart* pPart = new DinoPackunTailPart("尻尾ジョイント", this);
        MR::copyJointPos(this, ::sJointName[i], &pPart->mPosition);
        pPart->createJointController(this, ::sJointName[i]);
        pPart->mKeepBendPower = ::sKeepBendPower[i];
        pPart->mRadius = ::sJointRadius[i];
        pPart->initWithoutIter();
        mTail->addTailNode(pPart);
    }

    mBall = new DinoPackunBall("尻尾先端球", this);
    MR::copyJointPos(this, "TailBall", &mBall->mPosition);
    mBall->mLinkLength = 120.0f;
    mBall->setWeakSensor(getSensor("head"));
    mBall->createJointController(this, "TailBall");
    mBall->initWithoutIter();
    mTail->addTailNode(mBall);
}

void DinoPackun::initFootPrint() {
    mFootPrint = new FootPrint("足跡", 32);
    mFootPrint->setTexture(MR::getTexFromArc("DinoPackunFootprint.bti", this));
    mFootPrint->mSideOffset = 0.0f;
    mFootPrint->mHeight = 100.0f;
    mFootPrint->mWidth = 100.0f;
    mFootPrint->mMinPrintDistance = 10.0f;
}

void DinoPackun::initDemoPosition(const TableIter& rIter) {
    mDemoPos = new DinoPackunDemoPosition("デモ位置管理");
    mDemoPos->init(rIter);
}

void DinoPackun::initEggShell() {
    mShell = new DinoPackunEggShell("卵の殻", getSensor("head"), MR::getJointMtx(this, "EggShell"));
    mShell->initWithoutIter();
    MR::startBtp(mShell, getVsAnimName());
    MR::startBrk(mShell, getVsAnimName());

    mShellBreakModel = MR::createPartsModelEnemy(this, "卵の殻壊れモデル", "DinoPackunEggShellBreak", MR::getJointMtx(this, "EggShell"));
    MR::initLightCtrl(mShellBreakModel);
    mShellBreakModel->makeActorDead();
    MR::startBtp(mShellBreakModel, getVsAnimName());
}

void DinoPackun::initBall() {
    mTailBall = MR::createPartsModelEnemy(this, "尻尾玉モデル", "DinoPackunTailBall", MR::getJointMtx(this, "TailBall"));
    MR::startBrk(mTailBall, "Normal");
    MR::startBtp(mTailBall, getVsAnimName());
    mTailBall->makeActorAppeared();
}

void DinoPackun::initCamera(const TableIter& rIter) {
    mCameraInfo = new ActorCameraInfo(rIter);
    MR::initAnimCamera(this, mCameraInfo, "OpeningDemo");
    MR::initAnimCamera(this, mCameraInfo, "CryDemo");
    MR::initAnimCamera(this, mCameraInfo, "AngryDemo");
    MR::initAnimCamera(this, mCameraInfo, "DownDemo");
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, ::sEventCameraName);
    MR::getPlacementArg7WithInit(rIter, &mCameraRegisterId);

    if (mCameraRegisterId != -1) {
        MR::declareCameraRegisterVec(this, mCameraRegisterId, &mCameraVec);
    }

    mCamTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
}

void DinoPackun::initScaleJointController() {
    mAnimScaleCtrl = new AnimScaleController(&::sParam);
    mScaleJointCtrl = MR::createJointDelegatorWithNullChildFunc(this, &DinoPackun::hitScaleJoint, "Spine1");
}

void DinoPackun::makeActorDead() {
    LiveActor::makeActorDead();
    mTail->deactivate();
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void DinoPackun::startHitReaction() {
    MR::startSound(this, "SE_OJ_STAR_PIECE_HIT_STOP");
    if (mAnimScaleCtrl != nullptr) {
        mAnimScaleCtrl->startHitReaction();
    }
}

bool DinoPackun::isHitReaction(s32 step) const {
    return mAnimScaleCtrl != nullptr && mAnimScaleCtrl->isHitReaction(step);
}

// The scale joint's X, Y and Z run along the actor's Y, Z and X.
bool DinoPackun::hitScaleJoint(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    const Vec3f& rScale = mAnimScaleCtrl->mScale;
    Vec3f jointScale(rScale.y, rScale.z, rScale.x);
    MR::preScaleMtx(*pMtx, jointScale);
    return true;
}

DinoPackunEggShell* DinoPackun::getEggShell() {
    return mShell;
}

PartsModel* DinoPackun::getEggBrokenModel() {
    return mShellBreakModel;
}

PartsModel* DinoPackun::getBallModel() {
    return mTailBall;
}

void DinoPackun::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mSequence != nullptr) {
        mSequence->attackSensor(pSender, pReceiver);
    }
}

bool DinoPackun::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mSequence != nullptr) {
        return mSequence->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }

    return false;
}

bool DinoPackun::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (mSequence != nullptr) {
        return mSequence->receiveMsgPush(pSender, pReceiver);
    }

    return false;
}

bool DinoPackun::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mSequence != nullptr) {
        return mSequence->receiveOtherMsg(msg, pSender, pReceiver);
    }

    return false;
}

void DinoPackun::attackSensorTail(HitSensor* pSender, HitSensor* pReceiver) {
    if (mSequence != nullptr) {
        mSequence->attackSensorTail(pSender, pReceiver);
    }
}

bool DinoPackun::receiveMsgPlayerAttackTail(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mSequence != nullptr) {
        return mSequence->receiveMsgPlayerAttackTail(msg, pSender, pReceiver);
    }

    return false;
}

void DinoPackun::startSequence() {
    if (mSequence != nullptr) {
        MR::invalidateClipping(this);
        mTail->activate();
        mSequence->start();
    }
}

void DinoPackun::control() {
    if (mSequence != nullptr) {
        mSequence->update();
    }

    if (mAnimScaleCtrl != nullptr) {
        mAnimScaleCtrl->updateNerve();
    }

    // The first animation pass only records the tail joints' animated positions.
    updatePose();
    mIsPreCalcAnim = true;
    calcAnim();
    mIsPreCalcAnim = false;
    mTail->updateJoint();
    updateCameraInfo();
}

void DinoPackun::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    MR::setBaseScale(this, mScale);

    if (mIsPreCalcAnim) {
        mTail->registerPreCalcJointCallBack();
    } else {
        mTail->registerJointCallBack();
    }

    if (mScaleJointCtrl != nullptr) {
        mScaleJointCtrl->registerCallBack();
    }
}

void DinoPackun::updatePose() {
    mFront.orthogonalize(mGravity);
    if (MR::isNearZero(mFront)) {
        mPoseQuat.getZDir(mFront);
    } else {
        MR::normalize(&mFront);
    }

    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, 0.1f, 0.2f);
}

// One foot lands at the start of each stride of period steps, the other half way.
void DinoPackun::updateFootPrintNerve(s32 step, s32 period) {
    Vec3f side;
    mPoseQuat.getXDir(side);
    s32 strideStep = step % period;
    if (strideStep == 0) {
        addDinoFootPrint< false >(this, mPosition + side * -100.0f + mGravity * -100.0f);
    } else if (strideStep == period / 2) {
        addDinoFootPrint< true >(this, mPosition + side * 100.0f + mGravity * -100.0f);
    }
}

// The camera looks 300 above the body, drawn up to 0.6 of the way to the tail ball while aiming at it.
void DinoPackun::updateCameraInfo() {
    if (mCameraRegisterId == -1) {
        return;
    }

    mAimTailRate = MR::clamp01(mAimTailRate + mAimTailRateSpeed);
    Vec3f up;
    mPoseQuat.getYDir(up);
    Vec3f target;
    target.scaleAdd(300.0f, up, mPosition);

    if (mAimTailRate > 0.0f) {
        Vec3f tailEnd;
        mTail->getEndTailPosition(&tailEnd);
        MR::vecBlend(target, tailEnd, &target, (0.60f * mAimTailRate));
    }

    mCameraVec.set(target);
    mCamTargetMtx->mMatrix.setTrans(target);
}

void DinoPackun::updateNormalVelocity() {
    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    if (MR::isBindedWall(this)) {
        MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    }

    MR::attenuateVelocity(this, 0.9f);
}

void DinoPackun::updateRunVelocity() {
    updateNormalVelocity();
}

void DinoPackun::appearStarPiece(s32 num) {
    Vec3f pos;
    MR::copyJointPos(this, "Head", &pos);
    MR::appearStarPieceToDirection(this, pos, -mGravity, num, 50.0f, 60.0f, false);
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
}

bool DinoPackun::isSensorEgg(const HitSensor* pSensor) const {
    return getSensor("egg") == pSensor;
}

void DinoPackun::resetPosition() {
    mPoseQuat.set(mStartQuat);
    mPoseQuat.getZDir(mFront);
    MR::resetPosition(this, mStartPos);
    MR::zeroVelocity(this);

    // The player is put back at sMarioSetLocalPos, facing the boss.
    Pos3f mtx;
    mtx.setQT(mPoseQuat, mPosition);
    Vec3f playerPos;
    mtx.mult(::sMarioSetLocalPos, playerPos);
    Vec3f gravity;
    MR::calcGravityVector(this, playerPos, &gravity, nullptr, 0);

    Vec3f playerFront = mPosition - playerPos;
    MR::makeMtxUpFrontPos(&mtx, -gravity, playerFront, playerPos);
    MR::setPlayerBaseMtx(mtx);
}

// Moves the first tail node ratio of the way to rLocalPos in the body's frame.
void DinoPackun::adjustTailRootPosition(const Vec3f& rLocalPos, f32 ratio) {
    Vec3f targetPos;
    mPoseQuat.transform(rLocalPos, targetPos);
    targetPos.add(mPosition);
    DinoPackunTailNode* pNode = mTail->getNode(1);
    Vec3f currPos = pNode->mPosition;
    Vec3f pos = targetPos * ratio + currPos * (1.0f - ratio);
    pNode->mPosition.set(pos);
}

void DinoPackun::activateParts() {
    mTail->activate();
}

void DinoPackun::onMovementParts() {
    mTail->onMovement();
    MR::requestMovementOn(mDemoPos);
    MR::requestMovementOn(mFootPrint);
    if (mShell != nullptr) {
        MR::requestMovementOn(mShell);
    }

    if (mShellBreakModel != nullptr) {
        MR::requestMovementOn(mShellBreakModel);
    }

    if (mTailBall != nullptr) {
        MR::requestMovementOn(mTailBall);
    }
}

// Turns the camera to the tail ball over steps (at once for 0).
void DinoPackun::onAimTailBall(s32 steps) {
    if (steps <= 0) {
        mAimTailRateSpeed = 0.0f;
        mAimTailRate = 1.0f;
    } else {
        mAimTailRateSpeed = 1.0f / steps;
    }
}

void DinoPackun::offAimTailBall(s32 steps) {
    if (steps <= 0) {
        mAimTailRateSpeed = 0.0f;
        mAimTailRate = 0.0f;
    } else {
        mAimTailRateSpeed = -1.0f / steps;
    }
}

void DinoPackun::startDemo() {
    onMovementParts();
    MR::turnOffImageEffect();
}

void DinoPackun::startDemoAndReset() {
    mTail->deactivate();
    resetPosition();
    mFootPrint->clear();
    onMovementParts();
    MR::turnOffImageEffect();
}

void DinoPackun::endDemo(const char* pName) {
    MR::endDemo(this, pName);
    MR::setImageEffectControlAuto();
}

void DinoPackun::startDamageCamera() {
    CameraTargetArg arg(nullptr, mCamTargetMtx, nullptr, nullptr);
    MR::startMultiActorCameraTargetOther(this, mCameraInfo, ::sEventCameraName, arg);
    mDamageCameraState = 0;
}

void DinoPackun::endDamageCamera() {
    if (mDamageCameraState != -1) {
        MR::endMultiActorCamera(this, mCameraInfo, ::sEventCameraName, false);
        mDamageCameraState = -1;
    }
}

namespace MR {
    NameObj* createDinoPackunVs1(const char* pName) {
        DinoPackun* p = new DinoPackun(pName);
        p->mSequence = new DinoPackunVs1(p);
        return p;
    }

    NameObj* createDinoPackunVs2(const char* pName) {
        DinoPackun* p = new DinoPackun(pName);
        p->mSequence = new DinoPackunVs2(p);
        return p;
    }
};  // namespace MR
