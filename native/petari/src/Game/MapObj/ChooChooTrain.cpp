#include "Game/MapObj/ChooChooTrain.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 532.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f4, "control.f4", 532.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_init_moveCoord_1, "init.moveCoord.1", 1080.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_moveCoord_1, "control.moveCoord.1", 1080.0f);

namespace {
    static const s32 hWhistleStepMin = NebulaFrameClock::stepsOfSeconds(3);
    static const s32 hWhistleStepMax = NebulaFrameClock::stepsOfSeconds(10);
};  // namespace

ChooChooTrain::ChooChooTrain(const char* pName) : LiveActor(pName), mModelArray(), mClipCenter(0.0f, 0.0f, 0.0f), mRailSpeed(5.0f), mWhistleTimer() {
    lumacore::setClass(this, lumacore::kClassId< ChooChooTrain >);
}

void ChooChooTrain::startClipped() {
    LiveActor::startClipped();

    for (int i = 0; i < mModelArray.size(); i++) {
        mModelArray[i]->startClipped();
    }
}

void ChooChooTrain::endClipped() {
    LiveActor::endClipped();

    for (int i = 0; i < mModelArray.size(); i++) {
        mModelArray[i]->endClipped();
    }
}

void ChooChooTrain::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
    }

    MR::initDefaultPos(this, rIter);
    initRailRider(rIter);
    initModelManagerWithAnm("ChooChooTrain", 0, false);
    MR::connectToSceneCollisionMapObj(this);
    initEffectKeeper(0, 0);
    initSound(4, false);
    mWhistleTimer = MR::getRandom(::hWhistleStepMin, ::hWhistleStepMax);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, cNum_init_f1 * mScale.y);
    initHitSensor(1);
    MR::addMessageSensorMapObjMoveCollision(this, "body");
    MR::initCollisionParts(this, "ChooChooTrain", getSensor("body"), 0);
    MR::onCalcGravity(this);
    s32 arg = -1;
    s32 defTrainParts = 3;
    MR::getPlacementArg0NoInit(rIter, &arg);

    if (arg > 0) {
        defTrainParts = arg;
    }

    arg = -1;
    MR::getPlacementArg1NoInit(rIter, &arg);

    if (arg > 0) {
        mRailSpeed = arg;
    }

    mModelArray.init(defTrainParts);

    int i;

    for (i = 0; i < defTrainParts; i++) {
        ModelObj* pObj = new ModelObj("汽車ポッポ客車", "ChooChooTrainBody", nullptr, -2, MR::MovementType_CollisionMapObj,
                                      MR::CalcAnimType_CollisionMapObj, false);
        pObj->initWithoutIter();
        MR::initCollisionParts(pObj, "ChooChooTrainBody", getSensor("body"), nullptr);
        MR::invalidateClipping(pObj);
        mModelArray.push_back(pObj);
    }

    makeActorAppeared();
    MR::moveCoordToNearestPos(this, mPosition);
    MR::moveTransToCurrentRailPos(this);
    f32 railCoord = MR::getRailCoord(this);
    MR::reverseRailDirection(this);

    for (i = 0; i < defTrainParts; i++) {
        MR::moveCoord(this, cNum_init_moveCoord_1 * mScale.y);
        MR::moveTransToOtherActorRailPos(mModelArray[i], this);
        MR::onCalcGravity(mModelArray[i]);
        mModelArray[i]->makeActorAppeared();
        MR::startBck(mModelArray[i], "Run");
    }

    MR::reverseRailDirection(this);
    MR::setRailCoord(this, railCoord);
    MR::initAndSetRailClipping(&mClipCenter, this, cNum_init_f2, cNum_init_f3);
    MR::startBck(this, "Run");
}

void ChooChooTrain::control() {
    MR::moveCoord(this, mRailSpeed);
    MR::moveTransToCurrentRailPos(this);
    Vec3f dir(MR::getRailDirection(this));
    MR::turnDirectionAndGravityH(this, dir, cNum_control_f1, 1.0f);
    f32 railCoord = MR::getRailCoord(this);
    Vec3f frontVec;
    MR::calcFrontVec(&frontVec, this);
    // Each car faces the coupling at the back of the one ahead.
    Vec3f coupling = mPosition - ((frontVec * cNum_control_f2) * mScale.y);
    MR::reverseRailDirection(this);

    for (int i = 0; i < mModelArray.size(); i++) {
        MR::moveCoord(this, cNum_control_moveCoord_1 * mScale.y);
        MR::moveTransToOtherActorRailPos(mModelArray[i], this);
        dir = coupling - mModelArray[i]->mPosition;
        MR::turnDirectionAndGravityH(mModelArray[i], dir, cNum_control_f3, 1.0f);
        MR::calcFrontVec(&frontVec, mModelArray[i]);

        coupling = mModelArray[i]->mPosition - ((frontVec * cNum_control_f4) * mScale.y);
    }

    MR::reverseRailDirection(this);
    MR::setRailCoord(this, railCoord);
    MR::startLevelSound(this, "SE_OJ_LV_TOY_SL_MOVE");

    if (mWhistleTimer <= 0) {
        MR::startSound(this, "SE_OJ_TOY_SL_WHISTLE");
        mWhistleTimer = MR::getRandom(::hWhistleStepMin, ::hWhistleStepMax);
    } else {
        mWhistleTimer--;
    }
}
