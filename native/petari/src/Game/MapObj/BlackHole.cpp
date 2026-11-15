#include "Game/MapObj/BlackHole.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvBlackHole {
    NEW_NERVE(BlackHoleNrvWait, BlackHole, Wait);
    NEW_NERVE(BlackHoleNrvDemo, BlackHole, Demo);
    NEW_NERVE(BlackHoleNrvDisappear, BlackHole, Disappear);
};  // namespace NrvBlackHole

BlackHole::BlackHole(const char* pName) : LiveActor(pName), mBlackHoleModel(), mModelScale(1.0f), mRadius(500.0f), mCubeBox(), mCameraInfo() {
    lumacore::setClass(this, lumacore::kClassId< BlackHole >);
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
    f32 modelRadius = 500.0f * mModelScale;

    if (radius < modelRadius) {
        radius = modelRadius;
    }

    f32 clippingRadius = 500.0f + radius;
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
        mRadius = 500.0f * mScale.x;
    } else {
        Vec3f size(mScale);
        size.scale(500.0f);

        mRadius = size.length();
    }

    f32 scalePerMille;

    if (MR::getPlacementArg0NoInit(rIter, &scalePerMille)) {
        mModelScale = scalePerMille / 1000.0f;
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
    Vec3f vecStart(0.5f * (1000.0f * -mScale.x), 0.5f * (1000.0f * -mScale.y), 0.5f * (1000.0f * -mScale.z));
    Vec3f vecEnd(0.5f * (1000.0f * mScale.x), 0.5f * (1000.0f * mScale.y), 0.5f * (1000.0f * mScale.z));
    mCubeBox->set(vecStart, vecEnd);
}

bool BlackHole::isInCubeBox(const Vec3f& rVec) const {
    Vec3f pos;
    mCubeMtx.multTranspose(rVec, pos);
    return mCubeBox->intersectsPoint(pos);
}

void BlackHole::updateModelScale(f32 rangeScale, f32 coreScale) {
    mScale.setAll< f32 >(rangeScale);
    mBlackHoleModel->mScale.setAll< f32 >(0.5f * coreScale);
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
    mBlackHoleModel->mScale.setAll< f32 >(0.5f * blackHoleEase);

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        kill();
    }
}
