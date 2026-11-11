#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/MapObj/MapObjActor.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setupInitInfoTypical_f1, "setupInitInfoTypical.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupInitInfoTypical_f2, "setupInitInfoTypical.f2", 200.0f);

MapObjActorInitInfo::MapObjActorInitInfo()
    : mSetDefaultPosition(), mConnectToScene(), mInitBinder(), mHasEffect(), mHasSensors(), mHasShadows(), mCalcGravity(), mUseProjectMapMtx(),
      mInitFur(), mHasRailMover(), mHasRotator(), mHasRailRotator(), mHasSeesaw1AxisRotator(), mHasSeesaw2AxisRotator(), mUseRailPosture(), mUseBaseMtxFollowTarget(), mIsAffectedByScale(),
      mUseMirrorReflection(), mModelName(), mSensorType(), mSensorRadius(), mSensorPosition(gZeroVec), mHasHitSensorCB(), mBinderRadius(),
      mBinderCenterY(), mEffectName(), mSound(), mSoundPos(), mIsProjmapEveryFrame(), mNerve(), mGroupClipping(), mClippingRadius(), mFarClipping(), mLightType(-1),
      mColorChangeArg(-1), mTextureChangeArg(-1), mIsAppearRiddleSE(), mSeesaw1AxisRotatorName(), mSeesaw1AxisRotatorRadius(), mIsMirrorFromModel(), mShadowName(),
      mShadowLength(-1.0f), mBreakModelName(), mDummyChangeTexture(), mInitLightCtrl(), mNoUseLOD() {
}

void MapObjActorInitInfo::setupDefaultPos() {
    mSetDefaultPosition = true;
}

void MapObjActorInitInfo::setupModelName(const char* pName) {
    mModelName = pName;
}

void MapObjActorInitInfo::setupConnectToScene() {
    mConnectToScene = true;
}

void MapObjActorInitInfo::setupBinder(f32 radius, f32 centerY) {
    mInitBinder = true;
    mBinderRadius = radius;
    mBinderCenterY = centerY;
}

void MapObjActorInitInfo::setupEffect(const char* pName = nullptr) {
    mHasEffect = true;
    mEffectName = pName;
}

void MapObjActorInitInfo::setupSound(s32 id) {
    mSound = id;
}

void MapObjActorInitInfo::setupSoundPos(Vec3f* pPos) {
    mSoundPos = pPos;
}

void MapObjActorInitInfo::setupNoAppearRiddleSE() {
    mIsAppearRiddleSE = false;
}

void MapObjActorInitInfo::setupHitSensor() {
    mHasSensors = true;
}

void MapObjActorInitInfo::setupHitSensorCallBack() {
    mHasSensors = true;
    mHasHitSensorCB = true;
}

void MapObjActorInitInfo::setupHitSensorParam(u16 sensorType, f32 radius, const Vec3f& rPosition) {
    mSensorType = sensorType;
    mSensorRadius = radius;
    mSensorPosition.set(rPosition);
}

void MapObjActorInitInfo::setupNerve(const Nerve* pNerve) {
    mNerve = pNerve;
}

void MapObjActorInitInfo::setupShadow(const char* pName = nullptr) {
    mHasShadows = true;
    mShadowName = pName;
}

void MapObjActorInitInfo::setupGroupClipping(s32 id) {
    mGroupClipping = id;
}

void MapObjActorInitInfo::setupClippingRadius(f32 radius) {
    mClippingRadius = radius;
}

void MapObjActorInitInfo::setupFarClipping(f32 distance = -1.0f) {
    mFarClipping = distance;
}

void MapObjActorInitInfo::setupProjmapMtx(bool isEveryFrame) {
    mUseProjectMapMtx = true;
    mIsProjmapEveryFrame = isEveryFrame;
}

void MapObjActorInitInfo::setupRailMover() {
    mHasRailMover = true;
}

void MapObjActorInitInfo::setupRotator() {
    mHasRotator = true;
}

void MapObjActorInitInfo::setupRailRotator() {
    mHasRailRotator = true;
}

void MapObjActorInitInfo::setupSeesaw1AxisRotator(const char* pName, f32 radius) {
    mHasSeesaw1AxisRotator = true;
    mSeesaw1AxisRotatorName = pName;
    mSeesaw1AxisRotatorRadius = radius;
}

void MapObjActorInitInfo::setupRailPosture() {
    mUseRailPosture = true;
}

void MapObjActorInitInfo::setupBaseMtxFollowTarget() {
    mUseBaseMtxFollowTarget = true;
}

void MapObjActorInitInfo::setupAffectedScale() {
    mIsAffectedByScale = true;
}

void MapObjActorInitInfo::setupSeAppear() {
    mIsAppearRiddleSE = true;
}

void MapObjActorInitInfo::setupMirrorReflection(bool isFromModel) {
    mIsMirrorFromModel = isFromModel;
    mUseMirrorReflection = true;
}

void MapObjActorInitInfo::setupPrepareChangeDummyTexture(const char* pName) {
    mDummyChangeTexture = pName;
}

void MapObjActorInitInfo::setupNoUseLodCtrl() {
    mNoUseLOD = true;
}

void MapObjActorUtil::setupInitInfoTypical(MapObjActorInitInfo* pInitInfo, const char* pName) {
    const char* const cNoAppearRiddleSENameList[] = {"AsteroidMoveA",
                                                     "HeavensDoorAppearStepA",
                                                     "HeavensDoorAppearStepAAfter",
                                                     "HeavensDoorInsideRotatePartsA",
                                                     "HeavensDoorInsideRotatePartsB",
                                                     "HeavensDoorInsideRotatePartsC",
                                                     "PeachCastleTownAfterAttack",
                                                     "PeachCastleTownAfterAttack",
                                                     "PeachCastleTownBeforeAttack",
                                                     "PhantomShipPropellerBig",
                                                     "PhantomShipPropellerSmall",
                                                     "StarPieceCluster",
                                                     "RosettaChair",
                                                     "AstroDomeDemoAstroGalaxy",
                                                     "ChallengeBallMoveGroundB",
                                                     "ChallengeBallVanishingRoadA",
                                                     "DarkHopperRotateStepA",
                                                     "DarkHopperPlanetA",
                                                     "PeachCastleTownGate",
                                                     "KoopaShipE",
                                                     "BattleShipElevatorCover",
                                                     "StrongBlock"};
    if (MR::isExistString(pName, cNoAppearRiddleSENameList, ARRAY_SIZE(cNoAppearRiddleSENameList))) {
        pInitInfo->setupNoAppearRiddleSE();
    }

    if (MR::isEqualString("HeavensDoorInsidePlanet", pName)) {
        pInitInfo->setupProjmapMtx(false);
    }

    const char* const cUfoSandObstaclesList[] = {"UFOSandObstacleA", "UFOSandObstacleB", "UFOSandObstacleC"};
    if (MR::isExistString(pName, cUfoSandObstaclesList, ARRAY_SIZE(cUfoSandObstaclesList))) {
        pInitInfo->mShadowName = "Shadow";
    }

    const char* const cKoopaVs2PartsList[] = {"KoopaVS2PartsJoinedMoveStep", "KoopaVS2PartsSquareMoveStepA", "KoopaVS2PartsSquareMoveStepB",
                                              "KoopaVS2PartsNarrowRoad", "KoopaVS2PartsClipAreaDisplayA"};
    if (MR::isExistString(pName, cKoopaVs2PartsList, ARRAY_SIZE(cKoopaVs2PartsList))) {
        pInitInfo->setupHitSensor();
    }

    if (MR::isEqualString("SandUpDownTowerBreakableWallA", pName) || MR::isEqualString("SandUpDownTowerBreakableWallB", pName)) {
        pInitInfo->mBreakModelName = "SandUpDownTowerBreakableWallBreak";
    }

    if (MR::isEqualString("KoopaStatue", pName)) {
        pInitInfo->mInitLightCtrl = true;
        pInitInfo->mLightType = 2;
    }

    if (MR::isEqualString("DangerSignBoard", pName)) {
        pInitInfo->setupHitSensor();
        pInitInfo->setupHitSensorParam(4, cNum_setupInitInfoTypical_f1, Vec3f(0.0f, cNum_setupInitInfoTypical_f2, 0.0f));
    }
}

void MapObjActorUtil::setupInitInfoColorChangeArg0(MapObjActorInitInfo* pInitInfo, const TableIter& rIter) {
    s32 colorChange = -1;
    MR::getPlacementArg0NoInit(rIter, &colorChange);
    pInitInfo->mColorChangeArg = colorChange;
}

void MapObjActorUtil::setupInitInfoTextureChangeArg1(MapObjActorInitInfo* pInitInfo, const TableIter& rIter) {
    s32 textureChange = -1;
    MR::getPlacementArg1NoInit(rIter, &textureChange);
    pInitInfo->mTextureChangeArg = textureChange;
}

void MapObjActorUtil::setupInitInfoShadowLengthArg2(MapObjActorInitInfo* pInitInfo, const TableIter& rIter) {
    f32 shadowLength = -1.0f;
    MR::getPlacementArg2NoInit(rIter, &shadowLength);
    pInitInfo->mShadowLength = shadowLength;
}

void MapObjActorUtil::setupInitInfoSeesaw(MapObjActorInitInfo* pInitInfo, const TableIter& rIter, const char* pName, f32 radius) {
    s32 arg = -1;
    MR::getMapPartsArgRotateAxis(&arg, rIter);

    if (arg == 0) {
        pInitInfo->setupSeesaw1AxisRotator(pName, radius);
    } else {
        pInitInfo->mHasSeesaw2AxisRotator = true;
        pInitInfo->mSeesaw1AxisRotatorName = pName;
        pInitInfo->mSeesaw1AxisRotatorRadius = radius;
    }
}

void MapObjActorUtil::setupInitInfoSimpleMapObj(MapObjActorInitInfo* pInitInfo) {
    pInitInfo->setupDefaultPos();
    pInitInfo->setupConnectToScene();
    pInitInfo->setupEffect();
    pInitInfo->setupSound(4);
    pInitInfo->setupShadow();
    pInitInfo->setupGroupClipping(64);
    pInitInfo->setupSeAppear();
}

void MapObjActorUtil::setupInitInfoPlanet(MapObjActorInitInfo* pInitInfo) {
    pInitInfo->setupDefaultPos();
    pInitInfo->setupConnectToScene();
    pInitInfo->setupEffect();
    pInitInfo->setupFarClipping();
    pInitInfo->setupNoAppearRiddleSE();
}
