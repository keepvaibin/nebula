#include "Game/MapObj/WatchTowerRotateStep.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // Lifts ride joints 1..cLiftNum.
    const s32 cLiftNum = 4;
    // Degrees per frame; the start eases up to it over cMoveStartStep frames.
    const f32 cRotateSpeed = 0.3f;
    const s32 cMoveStartStep = NebulaFrameClock::stepsOfSeconds(3);
    const f32 cClippingRadiusOffset = 1500.0f;
    const f32 cLiftShadowStartDropOffset = 300.0f;
    const f32 cLiftShadowDropLength = 370.0f;
}  // namespace

namespace NrvWatchTowerRotateStep {
    NEW_NERVE(WatchTowerRotateStepNrvWait, WatchTowerRotateStep, Wait);
    NEW_NERVE(WatchTowerRotateStepNrvMoveStart, WatchTowerRotateStep, MoveStart);
    NEW_NERVE(WatchTowerRotateStepNrvMove, WatchTowerRotateStep, Move);
};  // namespace NrvWatchTowerRotateStep

WatchTowerRotateStep::WatchTowerRotateStep(const char* pName) : LiveActor(pName), mRotateVec(0.0f, 0.0f, 0.0f), mLift() {
    lumacore::setClass(this, lumacore::kClassId< WatchTowerRotateStep >);
}

void WatchTowerRotateStep::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("WatchTowerRotateStep", nullptr, false);
    MR::connectToSceneMapObj(this);

    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, "WatchTowerRotateStep", getSensor(nullptr), nullptr);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, ::cClippingRadiusOffset);

    // A demo cast waits for the demo to start it.
    const bool isDemoCast = MR::tryRegisterDemoCast(this, rIter);
    if (isDemoCast) {
        MR::registerDemoActionNerve(this, GET_NERVE(WatchTowerRotateStep, WatchTowerRotateStepNrvMoveStart), nullptr);
    }

    MR::calcUpVec(&mRotateVec, this);
    initLift(rIter);

    if (isDemoCast) {
        initNerve(GET_NERVE(WatchTowerRotateStep, WatchTowerRotateStepNrvWait));
    } else {
        initNerve(GET_NERVE(WatchTowerRotateStep, WatchTowerRotateStepNrvMove));
    }

    makeActorAppeared();
}

void WatchTowerRotateStep::exeWait() {
}

void WatchTowerRotateStep::exeMoveStart() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_READ_RIDDLE_S");
        MR::startSound(this, "SE_OJ_WATCH_TOWER_START");
    }

    MR::startLevelSound(this, "SE_OJ_LV_WATCH_TOWER_ROTATE");

    const f32 degree = MR::getEaseInValue(getNerveStep(), 0.0f, ::cRotateSpeed, static_cast< f32 >(::cMoveStartStep));
    rotate(degree);

    if (MR::isStep(this, ::cMoveStartStep)) {
        setNerve(GET_NERVE(WatchTowerRotateStep, WatchTowerRotateStepNrvMove));
    }
}

void WatchTowerRotateStep::exeMove() {
    rotate(::cRotateSpeed);
    MR::startLevelSound(this, "SE_OJ_LV_WATCH_TOWER_ROTATE");
}

// Turns the up vector about the front axis and moves the lifts with their joints.
void WatchTowerRotateStep::rotate(f32 degree) {
    Vec3f frontVec;
    MR::calcFrontVec(&frontVec, this);
    MR::rotateVecDegree(&mRotateVec, frontVec, degree);

    attachLift();
}

void WatchTowerRotateStep::calcAndSetBaseMtx() {
    Vec3f frontVec;
    MR::calcFrontVec(&frontVec, this);

    Pos3f baseMtx;
    MR::makeMtxFrontUpPos(&baseMtx, frontVec, mRotateVec, mPosition);
    MR::setBaseTRMtx(this, baseMtx);
}

void WatchTowerRotateStep::initLift(const TableIter& rIter) {
    mLift = new PartsModel*[::cLiftNum];

    for (s32 i = 0; i < ::cLiftNum; i++) {
        PartsModel* pLift = new PartsModel(this, "物見の塔リフト", "WatchTowerRotateStepLift", MR::getJointMtx(this, i + 1), -1, false);
        mLift[i] = pLift;
        pLift->mIsCalcOwnMtx = false;

        MR::initCollisionParts(pLift, "WatchTowerRotateStepLift", getSensor(nullptr), nullptr);
        MR::initShadowVolumeBox(pLift, Vec3f(600.0f, 200.0f, 400.0f), pLift->getBaseMtx());
        MR::setShadowVolumeStartDropOffset(pLift, "WatchTowerRotateStepLift", ::cLiftShadowStartDropOffset);
        MR::setShadowDropLength(pLift, "WatchTowerRotateStepLift", ::cLiftShadowDropLength);

        if (MR::isDemoCast(this, nullptr)) {
            MR::tryRegisterDemoCast(pLift, rIter);
        }

        pLift->initWithoutIter();
    }
}

void WatchTowerRotateStep::attachLift() {
    for (s32 i = 0; i < ::cLiftNum; i++) {
        Pos3f mtx;
        mtx.set(MR::getJointMtx(this, i + 1));
        mtx.getTrans(mLift[i]->mPosition);
    }
}
