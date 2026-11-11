#include "Game/MapObj/AstroMapObj.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/AstroMapObjFunction.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Screen/GalaxyNamePlate.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 cAstroPathAppearStep = NebulaFrameClock::stepsOfMilliseconds(1500);

    // Doors: a Close and an Open collision, swapped when the door opens.
    bool isDoorObj(const char* pObjName) {
        return MR::isEqualString(pObjName, "AstroDomeEntrance") || MR::isEqualString(pObjName, "AstroLibrary");
    }

    bool isRotatorObj(const char* pObjName) {
        return MR::isEqualString(pObjName, "AstroRotateStepA") || MR::isEqualString(pObjName, "AstroRotateStepB") ||
               MR::isEqualString(pObjName, "AstroDecoratePartsA");
    }

    bool isNamePlateObj(const char* pObjName) {
        return MR::isEqualString(pObjName, "AstroDomeEntrance") || MR::isEqualString(pObjName, "AstroLibrary") ||
               MR::isEqualString(pObjName, "AstroChildRoom") || MR::isEqualString(pObjName, "AstroParking");
    }

    // The steps' engines hum while they run.
    const char* getEngineSound(const char* pObjName) {
        if (MR::isEqualString(pObjName, "AstroRotateStepA")) {
            return "SE_OJ_LV_ASTRO_ENGINE_1";
        }

        if (MR::isEqualString(pObjName, "AstroRotateStepB")) {
            return "SE_OJ_LV_ASTRO_ENGINE_2";
        }

        return nullptr;
    }
};  // namespace

namespace NrvAstroMapObj {
    NEW_NERVE(AstroMapObjNrvDead, AstroMapObj, Wait);
    NEW_NERVE(AstroMapObjNrvAlive, AstroMapObj, Wait);
    NEW_NERVE(AstroMapObjNrvAliveAfterDemo, AstroMapObj, Wait);
    NEW_NERVE(AstroMapObjNrvRevival, AstroMapObj, Revival);
    NEW_NERVE(AstroMapObjNrvOpen, AstroMapObj, Open);
};  // namespace NrvAstroMapObj

AstroMapObj::AstroMapObj(const char* pName)
    : MapObjActor(pName), mNamePlateTable(nullptr), mNamePlate(nullptr), mObjName(nullptr), mCloseCollision(nullptr), mOpenCollision(nullptr),
      mDomeId(-1), mEngineSound(nullptr), mRevivalSound(nullptr), mIsDoor(false), mIsDomeEntrance(false), mIsStarPlate(false),
      mIsKitchenEntrance(false) {
    lumacore::setClass(this, lumacore::kClassId< AstroMapObj >);
}

// A part of the observatory, dead (a dark shape) until its dome's revival demo, then alive; doors open in a later demo.
// The placement name picks the model through the dome id (arg 0).
void AstroMapObj::init(const TableIter& rIter) {
    MR::getObjectName(&mObjName, rIter);
    MapObjActorInitInfo mapObjInitInfo = MapObjActorInitInfo();
    mDomeId = AstroMapObjFunction::getDomeIdFromArg0(rIter);
    mapObjInitInfo.setupDefaultPos();
    mapObjInitInfo.setupModelName(AstroMapObjFunction::getModelName(mObjName, mDomeId));
    mapObjInitInfo.setupConnectToScene();
    mapObjInitInfo.setupFarClipping(-1.0f);
    mapObjInitInfo.setupEffect(mObjName);
    mapObjInitInfo.setupSound(4);
    mapObjInitInfo.setupNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvDead));

    mIsDoor = ::isDoorObj(mObjName);

    if (::isRotatorObj(mObjName)) {
        mapObjInitInfo.setupRotator();
    }

    if (mIsDoor) {
        mapObjInitInfo.setupHitSensor();
    }

    initialize(rIter, mapObjInitInfo);

    // mObjectName is the model name from here on.
    mEngineSound = ::getEngineSound(mObjName);
    mIsDomeEntrance = MR::isEqualString(mObjName, "AstroDomeEntrance");
    mIsStarPlate = MR::isEqualString(mObjName, "AstroStarPlate");
    mIsKitchenEntrance = isObjectName("AstroDomeEntranceKitchen");

    if (mIsStarPlate) {
        mRevivalSound = "SE_OJ_ASTRO_PLATE_REVIVAL";
    } else if (MR::isEqualString(mObjectName, "AstroRotateStepA") || MR::isEqualString(mObjectName, "AstroRotateStepB")) {
        mRevivalSound = "SE_OJ_ASTRO_STEP_REVIVAL";
    }

    selectNrvWait();

    if (MR::isDemoCast(this, nullptr)) {
        MR::registerDemoActionFunctor(this, MR::Functor(this, &AstroMapObj::startDemo), nullptr);
    }

    AstroMapObjFunction::tryRegisterMultiDemoAndFunction(mObjName, this, rIter, MR::Functor(this, &AstroMapObj::startDemo));
    MR::registerDemoSimpleCastAll(this);

    if (::isNamePlateObj(mObjName)) {
        mNamePlateTable = AstroMapObjFunction::createAstroNamePlateParser();
        mNamePlate = new GalaxyNamePlate(nullptr, true);
        mNamePlate->initWithoutIter();
    }

    if (mIsDoor) {
        mCloseCollision = MR::createCollisionPartsFromLiveActor(this, "Close", getSensor("body"), MR::CollisionScaleType_Default);
        mOpenCollision = MR::createCollisionPartsFromLiveActor(this, "Open", getSensor("body"), MR::CollisionScaleType_Default);
    }

    AstroMapObjFunction::initilizePeculiar(mObjName, this, rIter);
}

void AstroMapObj::exeWait() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvDead))) {
            setStateDead();
        } else if (isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAlive))) {
            setStateAlive();
        }
    }

    if (mEngineSound != nullptr && isPlayMachineSE() &&
        (isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAlive)) || isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAliveAfterDemo)))) {
        MR::startLevelSound(this, mEngineSound);
    }
}

void AstroMapObj::exeRevival() {
    if (MR::isFirstStep(this)) {
        tryStartAllAnimAndEffect("Revival");

        if (mRevivalSound != nullptr) {
            MR::startSound(this, mRevivalSound);
        }
    }

    if (mEngineSound != nullptr) {
        MR::startLevelSound(this, mEngineSound);
    } else if (mIsStarPlate && MR::isGreaterStep(this, cAstroPathAppearStep)) {
        MR::startAtmosphereLevelSE("SE_AT_LV_ASTRO_PATH_APPEAR");
    }

    if (isEndRevival()) {
        setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAlive));
    }
}

void AstroMapObj::exeOpen() {
    if (MR::isFirstStep(this)) {
        tryStartAllAnimAndEffect("Open");

        if (mIsDomeEntrance) {
            MR::startSound(this, "SE_OJ_ASTRO_ENTRANCE_OPEN");
        }
    }

    if (MR::isBtkStopped(this)) {
        // Only a door has the two collisions (an open demo cast on another part has none to swap).
        if (mOpenCollision != nullptr && mCloseCollision != nullptr) {
            MR::validateCollisionParts(mOpenCollision);
            MR::invalidateCollisionParts(mCloseCollision);
        }

        setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAliveAfterDemo));
    }
}

void AstroMapObj::control() {
    MapObjActor::control();

    if (mNamePlate != nullptr) {
        AstroMapObjFunction::trySetAndShowNamePlate(mNamePlate, mNamePlateTable, mPosition, mObjName, mDomeId, mIsDoor);
    }
}

void AstroMapObj::setStateDead() {
    setStateDoorOpenOrClose();
    MR::tryStartAllAnim(this, "Revival");
    MR::setAllAnimFrameAndStop(this, "Revival", 0.0f);
    setStateDoorOpenOrClose();

    if (mRotator != nullptr) {
        endMapPartsFunctions();
    }
}

void AstroMapObj::setStateAlive() {
    MR::tryStartAllAnim(this, "Revival");
    MR::setAllAnimFrameAtEnd(this, "Revival");
    setStateDoorOpenOrClose();
    tryStartAllAnimAndEffect("AliveWait");

    if (mRotator != nullptr) {
        startMapPartsFunctions();
    }
}

// Shows a door as saved: open, or shut (its open animation at the start once alive). A door the demo just opened keeps
// its state.
void AstroMapObj::setStateDoorOpenOrClose() {
    if (isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAliveAfterDemo)) || !mIsDoor) {
        return;
    }

    if (AstroMapObjFunction::isAlreadyOpen(mObjName, mDomeId)) {
        MR::tryStartAllAnim(this, "Open");
        MR::setAllAnimFrameAtEnd(this, "Open");
        MR::validateCollisionParts(mOpenCollision);
        MR::invalidateCollisionParts(mCloseCollision);
        return;
    }

    if (!isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvDead))) {
        MR::tryStartAllAnim(this, "Open");
        MR::setAllAnimFrameAndStop(this, "Open", 0.0f);
    }

    MR::invalidateCollisionParts(mOpenCollision);
    MR::validateCollisionParts(mCloseCollision);
}

void AstroMapObj::selectNrvWait() {
    if (AstroMapObjFunction::isAlreadyRevival(mObjName, mDomeId)) {
        setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAlive));
    } else {
        setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvDead));
    }
}

void AstroMapObj::startDemo() {
    if (isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvDead))) {
        if (AstroMapObjFunction::isEnableRevival(mObjName, mDomeId)) {
            setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvRevival));
        }
    } else if (AstroMapObjFunction::isEnableOpen(mObjName, mDomeId)) {
        setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvOpen));
    }
}

void AstroMapObj::tryStartAllAnimAndEffect(const char* pAnimName) {
    if (mIsKitchenEntrance && MR::isEqualString(pAnimName, "AliveWait")) {
        MR::emitEffect(this, "KitchenSmoke");
    }

    if (MR::isRegisteredEffect(this, pAnimName)) {
        MR::emitEffect(this, pAnimName);
    }

    MR::tryStartAllAnim(this, pAnimName);
}

bool AstroMapObj::isEndRevival() const {
    if (MR::isExistBck(this, 0)) {
        return MR::isBckStopped(this);
    }

    if (MR::isExistBrk(this, 0)) {
        return MR::isBrkStopped(this);
    }

    return true;
}

// Quiet in the storybook part of Rosalina's explanation and while a star (or a grand star after the first) comes back.
bool AstroMapObj::isPlayMachineSE() const {
    if (MR::isDemoActive("ロゼッタ状況説明デモ") && MR::isDemoPartActive("状況説明[絵本表示]")) {
        return false;
    }

    if (MR::isDemoActive("グランドスター帰還[２回目以降]")) {
        return false;
    }

    return !MR::isDemoActive("パワースター帰還");
}

AstroSimpleObj::AstroSimpleObj(const char* pName) : SimpleMapObjFarMax(pName), mNamePlateTable(nullptr), mNamePlate(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< AstroSimpleObj >);
}

void AstroSimpleObj::init(const TableIter& rIter) {
    SimpleMapObjFarMax::init(rIter);
    mNamePlateTable = AstroMapObjFunction::createAstroNamePlateParser();
    mNamePlate = new GalaxyNamePlate(nullptr, true);
    mNamePlate->initWithoutIter();
}

void AstroSimpleObj::control() {
    MapObjActor::control();

    if (mNamePlate != nullptr) {
        AstroMapObjFunction::trySetAndShowNamePlate(mNamePlate, mNamePlateTable, mPosition, mObjectName, -1, false);
    }
}
