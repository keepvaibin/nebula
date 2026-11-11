#include "Game/MapObj/PowerStar.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/PowerStarAppearPoint.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_processWait_f1, "processWait.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearDemoRise_f1, "exeAppearDemoRise.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearDemoMove_f1, "exeAppearDemoMove.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearDemoMove_f2, "exeAppearDemoMove.f2", 360.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWeakToWait_setBrkRate_1, "exeWeakToWait.setBrkRate.1", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAppearDemoKoopa_1, "exeAppearDemoKoopa.1", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_exeStageClearDemo_1, "exeStageClearDemo.1", 1);

namespace {
    // Indexed by PowerStar::Color.
    const NebulaColor8 cLightColor[] = {
        {0x96, 0x96, 0x32, 0}, {0x32, 0x32, 0x96, 0}, {0x32, 0x96, 0x32, 0}, {0x96, 0x32, 0x32, 0}, {0x64, 0x64, 0x64, 0},
    };

    struct GrandStarSensor {
        const char* mName;
        f32 mX;
        f32 mY;
        f32 mZ;
        f32 mRadius;
    };

    // The five points and the centre.
    const GrandStarSensor cGrandStarSensors[] = {
        {"body1", 0.0f, 100.0f, 0.0f, 60.0f},  {"body2", -95.0f, 35.0f, 0.0f, 60.0f}, {"body3", -55.0f, -75.0f, 0.0f, 60.0f},
        {"body4", 55.0f, -75.0f, 0.0f, 60.0f}, {"body5", 95.0f, 35.0f, 0.0f, 60.0f},  {"body6", 0.0f, 0.0f, 0.0f, 80.0f},
    };

    const s32 cGrandStarSensorCount = sizeof(cGrandStarSensors) / sizeof(cGrandStarSensors[0]);

    const char* const cAppearDemoName = "パワースター出現";
    const char* const cStageClearAnimNameKoopaVs3 = "GrandStarGetKoopaVs3";
    NEBULA_TUNABLE(s32, cAppearPointGroupMax, 16);
    NEBULA_TUNABLE(u16, cSensorGroupSize, 8);
    NEBULA_TUNABLE(f32, cBodyRadius, 75.0f);
    NEBULA_TUNABLE(f32, cClippingRadius, 5000.0f);
    NEBULA_TUNABLE(f32, cCometClipDistance, 5000.0f);
    NEBULA_TUNABLE(f32, cCometClipLeaveMargin, 500.0f);
    NEBULA_TUNABLE(f32, cGrandStarNearClip, 250.0f);
    NEBULA_TUNABLE(f32, cGrandStarNearClipMargin, 50.0f);
    NEBULA_TUNABLE(f32, cPowerStarRotateSpeed, 3.0f);
    NEBULA_TUNABLE(f32, cGrandStarRotateSpeed, 2.0f);
    NEBULA_TUNABLE(f32, cWeakRotateSpeed, 0.3f);
    NEBULA_TUNABLE(f32, cWeakNoRotateAngle, 45.0f);
    const s32 cWeakToWaitSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, cBgmFadeOutSteps, 5);
    const s32 cBgmFadeInSteps = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cRiseEaseSteps = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, cRiseHeight, 300.0f);
    NEBULA_TUNABLE(f32, cRiseSpinSpeed, 10.0f);
    NEBULA_TUNABLE(s32, cRiseSteps, 80);
    const s32 cMoveSteps = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, cMoveSpinSpeed, 20.0f);
    NEBULA_TUNABLE(f32, cArcHeightMin, 300.0f);
    NEBULA_TUNABLE(f32, cArcHeightMax, 5000.0f);
    NEBULA_TUNABLE(f32, cSpinDownSteps, 90.0f);
    const s32 cMoveEndStep = NebulaFrameClock::stepsOfMilliseconds(3500);
    const s32 cKoopaVs3LightDuration = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cKoopaBgmStep = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(s32, cKoopaVs3BgmStep, 152);
    // Steps before the demo bck ends.
    NEBULA_TUNABLE(s32, cKoopaVs3StageBgmLead, 30);
    const s32 cKoopaVs3ClearStep = NebulaFrameClock::stepsOfSeconds(10);

    const char* getKoopaDemoName() {
        return MR::isStageKoopaVs3() ? "DemoKoopaGrandStarVs3" : "DemoKoopaGrandStar";
    }

    s32 calcColorWithDeclarer(const NameObj* pDeclarer, s32 starId) {
        if (MR::hasPowerStarInCurrentStageWithDeclarer(pDeclarer->mName, starId)) {
            return PowerStar::Color_Collected;
        }

        if (MR::isPowerStarGreenInCurrentStageWithDeclarer(pDeclarer->mName, starId)) {
            return PowerStar::Color_Green;
        }

        if (MR::isPowerStarRedInCurrentStageWithDeclarer(pDeclarer->mName, starId)) {
            return PowerStar::Color_Red;
        }

        return PowerStar::Color_Gold;
    }

    s32 calcColorAtResultSequence() {
        if (MR::hasPowerStarAtResultSequence()) {
            return PowerStar::Color_Collected;
        }

        if (MR::isPowerStarGreenAtResultSequence()) {
            return PowerStar::Color_Green;
        }

        if (MR::isPowerStarRedAtResultSequence()) {
            return PowerStar::Color_Red;
        }

        return PowerStar::Color_Gold;
    }

    void setupColor(LiveActor* pActor, s32 color) {
        bool isCollected = color == PowerStar::Color_Collected;
        MR::startBtp(pActor, "PowerStar");
        MR::setBtpFrameAndStop(pActor, isCollected ? 0.0f : color);
        MR::startBva(pActor, "PowerStar");
        MR::setBvaFrameAndStop(pActor, isCollected ? 1.0f : 0.0f);
    }

    void setupColorGrandStar(LiveActor* pActor, bool isCollected) {
        if (isCollected) {
            MR::showMaterial(pActor, "GrandStarEmpty");
            MR::hideMaterial(pActor, "FooMat");
        } else {
            MR::showMaterial(pActor, "FooMat");
            MR::hideMaterial(pActor, "GrandStarEmpty");
            MR::startBrk(pActor, "Recover");
            MR::setBrkFrameEndAndStop(pActor);
        }
    }

    void requestPointLight(const LiveActor* pActor, s32 color) {
        Vec3f jointPos;

        MR::copyJointPos(pActor, "PowerStar", &jointPos);
        MR::requestPointLight(pActor, Vec3f(jointPos), cLightColor[color], 1.0f, -1);
    }
};  // namespace

namespace NrvPowerStar {
    NEW_NERVE(PowerStarNrvWaitStartAppear, PowerStar, WaitStartAppear);
    NEW_NERVE(PowerStarNrvAppearDemoRise, PowerStar, AppearDemoRise);
    NEW_NERVE(PowerStarNrvAppearDemoMove, PowerStar, AppearDemoMove);
    NEW_NERVE(PowerStarNrvAppearDemoKoopa, PowerStar, AppearDemoKoopa);
    NEW_NERVE(PowerStarNrvWait, PowerStar, Wait);
    NEW_NERVE(PowerStarNrvWeak, PowerStar, Weak);
    NEW_NERVE(PowerStarNrvWeakNoRotate, PowerStar, WeakNoRotate);
    NEW_NERVE(PowerStarNrvWeakToWait, PowerStar, WeakToWait);
    NEW_NERVE(PowerStarNrvStageClearDemo, PowerStar, StageClearDemo);
};  // namespace NrvPowerStar

PowerStar::PowerStar(const char* pName)
    : LiveActor(pName), mPowerStarId(-1), mIsInDemo(false), mAppearPosition(gZeroVec), mInitRotation(gZeroVec), mInitPosition(gZeroVec),
      mPowerStarModelObj(nullptr), mIsForceAppear(false), mColor(Color_Gold), mIsGrandStar(false), mIsRiseOnGravity(false), mIsNoPostureFront(false),
      mIsCameraActorPose(false), mCameraInfo(nullptr), mCameraActor(nullptr), mLuigiNPC(nullptr), mIsWeak(false) {
    lumacore::setClass(this, lumacore::kClassId< PowerStar >);
    mPostureMtx.identity();
    mBaseMtx.identity();
    mPlayerMtx.identity();
}

void PowerStar::init(const TableIter& rIter) {
    mIsGrandStar = MR::isEqualObjectName(rIter, "GrandStar");

    initMapToolInfo(rIter);
    initModel();

    if (mIsGrandStar) {
        MR::connectToSceneIndirectMapObj(this);
        initSensorGrandStar();
    } else {
        MR::connectToSceneNoSilhouettedMapObj(this);
        initHitSensor(1);
        MR::addHitSensor(this, "body", ATYPE_POWER_STAR_BIND, cSensorGroupSize, cBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    }

    initEffectKeeper(0, nullptr);

    if (MR::isGalaxyGhostCometAppearInCurrentStage()) {
        MR::invalidateClipping(this);
    } else {
        MR::setClippingTypeSphere(this, cClippingRadius);
    }

    initShadow(rIter);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    MR::initAnimCamera(mPowerStarModelObj, mCameraInfo, getGetDemoName());

    if (mIsGrandStar && MR::isStageKoopaVs()) {
        MR::initAnimCamera(mPowerStarModelObj, mCameraInfo, ::getKoopaDemoName());

        if (MR::isStageKoopaVs3()) {
            MR::initAnimCamera(mPowerStarModelObj, mCameraInfo, ::cStageClearAnimNameKoopaVs3);
        }
    }

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(PowerStar, PowerStarNrvWeakNoRotate), "ミニ太陽消失");
        MR::registerDemoActionNerve(this, GET_NERVE(PowerStar, PowerStarNrvWeakToWait), "グランドスター復活");
        MR::registerDemoSimpleCastAll(this);
    }

    initSound(4, false);
    initNerve(GET_NERVE(PowerStar, PowerStarNrvWait));

    if (mPowerStarId == -1 && mIsInDemo) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void PowerStar::initAfterPlacement() {
}

void PowerStar::appear() {
    LiveActor::appear();
    MR::invalidateClipping(this);
    requestAppearOrWait();
}

void PowerStar::makeActorAppeared() {
    mInitPosition.set(mPosition);
    initPosture();
    LiveActor::makeActorAppeared();
}

void PowerStar::requestAppear() {
    makeActorAppeared();
    MR::invalidateClipping(this);
    MR::hideModel(this);
    MR::invalidateHitSensors(this);
    setNerve(GET_NERVE(PowerStar, PowerStarNrvWaitStartAppear));

    if (MR::isStageKoopaVs()) {
        MR::requestStartDemoMarioPuppetableWithoutCinemaFrame(this, ::cAppearDemoName, GET_NERVE(PowerStar, PowerStarNrvAppearDemoKoopa), nullptr);
    } else {
        MR::requestStartDemoWithoutCinemaFrame(this, ::cAppearDemoName, GET_NERVE(PowerStar, PowerStarNrvAppearDemoRise), nullptr);
    }
}

void PowerStar::setDemoAppearPos(const Vec3f& rPos) {
    if (MR::getGroupFromArray(this) != nullptr) {
        // A group with no appear point (a mod's) leaves the star appearing where it stands.
        mCameraActor = getNearestAppearPoint(rPos);
        if (mCameraActor != nullptr) {
            mAppearPosition.set(mCameraActor->mPosition);
        }
    }

    mPosition.set(rPos);
}

bool PowerStar::isEndAppearDemo() const {
    return isNerve(GET_NERVE(PowerStar, PowerStarNrvWait)) || isNerve(GET_NERVE(PowerStar, PowerStarNrvStageClearDemo));
}

void PowerStar::offAppearDemo() {
    mIsInDemo = false;
}

void PowerStar::setupColor(LiveActor* pActor, const NameObj* pDeclarer, s32 starId) {
    s32 color = ::calcColorWithDeclarer(pDeclarer, starId);

    if (MR::isGrandStarInCurrentStageWithDeclarer(pDeclarer->mName, starId)) {
        ::setupColorGrandStar(pActor, color == Color_Collected);
    } else {
        ::setupColor(pActor, color);
    }
}

void PowerStar::setupColorAtResultSequence(LiveActor* pPowerStar, bool isGrandStar) {
    s32 color = ::calcColorAtResultSequence();

    if (isGrandStar) {
        ::setupColorGrandStar(pPowerStar, color == Color_Collected);
    } else {
        ::setupColor(pPowerStar, color);
    }
}

void PowerStar::requestPointLight(const LiveActor* pActor, const NameObj* pDeclarer, s32 starId) {
    ::requestPointLight(pActor, ::calcColorWithDeclarer(pDeclarer, starId));
}

void PowerStar::requestPointLightAtResultSequence(const LiveActor* pActor) {
    ::requestPointLight(pActor, ::calcColorAtResultSequence());
}

f32 PowerStar::getPowerStarWaitRotateSpeed() {
    return cPowerStarRotateSpeed;
}

s32 PowerStar::getBtpFrameCurrentStage(s32 starId) {
    if (MR::isPowerStarGreenInCurrentStage(starId)) {
        return Color_Green;
    }

    if (MR::isPowerStarRedInCurrentStage(starId)) {
        return Color_Red;
    }

    return Color_Gold;
}

bool PowerStar::isCurrentStageKoopaVs3() {
    return MR::isStageKoopaVs3();
}

void PowerStar::initShadowPowerStar(LiveActor* pActor, bool isCylinder) {
    MR::initShadowFromCSV(pActor, "Shadow");
    MR::invalidateShadowAll(pActor);
    MR::validateShadowGroup(pActor, isCylinder ? "円柱" : "通常");
}

void PowerStar::control() {
    if (isNerve(GET_NERVE(PowerStar, PowerStarNrvWaitStartAppear))) {
        return;
    }

    Vec3f jointPos;

    if (isNerve(GET_NERVE(PowerStar, PowerStarNrvStageClearDemo))) {
        MR::copyJointPos(mPowerStarModelObj, "PowerStar", &jointPos);
    } else {
        MR::copyJointPos(this, "PowerStar", &jointPos);
    }

    s32 duration = (isNerve(GET_NERVE(PowerStar, PowerStarNrvAppearDemoKoopa)) && MR::isStageKoopaVs3()) ? cKoopaVs3LightDuration : -1;
    MR::requestPointLight(this, Vec3f(jointPos), cLightColor[mColor], 1.0f, duration);
}

void PowerStar::calcAndSetBaseMtx() {
    Pos3f rotateY;
    rotateY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), MR::toRadian(mRotation.y));

    Pos3f baseMtx;
    baseMtx.concat(mPostureMtx, rotateY);
    baseMtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, baseMtx);
}

bool PowerStar::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgStarPieceReflect(msg) || MR::isMsgJetTurtleAttack(msg);
}

bool PowerStar::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    switch (msg) {
    case ACTMES_IS_RUSH_TAKEOVER:
        return true;
    case ACTMES_AUTORUSH_BEGIN:
        if (isNerve(GET_NERVE(PowerStar, PowerStarNrvWait))) {
            MR::startSystemSE("SE_SY_STAR_GET");
            MR::stopSoundPlayer("SE_PV_BURN_RUN", 0);
            MR::stopSoundPlayer("SE_PV_NEEDLE_DAMAGE_RUN", 0);
            MR::makeMtxTR(mBaseMtx.toMtxPtr(), *MR::getPlayerPos(), *MR::getPlayerRotate());
            setNerve(GET_NERVE(PowerStar, PowerStarNrvStageClearDemo));
            return true;
        }
        break;
    case ACTMES_UPDATE_BASEMTX:
        MR::setPlayerBaseMtx(mBaseMtx.toMtxPtr());
        return true;
    }

    return false;
}

void PowerStar::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mPowerStarId);
    MR::getPlacementArg1NoInit(rIter, &mIsRiseOnGravity);
    MR::getPlacementArg2NoInit(rIter, &mIsNoPostureFront);
    MR::getPlacementArg4NoInit(rIter, &mIsCameraActorPose);
    mAppearPosition.set(mPosition);
    mInitRotation.set(mRotation);
    MR::registerPowerStar(this, mPowerStarId);
    MR::joinToGroupArray(this, rIter, "パワースター出現ポイントグループ", cAppearPointGroupMax);

    if (mPowerStarId <= 0) {
        return;
    }

    if (MR::hasPowerStarInCurrentStage(mPowerStarId)) {
        mColor = Color_Collected;
    } else if (mIsGrandStar) {
        mColor = Color_Gold;
    } else {
        mColor = getBtpFrameCurrentStage(mPowerStarId);
    }
}

void PowerStar::initModel() {
    initModelManagerWithAnm(getModelName(), nullptr, false);
    bool isCollected = mColor == Color_Collected;

    if (mIsGrandStar) {
        ::setupColorGrandStar(this, isCollected);
        mPowerStarModelObj = MR::createModelObjIndirectMapObj("グランドスターデモモデル", getModelName(), mBaseMtx.toMtxPtr());
        ::setupColorGrandStar(mPowerStarModelObj, isCollected);
    } else {
        ::setupColor(this, mColor);
        mPowerStarModelObj = MR::createModelObjNoSilhouettedMapObj("パワースターデモモデル", getModelName(), mBaseMtx.toMtxPtr());
        ::setupColor(mPowerStarModelObj, mColor);
    }

    MR::invalidateClipping(mPowerStarModelObj);
    mPowerStarModelObj->makeActorDead();

    if (MR::isPowerStarGetDemoWithLuigiCurrentGalaxy()) {
        mLuigiNPC = MR::createModelObjNpc("ルイージデモモデル", "LuigiNPC", mBaseMtx.toMtxPtr());

        MR::initLightCtrl(mLuigiNPC);
        MR::invalidateClipping(mLuigiNPC);
        mLuigiNPC->makeActorDead();
    }

    MR::tryCreateMirrorActor(this, getModelName());
}

void PowerStar::initSensorGrandStar() {
    initHitSensor(cGrandStarSensorCount);

    for (s32 i = 0; i < cGrandStarSensorCount; i++) {
        const GrandStarSensor& rSensor = cGrandStarSensors[i];
        MR::addHitSensorMtx(this, rSensor.mName, ATYPE_POWER_STAR_BIND, cSensorGroupSize, rSensor.mRadius, getBaseMtx(),
                            Vec3f(rSensor.mX, rSensor.mY, rSensor.mZ));
    }
}

void PowerStar::initShadow(const TableIter& rIter) {
    if (mIsGrandStar) {
        MR::initShadowFromCSV(this, "Shadow");
        return;
    }

    bool isCylinder = false;
    MR::getPlacementArg3NoInit(rIter, &isCylinder);
    initShadowPowerStar(this, isCylinder);
}

void PowerStar::initPosture() {
    MR::calcGravity(this, mAppearPosition);
    Pos3f mtx;
    MR::makeMtxRotate(mtx, mInitRotation);

    if (!mIsNoPostureFront) {
        Vec3f zDir;
        mtx.getZDir(zDir);

        Vec3f up = -mGravity;
        if (!MR::isSameDirection(up, zDir)) {
            MR::makeMtxUpFront(&mPostureMtx, up, zDir);
        } else {
            MR::makeMtxUpNoSupport(&mPostureMtx, up);
        }
    } else {
        mPostureMtx.set(mtx);
    }

    mPostureMtx.zeroTrans();
    mRotation.y = 0.0f;
}

void PowerStar::endAppearDemo() {
    if (!mIsForceAppear) {
        MR::endDemo(this, ::cAppearDemoName);
    }

    MR::moveVolumeStageBGM(1.0f, cBgmFadeInSteps);
    MR::moveVolumeSubBGM(1.0f, cBgmFadeInSteps);
    setNerve(GET_NERVE(PowerStar, PowerStarNrvWait));
}

PowerStarAppearPoint* PowerStar::getNearestAppearPoint(const Vec3f& rPos) const {
    LiveActorGroup* pGroup = MR::getGroupFromArray(this);
    f32 minDist = FLOAT_MAX;
    PowerStarAppearPoint* pNearestAppearPoint = nullptr;

    for (int i = 0; i < pGroup->getObjNum(); i++) {
        LiveActor* pActor = pGroup->getActor(i);

        if (pActor == this) {
            continue;
        }

        f32 dist = rPos.distance(pActor->mPosition);

        if (dist < minDist) {
            pNearestAppearPoint = static_cast< PowerStarAppearPoint* >(pActor);
            minDist = dist;
        }
    }

    return pNearestAppearPoint;
}

LiveActor* PowerStar::getAppearCameraActor() {
    if (mCameraActor == nullptr) {
        return this;
    }

    return mCameraActor;
}

ActorCameraInfo* PowerStar::getAppearCameraInfo() const {
    if (mCameraActor == nullptr) {
        return mCameraInfo;
    }

    return mCameraActor->mCameraInfo;
}

void PowerStar::requestAppearOrWait() {
    if (MR::isDemoCast(this, nullptr)) {
        setNerve(GET_NERVE(PowerStar, PowerStarNrvWeak));
    } else if (!mIsInDemo) {
        setNerve(GET_NERVE(PowerStar, PowerStarNrvWait));
    } else if (!mIsForceAppear) {
        requestAppear();
    } else {
        MR::invalidateClipping(this);
        MR::requestMovementOn(this);

        if (MR::isStageKoopaVs()) {
            setNerve(GET_NERVE(PowerStar, PowerStarNrvAppearDemoKoopa));
        } else {
            setNerve(GET_NERVE(PowerStar, PowerStarNrvAppearDemoRise));
        }
    }
}

void PowerStar::calcAppearDemoRiseTrans(Vec3f* pOutTrans, f32 height) const {
    if (mIsRiseOnGravity) {
        MR::calcGravityVector(this, mInitPosition, pOutTrans, nullptr, 0);
        pOutTrans->x = pOutTrans->x * -height;
        pOutTrans->y = pOutTrans->y * -height;
        pOutTrans->z = pOutTrans->z * -height;
    } else {
        Vec3f rise(0.0f, height, 0.0f);
        mPostureMtx.mult(rise, *pOutTrans);
    }

    pOutTrans->add(mInitPosition);
}

void PowerStar::processWait(f32 rotateSpeed) {
    mRotation.y = MR::repeat(mRotation.y + rotateSpeed, 0.0f, cNum_processWait_f1);

    if (mIsGrandStar) {
        // Hidden inside the near distance, shown only a margin beyond it, so a camera at the line does not toggle the model and its effects.
        if (MR::changeShowModelFlagSyncNearClipping(this, MR::isHiddenModel(this) ? cGrandStarNearClip + cGrandStarNearClipMargin : cGrandStarNearClip)) {
            MR::onDrawEffect(this);
        } else {
            MR::offDrawEffect(this);
        }
    }

    if (MR::isGalaxyGhostCometAppearInCurrentStage()) {
        // Shown at the clip distance, hidden only a margin past it: one test for both deleted and made the light again
        // at every crossing of the edge.
        const f32 clipDistance = MR::isHiddenModel(this) ? cCometClipDistance : cCometClipDistance + cCometClipLeaveMargin;
        if (MR::isJudgedToClipFrustum100m(mPosition, clipDistance)) {
            MR::hideModelAndOnCalcAnimIfShown(this);
            MR::forceDeleteEffect(this, "Light");
        } else {
            MR::showModelIfHidden(this);
            MR::emitEffect(this, "Light");
        }
    }

    if (!mIsWeak) {
        startExistSound();
    }
}

void PowerStar::startExistSound() {
    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        MR::startLevelSound(this, "SE_OJ_LV_POW_STAR_EXIST_W");
    } else {
        MR::startLevelSound(this, "SE_OJ_LV_POW_STAR_EXIST");
    }
}

f32 PowerStar::getWaitRotateSpeed() const {
    return mIsGrandStar ? cGrandStarRotateSpeed : cPowerStarRotateSpeed;
}

const char* PowerStar::getModelName() const {
    return mIsGrandStar ? "GrandStar" : "PowerStar";
}

const char* PowerStar::getGetDemoName() const {
    return mIsGrandStar ? "GrandStarGet" : "PowerStarGet";
}

void PowerStar::exeWaitStartAppear() {
}

void PowerStar::exeAppearDemoRise() {
    if (MR::isFirstStep(this)) {
        MR::showModelIfHidden(this);
        MR::moveVolumeStageBGM(0.0f, cBgmFadeOutSteps);
        MR::moveVolumeSubBGM(0.0f, cBgmFadeOutSteps);

        MR::startActorCameraTargetOther(getAppearCameraActor(), getAppearCameraInfo(), CameraTargetArg(this), 0);
        mPosition.set(mInitPosition);
        MR::emitEffect(this, "Light");
    }

    startExistSound();

    f32 height = MR::calcNerveEaseOutValue(this, cRiseEaseSteps, 0.0f, cRiseHeight);
    calcAppearDemoRiseTrans(&mPosition, height);
    mRotation.y = MR::repeat(mRotation.y + cRiseSpinSpeed, 0.0f, cNum_exeAppearDemoRise_f1);

    if (MR::isStep(this, cRiseSteps)) {
        setNerve(GET_NERVE(PowerStar, PowerStarNrvAppearDemoMove));
    }
}

void PowerStar::exeAppearDemoMove() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_SY_POW_STAR_APPEAR");
    }

    bool isMoving = MR::isLessStep(this, cMoveSteps);

    if (isMoving) {
        Vec3f risePos;
        f32 rate = getNerveStep() / static_cast< f32 >(cMoveSteps);
        calcAppearDemoRiseTrans(&risePos, cRiseHeight);
        mPosition.lerp(risePos, mAppearPosition, rate);

        Vec3f riseH;
        MR::vecKillElement(risePos, mGravity, &riseH);

        Vec3f appearH;
        MR::vecKillElement(mAppearPosition, mGravity, &appearH);

        f32 halfDistance = riseH.distance(appearH) * cNum_exeAppearDemoMove_f1;
        f32 arcHeight = MR::clamp(halfDistance, cArcHeightMin, cArcHeightMax);

        Vec3f arc(0.0f, 0.0f, 0.0f);
        arc.y = MR::sin(rate * PI) * arcHeight;
        mPostureMtx.mult(arc, arc);
        mPosition.add(arc);
    } else {
        startExistSound();
    }

    if (MR::isStep(this, cMoveSteps)) {
        mPosition.set(mAppearPosition);

        if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::startSound(this, "SE_OJ_POW_STAR_MOVE_END_W");
        } else {
            MR::startSound(this, "SE_OJ_POW_STAR_MOVE_END");
        }
    }

    f32 spinSpeed = isMoving ? cMoveSpinSpeed : MR::getEaseOutValue(getNerveStep() - cMoveSteps, cMoveSpinSpeed, getWaitRotateSpeed(), cSpinDownSteps);
    mRotation.y = MR::repeat(mRotation.y + spinSpeed, 0.0f, cNum_exeAppearDemoMove_f2);

    if (MR::isStep(this, cMoveEndStep)) {
        MR::resetCameraLocalOffset();

        MR::endActorCamera(getAppearCameraActor(), getAppearCameraInfo(), false, 0);
        endAppearDemo();
    }
}

void PowerStar::exeAppearDemoKoopa() {
    const char* pDemoName = ::getKoopaDemoName();
    bool isVs3 = MR::isStageKoopaVs3();

    if (MR::isFirstStep(this)) {
        MR::showModelIfHidden(this);
        MR::moveVolumeStageBGM(0.0f, cBgmFadeOutSteps);
        MR::moveVolumeSubBGM(0.0f, cBgmFadeOutSteps);
        MR::startAnimCameraTargetSelf(mPowerStarModelObj, mCameraInfo, pDemoName, 0, 1.0f);
        MR::hideModelAndOnCalcAnimIfShown(this);
        MR::forceDeleteEffect(this, "Light");
        mBaseMtx.set(mPostureMtx);
        mBaseMtx.setTrans(mInitPosition);
        mPowerStarModelObj->appear();
        MR::requestMovementOn(mPowerStarModelObj);
        MR::startBck(mPowerStarModelObj, pDemoName);
        mPlayerMtx.set(MR::getPlayerBaseMtx());
        MR::setPlayerBaseMtx(mBaseMtx);
    }

    if (MR::isStep(this, cStep_exeAppearDemoKoopa_1)) {
        MR::startBckPlayer(pDemoName, 0);
    }

    if (isVs3) {
        if (MR::isStep(this, cKoopaVs3BgmStep)) {
            MR::startSubBGM("BGM_KOOPA_GRAND_STAR_3", false);
        }

        if (MR::isStep(this, static_cast< s32 >(MR::getBckFrameMax(mPowerStarModelObj)) - cKoopaVs3StageBgmLead)) {
            MR::startStageBGM("MBGM_STAR_EXIST_2", false);
        }
    } else if (MR::isStep(this, cKoopaBgmStep)) {
        MR::startSubBGM("BGM_KOOPA_GRAND_STAR_1", false);
    }

    if (MR::isBckStopped(mPowerStarModelObj)) {
        MR::setPlayerBaseMtx(mPlayerMtx);
        mPosition.set(mAppearPosition);

        MR::showModelIfHidden(this);
        mPowerStarModelObj->kill();

        MR::endAnimCamera(mPowerStarModelObj, mCameraInfo, pDemoName, 0, true);
        endAppearDemo();

        if (isVs3) {
            MR::overlayWithPreviousScreen(2);
        }
    }
}

void PowerStar::exeWait() {
    if (MR::isFirstStep(this)) {
        if (!MR::isGalaxyGhostCometAppearInCurrentStage()) {
            MR::validateClipping(this);
        }

        MR::validateHitSensors(this);
        MR::emitEffect(this, "Light");
    }

    processWait(getWaitRotateSpeed());
}

void PowerStar::exeWeak() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::validateClipping(this);

        if (mColor != Color_Collected) {
            MR::startBrkAndSetFrameAndStop(this, "Recover", 0.0f);
        }
    }

    mIsWeak = true;

    processWait(cWeakRotateSpeed);
}

void PowerStar::exeWeakNoRotate() {
    if (MR::isFirstStep(this)) {
        mRotation.y = cWeakNoRotateAngle;
    }

    mIsWeak = true;

    processWait(0.0f);
}

void PowerStar::exeWeakToWait() {
    if (MR::isFirstStep(this) && mColor != Color_Collected) {
        MR::setBrkRate(this, cNum_exeWeakToWait_setBrkRate_1);
    }

    mIsWeak = false;

    processWait(MR::calcNerveEaseInValue(this, cWeakToWaitSteps, cWeakRotateSpeed, getWaitRotateSpeed()));
    MR::setNerveAtStep(this, GET_NERVE(PowerStar, PowerStarNrvWait), cWeakToWaitSteps);
}

void PowerStar::exeStageClearDemo() {
    if (MR::isFirstStep(this)) {
        if (mIsGrandStar) {
            MR::requestGrandStarGetDemo();
        } else {
            MR::requestPowerStarGetDemo();
        }

        if (mPowerStarId > 0) {
            MR::sendStageResultSequenceParam(mPowerStarId);
        }

        MR::invalidateClipping(this);
        MR::startAnimCameraTargetSelf(mPowerStarModelObj, mCameraInfo, getGetDemoName(), 0, 1.0f);
    }

    if (MR::isStep(this, cStep_exeStageClearDemo_1)) {
        if (mIsCameraActorPose && mCameraActor != nullptr) {
            MR::makeMtxTR(mBaseMtx, mCameraActor);
            mBaseMtx.zeroTrans();
        } else {
            mBaseMtx.set(mPostureMtx);
        }

        MR::forceDeleteEffect(this, "Light");
        MR::hideModelIfShown(this);
        MR::startBckPlayer(getGetDemoName(), 0);
        mBaseMtx.setTrans(mPosition);
        mPowerStarModelObj->makeActorAppeared();
        MR::startBck(mPowerStarModelObj, getGetDemoName());

        if (mLuigiNPC != nullptr && MR::isPowerStarGetDemoWithLuigiCurrentGalaxyAndScenario(mPowerStarId)) {
            mLuigiNPC->makeActorAppeared();

            MR::startBck(mLuigiNPC, "PowerStarGet");
            MR::startBva(mLuigiNPC, "PowerStarGet");
        }

        MR::startStarPointerModePowerStarGetDemo(this);
        MR::sendMsgToAllLiveActor(ACTMES_START_POWER_STAR_GET, this);
    }

    if (mIsGrandStar && MR::isStageKoopaVs3() && MR::isStep(this, cKoopaVs3ClearStep)) {
        MR::startBckPlayer(::cStageClearAnimNameKoopaVs3);
        MR::startBck(mPowerStarModelObj, ::cStageClearAnimNameKoopaVs3);
        MR::startAnimCameraTargetSelf(mPowerStarModelObj, mCameraInfo, ::cStageClearAnimNameKoopaVs3, 1, 1.0f);
    }
}
