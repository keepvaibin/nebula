#include "Game/Enemy/SearchBeamer.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

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
    inline void calcPlayerAimPos(Vec3f* pPos) {
        Vec3f up;
        MR::getPlayerUpVec(&up);
        pPos->scaleAdd(-20.0f, up, *MR::getPlayerCenterPos());
    }
};  // namespace

SearchBeamer::SearchBeamer(const char* pName) : LiveActor(pName) {
    mScaleController = nullptr;
    mBindStarPointer = nullptr;
    mFront.set(0.0f, 0.0f, 1.0f);
    mUp.set(0.0f, 0.0f, 0.0f);
    mPropellerAngle = 0;
    mPropellerSpeed = 0;
    mPropellerJointCtrl = nullptr;
    mBeanStartJointCtrl = nullptr;
    mBeamEndJointCtrl = nullptr;
    mBeamStart.set(0.0f, 0.0f, 0.0f);
    mBeamEnd.set(0.0f, 0.0f, 0.0f);
    mBeamAimSpeed = 8.0f;
    mTerritoryRadius = 1000.0f;
    mBeamLength = 0;
    mCollisionMtx.identity();
}

void SearchBeamer::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("SearchBeamer", nullptr, false);
    MR::connectToSceneCollisionMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addHitSensorEnemy(this, "body", 8, 60.0f, Vec3f(0.0f, -80.0f, 0.0f));
    MR::addHitSensorCallbackEnemyAttack(this, "beam", 8, 15.0f);
    MR::invalidateHitSensor(this, "beam");
    mCollisionMtx.setInline(getBaseMtx());
    MR::initCollisionParts(this, "Face", getSensor("body"), mCollisionMtx);
    initEffectKeeper(1, nullptr);
    initSound(4, false);
    MR::initStarPointerTarget(this, 80.0f, Vec3f(0, 0, 0));
    mScaleController = new AnimScaleController(nullptr);
    mScaleController->setParamTight();
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);

    MR::initShadowController(this, 2);
    MR::addShadowVolumeCylinder(this, "body", 80.0f);
    MR::addShadowVolumeBox(this, "beam", Vec3f(10.0f, 10.0f, 100.0f), MR::getJointMtx(this, "BeamStart"));
    MR::setShadowVolumeStartDropOffset(this, "body", 100.0f);
    MR::invalidateShadow(this, "beam");
    MR::setShadowDropLength(this, "body", 200.0f);
    MR::setShadowDropLength(this, "beam", 200.0f);
    MR::useStageSwitchSleep(this, rIter);
    MR::useStageSwitchReadA(this, rIter);

    mPropellerJointCtrl = MR::createJointController(this, "Propeller", &SearchBeamer::calcJointPropeller);
    mPropellerSpeed = 40.0f;
    mBeanStartJointCtrl = MR::createJointController(this, "BeamStart", &SearchBeamer::calcJointBeamStart);
    mBeamEndJointCtrl = MR::createJointController(this, "BeamEnd", &SearchBeamer::calcJointBeamEnd);
    initBeamPos();
    MR::getPlacementArg0NoInit(rIter, &mBeamAimSpeed);
    MR::getPlacementArg1NoInit(rIter, &mTerritoryRadius);
    mBeamLength = 300.0f + mTerritoryRadius;
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

    if (MR::isNearPlayerAnyTime(this, 4000.0f)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitFar));
    }
}

void SearchBeamer::exeCloseWaitFar() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
        MR::startBrk(this, "Wait");
    }

    reformDirection(false);

    if (MR::isStep(this, 30)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseSearch));
    } else if (MR::isNearPlayer(this, 2500.0f)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
    } else {
        if (tryNonActive()) {
            return;
        }
    }
}

void SearchBeamer::exeCloseSearch() {
    reformDirection(false);
    MR::rotateVecDegree(&mFront, mUp, 3.0f);

    if (MR::isStep(this, 50)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitFar));
    }
}

void SearchBeamer::exeCloseWaitNear() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
        MR::startBrk(this, "Wait");
    }

    reformDirection(false);
    MR::turnDirectionToPlayerDegree(this, &mFront, 3.0f);
    bool isNear = MR::isNearPlayer(this, 3000.0f) == false;

    if (isNear) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitFar));

    } else {
        if (isPlayerInTerritory()) {
            setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvOpenMouth));
        }
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

    if (MR::isStep(this, 10)) {
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
    Vec3f v7;
    checkBeamDistiny(&v7, Vec3f(mFront));
    Vec3f corePos;
    MR::copyJointPos(this, "Core", &corePos);
    MR::vecBlend(corePos, v7, &mBeamEnd, MR::calcNerveRate(this, 15));
    updateBeamEffect(MR::isGreaterStep(this, 15));
    updateBeamShadow();

    if (MR::isStep(this, 1)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvBeamAim));
    }
}

void SearchBeamer::exeBeamAim() {
    MR::startLevelSound(this, "SE_EM_LV_SEARCHBEAMER_BEAM");
    MR::copyJointPos(this, "Core", &mBeamStart);

    Vec3f v14;
    ::calcPlayerAimPos(&v14);

    Vec3f v13;
    v13.sub(v14, mPosition);
    MR::normalize(&v13);
    Vec3f v12;
    checkBeamDistiny(&v12, Vec3f(v13));
    Vec3f v11;
    v11.sub(v12, mBeamEnd);
    MR::normalizeOrZero(&v11);
    Vec3f v10;
    v10.scaleAdd(mBeamAimSpeed, v11, mBeamEnd);
    mFront.sub(v10, mPosition);
    MR::normalize(&mFront);
    Vec3f v9;
    MR::turnVecToPlane(&v9, mFront, mUp);
    MR::clampVecAngleDeg(&mFront, v9, 35.0f);
    updateBeamEffect(checkBeamDistiny(&mBeamEnd, Vec3f(mFront)));
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

    if (MR::isLessStep(this, 5)) {
        Vec3f corePos;
        MR::copyJointPos(this, "Core", &corePos);
        MR::vecBlend(corePos, mBeamEnd, &mBeamStart, MR::calcNerveRate(this, 5));
    }

    if (MR::isStep(this, 5)) {
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
        MR::deleteEffectAll(this);
        MR::invalidateHitSensor(this, "beam");
        MR::invalidateShadow(this, "beam");
        initBeamPos();
        MR::validateClipping(this);
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
    if (MR::isStep(this, 60)) {
        MR::startBrk(this, "Wait");
    }

    if (MR::isStep(this, 120)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
    }
}

void SearchBeamer::exePointerSwoon() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffectAll(this);
        MR::invalidateHitSensor(this, "beam");
        MR::invalidateShadow(this, "beam");
        initBeamPos();
        MR::validateClipping(this);
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
    mBeanStartJointCtrl->registerCallBack();
    mBeamEndJointCtrl->registerCallBack();
}

void SearchBeamer::control() {
    mScaleController->updateNerve();
    updatePropeller();

    if (tryPointerSwoon()) {
        return;
    }
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

bool SearchBeamer::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    bool isStopped = false;

    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopStart)) || isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStop)) ||
        isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff))) {
        isStopped = true;
    }

    if (isStopped) {
        return false;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        MR::deleteEffectAll(this);
        MR::invalidateHitSensor(this, "beam");
        MR::invalidateShadow(this, "beam");
        initBeamPos();
        MR::validateClipping(this);
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvCloseWaitNear));
        return true;
    }

    return false;
}

bool SearchBeamer::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgFloorTouch(msg)) {
        if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff))) {
            setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff));
            return true;
        } else {
            return tryStopStart();
        }
    }

    return false;
}

void SearchBeamer::updateHitSensor(HitSensor* pSensor) {
    if (!pSensor->mValidByHost) {
        pSensor->mPosition.set(mBeamStart);
        return;
    }

    Vec3f v7;
    Vec3f up;
    MR::getPlayerUpVec(&up);
    v7.scaleAdd(-20.0f, up, *MR::getPlayerCenterPos());
    MR::calcPerpendicFootToLineInside(&pSensor->mPosition, v7, mPosition, mBeamEnd);
}

bool SearchBeamer::calcJointPropeller(Pos3f* pPos, const JointControllerInfo& rInfo) {
    MR::rotateMtxLocalYDegree(*pPos, mPropellerAngle);
    return true;
}

bool SearchBeamer::calcJointBeamStart(Pos3f* pPos, const JointControllerInfo& rInfo) {
    pPos->setTrans(mBeamStart);
    return true;
}

bool SearchBeamer::calcJointBeamEnd(Pos3f* pPos, const JointControllerInfo& rInfo) {
    pPos->setTrans(mBeamEnd);
    return true;
}

// TODO -- float issues, missing frsp
void SearchBeamer::updatePropeller() {
    bool isStopped = false;

    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopStart)) || isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStop)) ||
        isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff))) {
        isStopped = true;
    }

    if (isStopped) {
        mPropellerSpeed -= 0.30f;
    } else {
        mPropellerSpeed += 0.30f;
    }

    mPropellerSpeed = MR::clamp(mPropellerSpeed, 10.0f, 40.0f);
    mPropellerAngle += mPropellerSpeed;
}

void SearchBeamer::updateBeamEffect(bool a1) {
    if (a1) {
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
    Vec3f v5;
    v5.z = dist;
    v5.x = 15.0f;
    v5.y = 15.0f;
    MR::setShadowVolumeBoxSize(this, "beam", v5);
    Vec3f v4;
    v4.x = 0.0f;
    v4.y = 0.0f;
    v4.z = dist / 2.0f;
    MR::setShadowDropPositionMtxPtr(this, "beam", MR::getJointMtx(this, "BeamStart"), v4);
}

void SearchBeamer::initBeamPos() {
    MR::copyJointPos(this, "Core", &mBeamStart);
    mBeamEnd.set(mBeamStart);
    MR::hideMaterial(this, "lambert5_v");
}

void SearchBeamer::reformDirection(bool a1) {
    if (mFront.dot(mUp) != 0.0f) {
        Vec3f v8;
        v8.killElement2(mFront, mUp);
        MR::normalize(&v8);
        f32 deg = a1 ? 15.0f : 3.0f;
        MR::turnVecToVecDegree(&mFront, mFront, v8, deg, Vec3f(0, 1, 0));
    }
}

void SearchBeamer::bowToPlayer() {
    Vec3f v12;
    ::calcPlayerAimPos(&v12);
    Vec3f v11;
    v11.sub(v12, mPosition);
    v11.orthogonalize2(mUp);
    MR::normalize(&v11);

    Vec3f down;
    down.negate(mUp);
    MR::turnVecToVecDegree(&v11, v11, down, 35.0f, Vec3f(0, 1, 0));
    MR::turnVecToVecDegree(&mFront, mFront, v11, 3.0f, Vec3f(0, 1, 0));
    Vec3f v9;
    MR::turnVecToPlane(&v9, mFront, mUp);
    MR::clampVecAngleDeg(&mFront, v9, 35.0f);
}

bool SearchBeamer::checkBeamDistiny(Vec3f* pPosition, Vec3f a2) const {
    Vec3f v12;
    v12.scale(mBeamLength, a2);

    if (!MR::getFirstPolyOnLineToMap(pPosition, nullptr, mPosition, v12)) {
        pPosition->add(v12, mPosition);
        return false;
    }

    return true;
}

bool SearchBeamer::isPlayerInTerritory() const {
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        return false;
    }

    bool ret = false;

    if (MR::isNearPlayer(this, mBeamLength)) {
        if (MR::isFaceToPlayerHorizontalDegree(this, mFront, 45.0f)) {
            ret = true;
        }
    }

    return ret;
}

bool SearchBeamer::tryNonActive() {
    if (MR::isNearPlayerAnyTime(this, 4000.0f)) {
        return false;
    }

    setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvNonActive));
    return true;
}

bool SearchBeamer::tryStopStart() {
    bool isStopped = false;

    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopStart)) || isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStop)) ||
        isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff))) {
        isStopped = true;
    }

    if (isStopped) {
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
    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvPointerSwoon))) {
        return false;
    }

    bool isStopped = false;

    if (isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopStart)) || isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStop)) ||
        isNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvStopForPlayerOff))) {
        isStopped = true;
    }

    if (isStopped) {
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
    if (MR::isStep(this, 5)) {
        setNerve(GET_NERVE(SearchBeamer, SearchBeamerNrvRecover));
    }
}
