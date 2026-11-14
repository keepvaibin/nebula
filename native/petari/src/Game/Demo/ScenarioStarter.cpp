#include "Game/Demo/ScenarioStarter.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpinDriverCamera.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Screen/ScenarioTitle.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"
#include <nebula_module.h>

namespace NrvScenarioStarter {
    NEW_NERVE(ScenarioStarterWaitScenarioCameraEnd, ScenarioStarter, WaitScenarioCameraEnd);
    NEW_NERVE(ScenarioStarterWaitToStart, ScenarioStarter, WaitToStart);
    NEW_NERVE(ScenarioStarterCinemaFrameBlank, ScenarioStarter, CinemaFrameBlank);
    NEW_NERVE(ScenarioStarterRailMove, ScenarioStarter, RailMove);
    NEW_NERVE(ScenarioStarterRailMoveCanceled, ScenarioStarter, RailMoveCanceled);
    NEW_NERVE(ScenarioStarterShowWelcomeLayout, ScenarioStarter, ShowWelcomeLayout);
};  // namespace NrvScenarioStarter

ScenarioStarter::ScenarioStarter(const char* pName)
    : LiveActor(pName), mBindActor(), mHeadDir(0.0f, 1.0f, 0.0f), mJumpVec(0.0f, 0.0f, 0.0f), mShootPath(), mSpinDriverCamera(), mInitUp(0.0f, 1.0f, 0.0f), mInitFront(0.0f, 0.0f, 1.0f),
      mSpinAngle(), mSpinAngleMax(), mFallAngle(), mFlightTime(300), mFlyMotionStartStep(), mFlyEndStep(), mFallMotionStartStep(), mInitalPlayerRotation(),
      mWelcomeLayout(), mTitle(), mStartPos(0.0f, 0.0f, 0.0f), mStartRotate(0.0f, 0.0f, 0.0f), mIsSkipped() {
    lumacore::setClass(this, lumacore::kClassId< ScenarioStarter >);
    mBindQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void ScenarioStarter::init(const TableIter& rIter) {
    // The opening flight follows the starter's rail. A starter whose rail the stage does not have is not made: it is
    // renamed so the scene does not find it (MR::isBeginScenarioStarter) and joins no list, and the stage starts with
    // the player at the start position, as a stage with no starter does.
    const Table* pPoints = nullptr;
    TableIter rail;
    if (MR::isConnectedWithRail(rIter)) {
        MR::getRailInfo(&rail, &pPoints, rIter);
    }
    if (pPoints == nullptr || pPoints->getNumEntries() < 2) {
        nebula_log("ScenarioStarter: its rail is not in the stage (CommonPath_ID names no rail with two points); not made, "
                   "the stage starts without its opening flight\n");
        setName("ScenarioStarter (no rail)");
        return;
    }
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneMapObjMovement(this);
    MR::getPlacementArg0NoInit(rIter, &mFlightTime);
    initShootPath(rIter);

    mSpinDriverCamera = new SpinDriverCamera();
    mSpinDriverCamera->init(rIter, this);

    initHitSensor(1);
    MR::addHitSensor(this, "body", ACTMES_GROUP_LOD_LOW, 8, 200.0f, Vec3f(0.0f));
    MR::invalidateClipping(this);
    mWelcomeLayout = MR::createSimpleLayout("Welcome表示", "Welcome", 1);
    mWelcomeLayout->kill();
    MR::registerDemoSimpleCastAll(mWelcomeLayout);
    mTitle = new ScenarioTitle();
    mTitle->initWithoutIter();
    mTitle->kill();
    mStartPos.set(*MR::getPlayerPos());
    mStartRotate.set(*MR::getPlayerRotate());
    mPosition.set(mStartPos);
    initNerve(GET_NERVE(ScenarioStarter, ScenarioStarterWaitScenarioCameraEnd));
    if (MR::isBeginScenarioStarter()) {
        appear();
    } else {
        kill();
    }
}

void ScenarioStarter::exeWaitToStart() {
    if (MR::isFirstStep(this)) {
        mIsSkipped = false;
        MR::forceToBlankCinemaFrame();
    }
}

void ScenarioStarter::exeCinemaFrameBlank() {
    if (MR::isFirstStep(this) && !isStartBgmOnWelcome()) {
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
    }

    MR::setNerveAtStep(this, GET_NERVE(ScenarioStarter, ScenarioStarterRailMove), 0);
}

void ScenarioStarter::exeRailMove() {
    if (MR::isFirstStep(this)) {
        calcShootMotionTime();
        MR::startBck(mBindActor, "SpaceFlyStart");
        mHeadDir = mInitUp;
        turnBindHead(mHeadDir, 1.0f);
        mPosition.set(MR::getRailPointPosStart(this));
        MR::endStartPosCamera();
        mSpinDriverCamera->start(mInitUp, mInitFront, mPosition);
        Pos3f rotMtx;
        mFallAngle = 0.0f;
        MR::makeMtxUpFront(&rotMtx, mInitUp, mInitFront);
        rotMtx.getQuat(mBindQuat);
    }

    if (MR::isStep(this, 10)) {
        MR::tryBlankToFrameCinemaFrame();
        mTitle->start();
    }

    updateBindPosition();
    if (!MR::isNearZero(mHeadDir)) {
        turnBindHead(mHeadDir, 0.4f);
    }

    updateShootMotion();
    mSpinDriverCamera->update(mHeadDir, mPosition);
    if (mFlyMotionStartStep <= getNerveStep() && getNerveStep() <= mFlyEndStep) {
        mSpinAngle = mSpinAngleMax * MR::getEaseOutValue(MR::normalize(getNerveStep(), mFlyMotionStartStep, mFlyEndStep), 0.0f, 1.0f, 1.0f);
    }

    mFallAngle = MR::pi() * MR::normalize(getNerveStep(), mFlyEndStep, mFallMotionStartStep);
    if (trySkipTrigger()) {
        MR::tryFrameToBlankCinemaFrame();
        mIsSkipped = true;
    }

    if (mIsSkipped && MR::isStopCinemaFrame()) {
        setNerve(GET_NERVE(ScenarioStarter, ScenarioStarterRailMoveCanceled));
        return;
    }

    s32 bgmStartTime = 0;
    if (MR::isEqualStageName("EggStarGalaxy")) {
        bgmStartTime = 20;
    } else if (MR::isEqualStageName("HoneyBeeKingdomGalaxy")) {
        bgmStartTime = 7;
    } else if (MR::isEqualStageName("FactoryGalaxy")) {
        bgmStartTime = 11;
    }

    if (MR::isStep(this, mFlightTime - bgmStartTime) && isStartBgmOnWelcome()) {
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
    }

    if (MR::isGreaterEqualStep(this, mFlightTime)) {
        MR::endBindAndSpinDriverJump(this, mJumpVec);
        mBindActor = nullptr;
        mSpinDriverCamera->end();
        setNerve(GET_NERVE(ScenarioStarter, ScenarioStarterShowWelcomeLayout));
    }
}

void ScenarioStarter::exeRailMoveCanceled() {
    if (MR::isFirstStep(this)) {
        MR::endBindAndPlayerWait(this);
        mBindActor = nullptr;
        mSpinDriverCamera->cancel();
        mTitle->kill();
        if (isStartBgmOnWelcome()) {
            MR::stopStageBGM(0);
            MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        }

        setNerve(GET_NERVE(ScenarioStarter, ScenarioStarterShowWelcomeLayout));
    }
}

void ScenarioStarter::exeShowWelcomeLayout() {
    if (MR::isFirstStep(this)) {
        GameSceneFunction::notifyEndScenarioStarter();
        MR::tryFrameToScreenCinemaFrame();
        MR::startStartPosCamera(!mIsSkipped);
        mWelcomeLayout->appear();
        if (!MR::isDead(mTitle)) {
            mTitle->end();
        }

        MR::startAnim(mWelcomeLayout, "Appear", 0);
        MR::executeOnWelcomeAndRetry();
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::forceAppearDefaultGameLayout();
    }

    if (MR::isAnimStopped(mWelcomeLayout, 0)) {
        mWelcomeLayout->kill();
        kill();
    }
}

void ScenarioStarter::initShootPath(const TableIter& rIter) {
    initRailRider(rIter);
    mInitalPlayerRotation = 0.0f;
    MR::getPlacementArg5NoInit(rIter, &mInitalPlayerRotation);
    mShootPath = new SpinDriverShootPath();
    mShootPath->init(rIter);
    Vec3f side(1.0f, 0.0f, 0.0f);
    mShootPath->calcInitPose(&mInitUp, &mInitFront, &side, mInitalPlayerRotation);
}

void ScenarioStarter::updateBindPosition() {
    Vec3f pathPos;
    Vec3f pathDir;
    f32 nerveRate = MR::calcNerveRate(this, mFlightTime);
    mShootPath->calcPosition(&pathPos, nerveRate);
    mShootPath->calcDirection(&pathDir, nerveRate, 0.01f);
    if (!MR::isNearZero(pathDir)) {
        mHeadDir = pathDir;
    }

    mJumpVec.set(pathPos);
    mJumpVec -= mPosition;
    mPosition.set(pathPos);
}

void ScenarioStarter::updateBindActorMtx() {
    if (isNerve(GET_NERVE(ScenarioStarter, ScenarioStarterRailMove))) {
        Pos3f rotateY;
        rotateY.identity();
        rotateY.setEulerY(mSpinAngle);
        rotateY.zeroTrans();

        Pos3f rotateX;
        rotateX.identity();
        rotateX.setEulerX(mFallAngle);

        Pos3f mtx;
        mtx.setQT(mBindQuat, mPosition);
        mtx.concat(mtx, rotateX);
        mtx.concat(mtx, rotateY);
        MR::setBaseTRMtx(mBindActor, mtx);
    } else {
        Pos3f mtx;
        MR::makeMtxTR(mtx, mStartPos, mStartRotate);
        MR::setBaseTRMtx(mBindActor, mtx);
    }
}

void ScenarioStarter::updateShootMotion() {
    if (MR::isStep(this, mFlyMotionStartStep)) {
        MR::startBckWithInterpole(mBindActor, "SpaceFlyLoop", 5);
    }

    if (MR::isLessStep(this, mFlyEndStep)) {
        MR::startLevelSound(mBindActor, "SE_PM_LV_S_SPIN_DRV_FLY");
    }

    if (MR::isStep(this, mFlyEndStep)) {
        MR::startBckWithInterpole(mBindActor, "SpaceFlyEnd", 0);
        MR::startSound(mBindActor, "SE_PM_S_SPIN_DRV_COOL_DOWN");
        MR::startSound(mBindActor, "SE_PV_JUMP_S");
    }

    if (MR::isStep(this, mFallMotionStartStep)) {
        MR::startBckWithInterpole(mBindActor, "Fall", 20);
    }
}

void ScenarioStarter::turnBindHead(const Vec3f& rHead, f32 rate) {
    MR::turnQuatYDirRad(&mBindQuat, mBindQuat, mHeadDir, PI);
    Vec3f head;
    mBindQuat.getYDir(head);
    Quat4f rotation;
    rotation.setRotate(head, rHead, rate);
    mBindQuat.mult(rotation);
    mBindQuat.normalize();
}

void ScenarioStarter::calcShootMotionTime() {
    s32 startTime = MR::getBckFrameMax(mBindActor, "SpaceFlyStart");
    s32 endTime = MR::getBckFrameMax(mBindActor, "SpaceFlyEnd");

    if (mFlightTime < 20) {
        mFlyMotionStartStep = -1;
        mFlyEndStep = -1;
        mFallMotionStartStep = 0;
        return;
    }

    if (mFlightTime < endTime + 20) {
        mFlyMotionStartStep = -1;
        mFlyEndStep = 0;
        mFallMotionStartStep = -1;
        return;
    }

    mFlyEndStep = mFlightTime - (endTime + 20);
    mFallMotionStartStep = mFlightTime - 20;
    s32 flyTime = 0.2f * mFlightTime;
    if (flyTime > 90) {
        flyTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    }

    mFlyMotionStartStep = startTime != 0 ? startTime * (flyTime / startTime) : 0;
    if (mFlyEndStep <= mFlyMotionStartStep) {
        mFlyMotionStartStep = 0;
    }

    f32 duration = 0.05f * (mFlyEndStep - mFlyMotionStartStep);
    mSpinAngle = 0.0f;
    mSpinAngleMax = static_cast< s32 >(duration / TWO_PI) * TWO_PI;
}

bool ScenarioStarter::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (mBindActor == nullptr && MR::isMsgAutoRushBegin(msg) && isNerve(GET_NERVE(ScenarioStarter, ScenarioStarterWaitToStart))) {
        mBindActor = MR::getSensorHost(pSender);
        setNerve(GET_NERVE(ScenarioStarter, ScenarioStarterCinemaFrameBlank));
        return true;
    }

    if (mBindActor != nullptr && MR::isMsgUpdateBaseMtx(msg)) {
        updateBindActorMtx();
        return true;
    }

    return false;
}

bool ScenarioStarter::trySkipTrigger() const {
    return MR::isAlreadyVisitedCurrentStageAndScenario() && MR::isGreaterStep(this, 10) && MR::isLessStep(this, mFlyEndStep) &&
           MR::testSystemPadTriggerDecide();
}

bool ScenarioStarter::isStartBgmOnWelcome() {
    if (MR::isGalaxyRedCometAppearInCurrentStage() || MR::isGalaxyBlackCometAppearInCurrentStage()) {
        return false;
    }

    if (MR::isEqualStageName("EggStarGalaxy") && MR::getCurrentScenarioNo() == 1) {
        return true;
    }

    if (MR::isEqualStageName("HoneyBeeKingdomGalaxy") && MR::getCurrentScenarioNo() == 1) {
        return true;
    }

    if (MR::isEqualStageName("FactoryGalaxy") && MR::getCurrentScenarioNo() <= 3) {
        return true;
    }

    return false;
}

void ScenarioStarter::exeWaitScenarioCameraEnd() {
    if (!MR::isStageStateScenarioOpeningCamera()) {
        setNerve(GET_NERVE(ScenarioStarter, ScenarioStarterWaitToStart));
    }
}
