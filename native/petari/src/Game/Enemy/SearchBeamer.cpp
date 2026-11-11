#include "Game/Enemy/SearchBeamer.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBeamShadow_f1, "updateBeamShadow.f1", 2.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeCloseSearch_1, "exeCloseSearch.1", 50);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBeamPrepare_1, "exeBeamPrepare.1", 10);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBeamStart_1, "exeBeamStart.1", 15);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBeamStart_2, "exeBeamStart.2", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_exeCloseMouth_1, "exeCloseMouth.1", 5);
NEBULA_TUNABLE_NAMED(s32, cStep_exeCloseMouth_2, "exeCloseMouth.2", 5);
NEBULA_TUNABLE_NAMED(s32, cStep_exeStopForPlayerOff_1, "exeStopForPlayerOff.1", 5);

namespace NrvSearchBeamer {
    NEW_NERVE_ONEND(SearchBeamerNrvNonActive, SearchBeamer, NonActive, NonActive);
    NEW_NERVE(SearchBeamerNrvCloseWaitFar, SearchBeamer, CloseWaitFar);
    NEW_NERVE(SearchBeamerNrvCloseSearch, SearchBeamer, CloseSearch);
    NEW_NERVE(SearchBeamerNrvCloseWaitNear, SearchBeamer, CloseWaitNear);
    NEW_NERVE(SearchBeamerNrvOpenMouth, SearchBeamer, OpenMouth);
    NEW_NERVE(SearchBeamerNrvBeamPrepare, SearchBeamer, BeamPrepare);
    NEW_NERVE(SearchBeamerNrvBeamStart, SearchBeamer, BeamStart);
    NEW_NERVE(SearchBeamerNrvBeamAim, SearchBeamer, BeamAim);
    NEW_NERVE(SearchBeamerNrvCloseMouth, SearchBeamer, CloseMouth);
    NEW_NERVE(SearchBeamerNrvStopStart, SearchBeamer, StopStart);
    NEW_NERVE(SearchBeamerNrvStop, SearchBeamer, Stop);
    NEW_NERVE(SearchBeamerNrvStopForPlayerOff, SearchBeamer, StopForPlayerOff);
    NEW_NERVE(SearchBeamerNrvRecover, SearchBeamer, Recover);
    NEW_NERVE_ONEND(SearchBeamerNrvPointerSwoon, SearchBeamer, PointerSwoon, PointerSwoon);
};  // namespace NrvSearchBeamer

namespace {
    NEBULA_TUNABLE(f32, cActiveDistance, 4000.0f);
    NEBULA_TUNABLE(f32, cNearDistance, 2500.0f);
    NEBULA_TUNABLE(f32, cFarDistance, 3000.0f);
    NEBULA_TUNABLE(f32, cTurnDegree, 3.0f);
    NEBULA_TUNABLE(f32, cStopTurnDegree, 15.0f);
    NEBULA_TUNABLE(f32, cPitchLimitDegree, 35.0f);
    NEBULA_TUNABLE(f32, cTerritoryFaceDegree, 45.0f);
    NEBULA_TUNABLE(f32, cBeamOverTerritory, 300.0f);
    NEBULA_TUNABLE(f32, cBeamWidth, 15.0f);
    NEBULA_TUNABLE(f32, cPropellerAccel, 0.30f);
    NEBULA_TUNABLE(f32, cPropellerMinSpeed, 10.0f);
    NEBULA_TUNABLE(f32, cPropellerMaxSpeed, 40.0f);
    NEBULA_TUNABLE(f32, cAimBelowCenter, 20.0f);

    // A little below the player's center.
    inline void calcPlayerAimPos(Vec3f* pPos) {
        Vec3f up;
        MR::getPlayerUpVec(&up);
        pPos->scaleAdd(-cAimBelowCenter, up, *MR::getPlayerCenterPos());
    }
};  // namespace

SearchBeamer::SearchBeamer(const char* pName)
    : LiveActor(pName), mScaleController(nullptr), mBindStarPointer(nullptr), mFront(0.0f, 0.0f, 1.0f), mUp(0.0f, 0.0f, 0.0f), mPropellerAngle(0.0f),
      mPropellerSpeed(0.0f), mPropellerJointCtrl(nullptr), mBeamStartJointCtrl(nullptr), mBeamEndJointCtrl(nullptr), mBeamStart(0.0f, 0.0f, 0.0f),
      mBeamEnd(0.0f, 0.0f, 0.0f), mBeamAimSpeed(8.0f), mTerritoryRadius(1000.0f), mBeamLength(0.0f) {
    lumacore::setClass(this, lumacore::kClassId< SearchBeamer >);
    mCollisionMtx.identity();
}

void SearchBeamer::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("SearchBeamer", nullptr, false);
    MR::connectToSceneCollisionMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f1, Vec3f(0.0f, -cNum_init_f2, 0.0f));
    MR::addHitSensorCallbackEnemyAttack(this, "beam", 8, cBeamWidth);
    MR::invalidateHitSensor(this, "beam");
    mCollisionMtx.setInline(getBaseMtx());
    MR::initCollisionParts(this, "Face", getSensor("body"), mCollisionMtx);
    initEffectKeeper(1, nullptr);
    initSound(4, false);
    MR::initStarPointerTarget(this, cNum_init_f3, Vec3f(0, 0, 0));
    mScaleController = new AnimScaleController(nullptr);
    mScaleController->setParamTight();
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);

    MR::initShadowController(this, 2);
    MR::addShadowVolumeCylinder(this, "body", cNum_init_f4);
    MR::addShadowVolumeBox(this, "beam", Vec3f(cNum_init_f5, cNum_init_f6, cNum_init_f7), MR::getJointMtx(this, "BeamStart"));
    MR::setShadowVolumeStartDropOffset(this, "body", cNum_init_f8);
    MR::invalidateShadow(this, "beam");
    MR::setShadowDropLength(this, "body", cNum_init_f9);
    MR::setShadowDropLength(this, "beam", cNum_init_f10);
    MR::useStageSwitchSleep(this, rIter);
    MR::useStageSwitchReadA(this, rIter);

    mPropellerJointCtrl = MR::createJointController(this, "Propeller", &SearchBeamer::calcJointPropeller);
    mPropellerSpeed = cPropellerMaxSpeed;
    mBeamStartJointCtrl = MR::createJointController(this, "BeamStart", &SearchBeamer::calcJointBeamStart);
    mBeamEndJointCtrl = MR::createJointController(this, "BeamEnd", &SearchBeamer::calcJointBeamEnd);
    initBeamPos();
    MR::getPlacementArg0NoInit(rIter, &mBeamAimSpeed);
    MR::getPlacementArg1NoInit(rIter, &mTerritoryRadius);
    mBeamLength = cBeamOverTerritory + mTerritoryRadius;
    MR::calcFrontVec(&mFront, this);
    MR::calcUpVec(&mUp, this);
    mGravity.negate(mUp);
    initNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitFar));
    makeActorAppeared();
}

void SearchBeamer::exeNonActive() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::invalidateCollisionParts(this);
        MR::offCalcAnim(this);
    }

    if (MR::isNearPlayerAnyTime(this, cActiveDistance)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitFar));
    }
}

void SearchBeamer::exeCloseWaitFar() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
        MR::startBrk(this, "Wait");
    }

    reformDirection(false);

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseSearch));
    } else if (MR::isNearPlayer(this, cNearDistance)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
    } else {
        tryNonActive();
    }
}

void SearchBeamer::exeCloseSearch() {
    reformDirection(false);
    MR::rotateVecDegree(&mFront, mUp, cTurnDegree);

    if (MR::isStep(this, cStep_exeCloseSearch_1)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitFar));
    }
}

void SearchBeamer::exeCloseWaitNear() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
        MR::startBrk(this, "Wait");
    }

    reformDirection(false);
    MR::turnDirectionToPlayerDegree(this, &mFront, cTurnDegree);

    if (!MR::isNearPlayer(this, cFarDistance)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitFar));
    } else if (isPlayerInTerritory()) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvOpenMouth));
    }
}

void SearchBeamer::exeOpenMouth() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Open");
        MR::startSound(this, "SE_EM_SEARCHBEAMER_PRE_BEAM");
    }

    bowToPlayer();

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvBeamPrepare));
    }
}

void SearchBeamer::exeBeamPrepare() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
    }

    if (MR::isStep(this, cStep_exeBeamPrepare_1)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvBeamStart));
    }
}

void SearchBeamer::exeBeamStart() {
    if (MR::isFirstStep(this)) {
        MR::startBtk(this, "SearchBeamer");
        MR::validateHitSensor(this, "beam");
        MR::validateShadow(this, "beam");
        MR::invalidateClipping(this);
        initBeamPos();
        MR::showMaterial(this, "lambert5_v");
    }

    MR::startLevelSound(this, "SE_EM_LV_SEARCHBEAMER_BEAM");
    bowToPlayer();
    Vec3f beamHit;
    checkBeamDestination(&beamHit, mFront);
    Vec3f corePos;
    MR::copyJointPos(this, "Core", &corePos);
    MR::vecBlend(corePos, beamHit, &mBeamEnd, MR::calcNerveRate(this, 15));
    updateBeamEffect(MR::isGreaterStep(this, cStep_exeBeamStart_1));
    updateBeamShadow();

    if (MR::isStep(this, cStep_exeBeamStart_2)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvBeamAim));
    }
}

// The beam end chases the player's beam hit at mBeamAimSpeed; the front pitches at most cPitchLimitDegree.
void SearchBeamer::exeBeamAim() {
    MR::startLevelSound(this, "SE_EM_LV_SEARCHBEAMER_BEAM");
    MR::copyJointPos(this, "Core", &mBeamStart);

    Vec3f aimPos;
    calcPlayerAimPos(&aimPos);

    Vec3f toAim;
    toAim.sub(aimPos, mPosition);
    MR::normalize(&toAim);
    Vec3f aimHit;
    checkBeamDestination(&aimHit, toAim);
    Vec3f endToHit;
    endToHit.sub(aimHit, mBeamEnd);
    MR::normalizeOrZero(&endToHit);
    Vec3f nextEnd;
    nextEnd.scaleAdd(mBeamAimSpeed, endToHit, mBeamEnd);
    mFront.sub(nextEnd, mPosition);
    MR::normalize(&mFront);
    Vec3f planeFront;
    MR::turnVecToPlane(&planeFront, mFront, mUp);
    MR::clampVecAngleDeg(&mFront, planeFront, cPitchLimitDegree);
    updateBeamEffect(checkBeamDestination(&mBeamEnd, mFront));
    updateBeamShadow();

    if (!isPlayerInTerritory()) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseMouth));
    }
}

void SearchBeamer::exeCloseMouth() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Close");
        MR::deleteEffectAll(this);
        MR::invalidateHitSensor(this, "beam");
        MR::invalidateShadow(this, "beam");
    }

    reformDirection(false);

    if (MR::isLessStep(this, cStep_exeCloseMouth_1)) {
        Vec3f corePos;
        MR::copyJointPos(this, "Core", &corePos);
        MR::vecBlend(corePos, mBeamEnd, &mBeamStart, MR::calcNerveRate(this, 5));
    }

    if (MR::isStep(this, cStep_exeCloseMouth_2)) {
        initBeamPos();
    }

    if (MR::isBckStopped(this)) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
    }
}

void SearchBeamer::exeStopStart() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Close");
        stopBeam();
    }

    reformDirection(true);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStop));
    }
}

void SearchBeamer::exeStop() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Stop");
        MR::startBrk(this, "Stop");
    }

    reformDirection(true);
    setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff));
}

void SearchBeamer::exeRecover() {
    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::startBrk(this, "Wait");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
    }
}

void SearchBeamer::exePointerSwoon() {
    if (MR::isFirstStep(this)) {
        stopBeam();
    }

    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
}

void SearchBeamer::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::makeMtxFrontUpPos(&mtx, mFront, mUp, mPosition);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
    mPropellerJointCtrl->registerCallBack();
    mBeamStartJointCtrl->registerCallBack();
    mBeamEndJointCtrl->registerCallBack();
}

void SearchBeamer::control() {
    mScaleController->updateNerve();
    updatePropeller();
    tryPointerSwoon();
}

void SearchBeamer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isSensorEnemyAttack(pSender)) {
            MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
        } else if (MR::isSensorEnemy(pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool SearchBeamer::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver) || isStopNerve()) {
        return false;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        stopBeam();
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
        return true;
    }

    return false;
}

// Standing on it stops it; while it is stopped, each touch restarts the StopForPlayerOff wait.
bool SearchBeamer::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (!MR::isMsgFloorTouch(msg)) {
        return false;
    }

    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff))) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff));
        return true;
    }

    return tryStopStart();
}

// The beam sensor sits at the point of the beam nearest the player.
void SearchBeamer::updateHitSensor(HitSensor* pSensor) {
    if (!pSensor->mValidByHost) {
        pSensor->mPosition.set(mBeamStart);
        return;
    }

    Vec3f aimPos;
    calcPlayerAimPos(&aimPos);
    MR::calcPerpendicFootToLineInside(&pSensor->mPosition, aimPos, mPosition, mBeamEnd);
}

bool SearchBeamer::calcJointPropeller(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    MR::rotateMtxLocalYDegree(*pPos, mPropellerAngle);
    return true;
}

bool SearchBeamer::calcJointBeamStart(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    pPos->setTrans(mBeamStart);
    return true;
}

bool SearchBeamer::calcJointBeamEnd(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    pPos->setTrans(mBeamEnd);
    return true;
}

void SearchBeamer::updatePropeller() {
    if (isStopNerve()) {
        mPropellerSpeed -= cPropellerAccel;
    } else {
        mPropellerSpeed += cPropellerAccel;
    }

    mPropellerSpeed = MR::clamp(mPropellerSpeed, cPropellerMinSpeed, cPropellerMaxSpeed);
    mPropellerAngle += mPropellerSpeed;
}

void SearchBeamer::updateBeamEffect(bool isHitMap) {
    if (isHitMap) {
        if (MR::isEffectValid(this, "BeamEndAir")) {
            MR::deleteEffect(this, "BeamEndAir");
        }

        MR::emitEffect(this, "BeamEnd");
    } else {
        if (MR::isEffectValid(this, "BeamEnd")) {
            MR::deleteEffect(this, "BeamEnd");
        }

        MR::emitEffect(this, "BeamEndAir");
    }
}

void SearchBeamer::updateBeamShadow() {
    f32 dist = mPosition.distance(mBeamEnd);
    Vec3f boxSize(cBeamWidth, cBeamWidth, dist);
    MR::setShadowVolumeBoxSize(this, "beam", boxSize);
    Vec3f dropOffset(0.0f, 0.0f, dist / cNum_updateBeamShadow_f1);
    MR::setShadowDropPositionMtxPtr(this, "beam", MR::getJointMtx(this, "BeamStart"), dropOffset);
}

void SearchBeamer::initBeamPos() {
    MR::copyJointPos(this, "Core", &mBeamStart);
    mBeamEnd.set(mBeamStart);
    MR::hideMaterial(this, "lambert5_v");
}

void SearchBeamer::stopBeam() {
    MR::deleteEffectAll(this);
    MR::invalidateHitSensor(this, "beam");
    MR::invalidateShadow(this, "beam");
    initBeamPos();
    MR::validateClipping(this);
}

// Turns the front back onto the plane across mUp.
void SearchBeamer::reformDirection(bool isFast) {
    if (mFront.dot(mUp) != 0.0f) {
        Vec3f planeFront;
        planeFront.killElement2(mFront, mUp);
        MR::normalize(&planeFront);
        f32 deg = isFast ? cStopTurnDegree : cTurnDegree;
        MR::turnVecToVecDegree(&mFront, mFront, planeFront, deg, Vec3f(0, 1, 0));
    }
}

// Faces the player across mUp, then dips toward them, pitch limited to cPitchLimitDegree.
void SearchBeamer::bowToPlayer() {
    Vec3f aimPos;
    calcPlayerAimPos(&aimPos);
    Vec3f toAim;
    toAim.sub(aimPos, mPosition);
    toAim.orthogonalize2(mUp);
    MR::normalize(&toAim);

    Vec3f down;
    down.negate(mUp);
    MR::turnVecToVecDegree(&toAim, toAim, down, cPitchLimitDegree, Vec3f(0, 1, 0));
    MR::turnVecToVecDegree(&mFront, mFront, toAim, cTurnDegree, Vec3f(0, 1, 0));
    Vec3f planeFront;
    MR::turnVecToPlane(&planeFront, mFront, mUp);
    MR::clampVecAngleDeg(&mFront, planeFront, cPitchLimitDegree);
}

// The beam's end along rDir: the first map hit (true) or mBeamLength out (false).
bool SearchBeamer::checkBeamDestination(Vec3f* pHitPos, Vec3f dir) const {
    Vec3f beam;
    beam.scale(mBeamLength, dir);

    if (!MR::getFirstPolyOnLineToMap(pHitPos, nullptr, mPosition, beam)) {
        pHitPos->add(beam, mPosition);
        return false;
    }

    return true;
}

bool SearchBeamer::isPlayerInTerritory() const {
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        return false;
    }

    return MR::isNearPlayer(this, mBeamLength) && MR::isFaceToPlayerHorizontalDegree(this, mFront, cTerritoryFaceDegree);
}

bool SearchBeamer::isStopNerve() const {
    return isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopStart)) || isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStop)) ||
           isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff));
}

bool SearchBeamer::tryNonActive() {
    if (MR::isNearPlayerAnyTime(this, cActiveDistance)) {
        return false;
    }

    setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvNonActive));
    return true;
}

bool SearchBeamer::tryStopStart() {
    if (isStopNerve()) {
        return false;
    }

    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvRecover))) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStop));
    } else {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopStart));
    }

    return true;
}

bool SearchBeamer::tryPointerSwoon() {
    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvPointerSwoon)) || isStopNerve()) {
        return false;
    }

    if (!mBindStarPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvPointerSwoon));
    return true;
}

void SearchBeamer::endNonActive() {
    MR::validateHitSensor(this, "body");
    MR::validateCollisionParts(this);
    MR::onCalcAnim(this);
}

void SearchBeamer::endPointerSwoon() {
    mBindStarPointer->kill();
}

void SearchBeamer::exeStopForPlayerOff() {
    reformDirection(true);
    if (MR::isStep(this, cStep_exeStopForPlayerOff_1)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvRecover));
    }
}
