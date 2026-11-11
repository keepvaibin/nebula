#include "Game/MapObj/CrystalCageMoving.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 350.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBreakBig_f1, "exeBreakBig.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_crashMario_f1, "crashMario.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_crashMario_f2, "crashMario.f2", 450.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 450.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f2, "updateHitSensor.f2", 450.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDummyModel_f1, "initDummyModel.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDummyModel_f2, "initDummyModel.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initDummyModel_f3, "initDummyModel.f3", 3.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeDemoTicoStop_setBckRate_1, "exeDemoTicoStop.setBckRate.1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDemoTicoChange_setBckRate_1, "exeDemoTicoChange.setBckRate.1", 1.5f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeBreakBig_1, "exeBreakBig.1", 10);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, sDummyModelOffset, {0.0f, -150.0f, 0.0f});
};  // namespace

namespace NrvCrystalCageMoving {
    NEW_NERVE(CrystalCageMovingNrvWaitBig, CrystalCageMoving, WaitBig);
    NEW_NERVE_ONEND(CrystalCageMovingNrvBreakBig, CrystalCageMoving, BreakBig, BreakBig);
    NEW_NERVE(CrystalCageMovingNrvWaitSmall, CrystalCageMoving, WaitSmall);
    NEW_NERVE(CrystalCageMovingNrvBreakSmall, CrystalCageMoving, BreakSmall);
    NEW_NERVE(CrystalCageMovingNrvBreakAll, CrystalCageMoving, BreakAll);
    NEW_NERVE(CrystalCageMovingNrvDemoTicoMove, CrystalCageMoving, DemoTicoMove);
    NEW_NERVE(CrystalCageMovingNrvDemoTicoStop, CrystalCageMoving, DemoTicoStop);
    NEW_NERVE(CrystalCageMovingNrvDemoTicoChange, CrystalCageMoving, DemoTicoChange);
};  // namespace NrvCrystalCageMoving

CrystalCageMoving::CrystalCageMoving(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< CrystalCageMoving >);
    mTicoModel = nullptr;
    mCameraInfo = nullptr;
    mStartPos.x = 0.0f;
    mStartPos.y = 0.0f;
    mStartPos.z = 0.0f;
    mIsCoreShown = 0;
    mDummyModelMtx.identity();
}

void CrystalCageMoving::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(0);
    info.setupSound(4);
    info.setupNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvWaitBig));
    info.setupRailMover();
    info.setupHitSensorCallBack();

    Vec3f vec;
    vec.x = 0.0f;
    vec.y = 0.0f;
    vec.z = 0.0f;
    info.setupHitSensorParam(4, cNum_init_f1, vec);
    initialize(rIter, info);
    mStartPos.set(mPosition);
    initDummyModel(rIter);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    MR::startBck(this, "Wait");

    if (MR::isDemoExist("脱出スピドラ出現")) {
        MR::registerDemoActionNerve(this, GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvDemoTicoMove), nullptr);
    }
}

void CrystalCageMoving::exeWaitBig() {
}

void CrystalCageMoving::exeBreakBig() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "CoreWait");
        MR::startActorCameraNoTarget(this, mCameraInfo);
        MR::setSensorRadius(this, "body", cNum_exeBreakBig_f1);
        mIsCoreShown = 1;
        MR::emitEffect(this, "BreakOutside");
        MR::startSound(this, "SE_OJ_CRY_CAGE_MV_BREAK_EDG");
    }

    if (MR::isStep(this, cStep_exeBreakBig_1)) {
        setNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvWaitSmall));
    }
}

void CrystalCageMoving::exeWaitSmall() {
}

void CrystalCageMoving::exeBreakSmall() {
    if (MR::isFirstStep(this)) {
        startBreakDemo();
        MR::emitEffect(this, "BreakInside");
        MR::startSound(this, "SE_OJ_CRY_CAGE_MV_BREAK_CNT");
    }
}

void CrystalCageMoving::exeBreakAll() {
    if (MR::isFirstStep(this)) {
        startBreakDemo();
        MR::emitEffect(this, "BreakOutside");
        MR::emitEffect(this, "BreakInside");
        MR::startSound(this, "SE_OJ_CRY_CAGE_MV_BREAK_ALL");
    }
}

void CrystalCageMoving::exeDemoTicoMove() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mTicoModel, "Fly");
        MR::startSound(mTicoModel, "SE_SM_TICO_SPIN");
    }

    Vec3f toStart;
    toStart.sub(mStartPos, mPosition);

    f32 nerveRate = MR::calcNerveRate(this, NebulaFrameClock::stepsOfMilliseconds(500));
    Vec3f ticoPos;
    ticoPos.scaleAdd(nerveRate, toStart, mPosition);
    mDummyModelMtx.setTrans(ticoPos);

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        setNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvDemoTicoStop));
    }
}

void CrystalCageMoving::exeDemoTicoStop() {
    if (MR::isFirstStep(this)) {
        mDummyModelMtx.set(getBaseMtx());
        mDummyModelMtx.setTrans(mStartPos);
        MR::setBckRate(mTicoModel, cNum_exeDemoTicoStop_setBckRate_1);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        setNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvDemoTicoChange));
    }
}

void CrystalCageMoving::exeDemoTicoChange() {
    if (MR::isFirstStep(this)) {
        MR::startBck(mTicoModel, "Metamorphosis");
        MR::startSound(mTicoModel, "SE_SM_TICO_METAMORPHOSE");
        MR::setBckRate(mTicoModel, cNum_exeDemoTicoChange_setBckRate_1);
    }

    if (MR::isBckStopped(mTicoModel)) {
        kill();
    }
}

void CrystalCageMoving::endBreakBig() {
    MR::endActorCamera(this, mCameraInfo, true);
}

void CrystalCageMoving::kill() {
    if (mTicoModel) {
        mTicoModel->kill();
    }

    MapObjActor::kill();
}

void CrystalCageMoving::control() {
    if (!isNerveTypeEnd()) {
        MapObjActor::control();
        mDummyModelMtx.set(MR::getJointMtx(this, 0));
        Vec3f trans;
        NebulaMtxMultVec(mDummyModelMtx.toMtxPtr(), &::sDummyModelOffset, &trans);
        mDummyModelMtx.setTrans(trans);
    }
}

bool CrystalCageMoving::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerveTypeEnd()) {
        return false;
    }

    if (msg == ACTMES_RUSH_PLAYER_TOUCH) {
        crashMario(pSender, pReceiver);

        return true;
    }

    return false;
}

void CrystalCageMoving::crashMario(HitSensor* pSender, HitSensor* pReceiver) {
    MR::tryRumblePadVeryStrong(this, PLAYER_1);
    MR::shakeCameraVeryStrong();

    if (mIsCoreShown) {
        setNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvBreakSmall));
    } else {
        f32 sensorDist = pReceiver->mPosition.distance(pSender->mPosition);
        f32 sensorObjDist = mPosition.distance(pSender->mPosition);

        if (sensorDist < cNum_crashMario_f1 && sensorObjDist < cNum_crashMario_f2) {
            setNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvBreakAll));
        } else {
            setNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvBreakBig));
        }
    }
}

void CrystalCageMoving::updateHitSensor(HitSensor* pSensor) {
    if (!mIsCoreShown) {
        pSensor->mPosition.set(mPosition);
    } else {
        f32 radius = pSensor->mRadius;
        Pos3f joint_mtx;
        joint_mtx.set(MR::getJointMtx(this, nullptr));
        Vec3f up;
        joint_mtx.getYDir(up);
        Vec3f lineBottom;
        lineBottom.scaleAdd(-cNum_updateHitSensor_f1 + radius, up, mPosition);
        Vec3f lineTop;
        lineTop.scaleAdd(cNum_updateHitSensor_f2 - radius, up, mPosition);
        MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), lineBottom, lineTop);
    }
}

void CrystalCageMoving::connectToScene(const MapObjActorInitInfo& rInfo) {
    if (rInfo.mConnectToScene) {
        MR::connectToSceneCrystal(this);
    }
}

void CrystalCageMoving::initDummyModel(const TableIter& rIter) {
    mDummyModelMtx.set(MR::getJointMtx(this, 0));
    Vec3f dummyPos;
    NebulaMtxMultVec(mDummyModelMtx.toMtxPtr(), &::sDummyModelOffset, &dummyPos);
    mDummyModelMtx.setTrans(dummyPos);
    mTicoModel = new ModelObj("動くクリスタルケージ中身", "Tico", mDummyModelMtx.toMtxPtr(), MR::DrawBufferType_CrystalItem, -2, -2, false);
    mTicoModel->initWithoutIter();
    ModelObj* tico = mTicoModel;
    tico->mScale.x = cNum_initDummyModel_f1;
    tico->mScale.y = cNum_initDummyModel_f2;
    tico->mScale.z = cNum_initDummyModel_f3;
    MR::startBrk(mTicoModel, "ColorChange");
    MR::setBrkFrameAndStop(mTicoModel, 0.0f);

    if (MR::isDemoCast(this, nullptr)) {
        MR::tryRegisterDemoCast(mTicoModel, rIter);
    }
}

void CrystalCageMoving::startBreakDemo() {
    MR::startSound(this, "SE_OJ_CRY_CAGE_MV_TICO_APR");
    MR::requestStartTimeKeepDemo(this, "脱出スピドラ出現", nullptr, nullptr, "クリスタル破壊");
    MR::hideModel(this);
    ModelObj* tico = mTicoModel;
    tico->mScale.x = 1.0f;
    tico->mScale.y = 1.0f;
    tico->mScale.z = 1.0f;
}

bool CrystalCageMoving::isNerveTypeEnd() const {
    return isNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvBreakBig)) ||
           isNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvBreakSmall)) ||
           isNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvBreakAll)) ||
           isNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvDemoTicoMove)) ||
           isNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvDemoTicoStop)) ||
           isNerve(GET_NERVE(CrystalCageMoving, CrystalCageMovingNrvDemoTicoChange));
}
