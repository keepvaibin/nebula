#include "Game/MapObj/BlackHole.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMapToolInfo_f1, "initMapToolInfo.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMapToolInfo_f2, "initMapToolInfo.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initMapToolInfo_f3, "initMapToolInfo.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f1, "initCubeBox.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f2, "initCubeBox.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f3, "initCubeBox.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f4, "initCubeBox.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f5, "initCubeBox.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f6, "initCubeBox.f6", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f7, "initCubeBox.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f8, "initCubeBox.f8", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f9, "initCubeBox.f9", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f10, "initCubeBox.f10", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f11, "initCubeBox.f11", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initCubeBox_f12, "initCubeBox.f12", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateModelScale_f1, "updateModelScale.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDisappear_f1, "exeDisappear.f1", 0.5f);

namespace NrvBlackHole {
    NEW_NERVE(BlackHoleNrvWait, BlackHole, Wait);
    NEW_NERVE(BlackHoleNrvDemo, BlackHole, Demo);
    NEW_NERVE(BlackHoleNrvDisappear, BlackHole, Disappear);
};  // namespace NrvBlackHole

BlackHole::BlackHole(const char* pName) : LiveActor(pName), mBlackHoleModel(), mModelScale(1.0f), mRadius(500.0f), mCubeBox(), mCameraInfo() {
    mCubeMtx.identity();
    mSuctionEffectMtx.identity();
}

void BlackHole::init(const TableIter& rIter) {
    initMapToolInfo(rIter);
    initModel();
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorEye(this, "body", 16, mRadius, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    MR::setEffectHostMtx(this, "BlackHoleSuction", mSuctionEffectMtx.toMtxPtr());
    f32 radius = mRadius;
    f32 modelRadius = cNum_init_f1 * mModelScale;

    if (radius < modelRadius) {
        radius = modelRadius;
    }

    f32 clippingRadius = cNum_init_f2 + radius;
    MR::setClippingTypeSphere(this, clippingRadius);
    MR::setClippingTypeSphere(mBlackHoleModel, clippingRadius);
    MR::setClippingFarMax(this);
    MR::setClippingFarMax(mBlackHoleModel);
    initSound(4, false);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(BlackHole, BlackHoleNrvDisappear), 0);
    }

    bool isCreated = MR::createActorCameraInfoIfExist(rIter, &mCameraInfo);

    if (isCreated) {
        MR::initActorCamera(this, rIter, &mCameraInfo);
    }

    initNerve(GET_NERVE(BlackHole, BlackHoleNrvWait));
    bool uses = MR::useStageSwitchReadAppear(this, rIter);

    if (uses) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void BlackHole::makeActorAppeared() {
    bool isOnSwitch = MR::isValidSwitchA(this) && MR::isOnSwitchA(this);

    if (!isOnSwitch) {
        LiveActor::makeActorAppeared();
        mBlackHoleModel->makeActorAppeared();
    }
}

void BlackHole::kill() {
    LiveActor::kill();
    mBlackHoleModel->kill();
}

bool BlackHole::tryStartDemoCamera() {
    if (mCameraInfo != nullptr) {
        MR::startActorCameraTargetSelf(this, mCameraInfo);
        return true;
    }

    return false;
}

void BlackHole::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    Vec3f dirToCam;
    dirToCam.sub(MR::getCamPos(), mPosition);
    Vec3f camYDir;
    camYDir.set(MR::getCamYdir());

    if (MR::normalizeOrZero(&dirToCam)) {
        return;
    }

    if (MR::isSameDirection(dirToCam, camYDir)) {
        return;
    }

    MR::makeMtxFrontUpPos(&mSuctionEffectMtx, dirToCam, camYDir, mPosition);

    mSuctionEffectMtx.scaleXYZ(mScale.x);
}

void BlackHole::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(BlackHole, BlackHoleNrvWait))) {
        return;
    }

    if (mCubeBox != nullptr && !isInCubeBox(pReceiver->mPosition)) {
        return;
    }

    if (!MR::sendArbitraryMsg(ACTMES_INHALE_BLACK_HOLE, pReceiver, pSender)) {
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    setNerve(GET_NERVE(BlackHole, BlackHoleNrvDemo));
}

void BlackHole::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchReadAppear(this, rIter);

    if (MR::isEqualObjectName(rIter, "BlackHoleCube")) {
        initCubeBox();
        setName("ブラックホール[キューブ指定]");
    }

    if (mCubeBox == nullptr) {
        mRadius = cNum_initMapToolInfo_f1 * mScale.x;
    } else {
        Vec3f size(mScale);
        size.scale(cNum_initMapToolInfo_f2);

        mRadius = size.length();
    }

    f32 scalePerMille;

    if (MR::getPlacementArg0NoInit(rIter, &scalePerMille)) {
        mModelScale = scalePerMille / cNum_initMapToolInfo_f3;
    } else if (mCubeBox == nullptr) {
        mModelScale = mScale.x;
    } else {
        mModelScale = 1.0f;
    }
}

void BlackHole::initModel() {
    initModelManagerWithAnm("BlackHoleRange", nullptr, false);
    mBlackHoleModel = MR::createModelObjMapObj("コアモデル", "BlackHole", getBaseMtx());
    mBlackHoleModel->makeActorDead();
    updateModelScale(mModelScale, mModelScale);
}

void BlackHole::initCubeBox() {
    MR::makeMtxRotate(mCubeMtx, mRotation.x, mRotation.y, mRotation.z);
    mCubeMtx.setTrans(mPosition);

    mCubeBox = new Box3f();
    Vec3f vecStart(cNum_initCubeBox_f1 * (cNum_initCubeBox_f2 * -mScale.x), cNum_initCubeBox_f3 * (cNum_initCubeBox_f4 * -mScale.y), cNum_initCubeBox_f5 * (cNum_initCubeBox_f6 * -mScale.z));
    Vec3f vecEnd(cNum_initCubeBox_f7 * (cNum_initCubeBox_f8 * mScale.x), cNum_initCubeBox_f9 * (cNum_initCubeBox_f10 * mScale.y), cNum_initCubeBox_f11 * (cNum_initCubeBox_f12 * mScale.z));
    mCubeBox->set(vecStart, vecEnd);
}

bool BlackHole::isInCubeBox(const Vec3f& rVec) const {
    Vec3f pos;
    mCubeMtx.multTranspose(rVec, pos);
    return mCubeBox->intersectsPoint(pos);
}

void BlackHole::updateModelScale(f32 rangeScale, f32 coreScale) {
    mScale.setAll< f32 >(rangeScale);
    mBlackHoleModel->mScale.setAll< f32 >(cNum_updateModelScale_f1 * coreScale);
}

void BlackHole::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "BlackHoleRange");
        MR::startBtk(this, "BlackHoleRange");
        MR::startBtk(mBlackHoleModel, "BlackHole");
    }

    MR::startLevelSound(this, "SE_OJ_LV_BLACK_HOLE");
    bool isOnSwitch = false;

    if (MR::isValidSwitchA(this) && MR::isOnSwitchA(this)) {
        isOnSwitch = true;
    }

    if (isOnSwitch) {
        kill();
    }
}

void BlackHole::exeDemo() {
    MR::startLevelSound(this, "SE_OJ_LV_BLACK_HOLE");
}

void BlackHole::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_BLACK_HOLE_DISAPPEAR");
    }

    f32 nerveEaseIn = MR::calcNerveEaseInValue(this, NebulaFrameClock::stepsOfMilliseconds(1500), mModelScale, 0.0f);
    f32 blackHoleEase = MR::calcNerveEaseInValue(this, NebulaFrameClock::stepsOfMilliseconds(500), NebulaFrameClock::stepsOfMilliseconds(1500), mModelScale, 0.0f);
    mScale.setAll< f32 >(nerveEaseIn);
    mBlackHoleModel->mScale.setAll< f32 >(cNum_exeDisappear_f1 * blackHoleEase);

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        kill();
    }
}
