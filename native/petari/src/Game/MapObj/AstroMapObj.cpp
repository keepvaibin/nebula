#include "Game/MapObj/AstroMapObj.hpp"
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

namespace {
    s32 sAstroPathAppearStep = 90;
};  // namespace

namespace {
    bool checkStrings(const char* arg1) {
        return MR::isEqualString(arg1, "AstroDomeEntrance") || MR::isEqualString(arg1, "AstroLibrary");
    }

    bool checkOtherStrings(const char* arg1) {
        return MR::isEqualString(arg1, "AstroRotateStepA") || MR::isEqualString(arg1, "AstroRotateStepB") ||
               MR::isEqualString(arg1, "AstroDecoratePartsA");
    }

    bool moreInlines(const char* arg1) {
        return MR::isEqualString(arg1, "AstroDomeEntrance") || MR::isEqualString(arg1, "AstroLibrary") || MR::isEqualString(arg1, "AstroChildRoom") ||
               MR::isEqualString(arg1, "AstroParking");
    }
};  // namespace

namespace NrvAstroMapObj {
    NEW_NERVE(AstroMapObjNrvDead, AstroMapObj, Wait);
    NEW_NERVE(AstroMapObjNrvAlive, AstroMapObj, Wait);
    NEW_NERVE(AstroMapObjNrvAliveAfterDemo, AstroMapObj, Wait);
    NEW_NERVE(AstroMapObjNrvRevival, AstroMapObj, Revival);
    NEW_NERVE(AstroMapObjNrvOpen, AstroMapObj, Open);
};  // namespace NrvAstroMapObj

AstroMapObj::AstroMapObj(const char* pName) : MapObjActor(pName) {
    mNamePlateTable = 0;
    mNamePlate = 0;
    mObjName = 0;
    mCloseCollision = 0;
    mOpenCollision = 0;
    mDomeId = -1;
}

void AstroMapObj::init(const TableIter& rIter) {  // Pain
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

    if (::checkOtherStrings(mObjName)) {
        mapObjInitInfo.setupRotator();
    }

    if (::checkStrings(mObjName)) {
        mapObjInitInfo.setupHitSensor();
    }

    initialize(rIter, mapObjInitInfo);
    selectNrvWait();

    if (MR::isDemoCast(this, nullptr)) {
        MR::registerDemoActionFunctor(this, MR::Functor(this, &AstroMapObj::startDemo), nullptr);
    }

    AstroMapObjFunction::tryRegisterMultiDemoAndFunction(mObjName, this, rIter, MR::Functor(this, &AstroMapObj::startDemo));
    MR::registerDemoSimpleCastAll(this);

    if (::moreInlines(mObjName)) {
        mNamePlateTable = AstroMapObjFunction::createAstroNamePlateParser();
        mNamePlate = new GalaxyNamePlate(nullptr, true);
        mNamePlate->initWithoutIter();
    }

    if (::checkStrings(mObjName)) {
        mCloseCollision = MR::createCollisionPartsFromLiveActor(this, "Close", getSensor("body"), MR::CollisionScaleType_Unk2);
        mOpenCollision = MR::createCollisionPartsFromLiveActor(this, "Open", getSensor("body"), MR::CollisionScaleType_Unk2);
    }

    AstroMapObjFunction::initilizePeculiar(mObjName, this, rIter);
}

void AstroMapObj::exeWait() {
    if (MR::isFirstStep(this)) {
        if (LiveActor::isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvDead))) {
            setStateDead();
        } else if (LiveActor::isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAlive))) {
            setStateAlive();
        }
    }
    if (AstroMapObj::isPlayMachineSE() &&
        (isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAlive)) || isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAliveAfterDemo)))) {
        if (MR::isEqualString(mObjName, "AstroRotateStepA")) {
            MR::startLevelSound(this, "SE_OJ_LV_ASTRO_ENGINE_1");
        } else if (MR::isEqualString(mObjName, "AstroRotateStepB")) {
            MR::startLevelSound(this, "SE_OJ_LV_ASTRO_ENGINE_2");
        }
    }
}

void AstroMapObj::exeRevival() {
    if (MR::isFirstStep(this)) {
        tryStartAllAnimAndEffect("Revival");
        if (MR::isEqualString(mObjName, "AstroStarPlate")) {
            MR::startSound(this, "SE_OJ_ASTRO_PLATE_REVIVAL");
        } else if (MR::isEqualString(mObjectName, "AstroRotateStepA")) {
            MR::startSound(this, "SE_OJ_ASTRO_STEP_REVIVAL");
        } else if (MR::isEqualString(mObjectName, "AstroRotateStepB")) {
            MR::startSound(this, "SE_OJ_ASTRO_STEP_REVIVAL");
        }
    }
    if (MR::isEqualString(mObjName, "AstroRotateStepA")) {
        MR::startLevelSound(this, "SE_OJ_LV_ASTRO_ENGINE_1");
    } else if (MR::isEqualString(mObjName, "AstroRotateStepB")) {
        MR::startLevelSound(this, "SE_OJ_LV_ASTRO_ENGINE_2");
    } else if (MR::isEqualString(mObjName, "AstroStarPlate") && MR::isGreaterStep(this, ::sAstroPathAppearStep)) {
        MR::startAtmosphereLevelSE("SE_AT_LV_ASTRO_PATH_APPEAR");
    }
    if (AstroMapObj::isEndRevival()) {
        setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAlive));
    }
}

void AstroMapObj::exeOpen() {
    if (MR::isFirstStep(this)) {
        tryStartAllAnimAndEffect("Open");
        if (MR::isEqualString(mObjName, "AstroDomeEntrance")) {
            MR::startSound(this, "SE_OJ_ASTRO_ENTRANCE_OPEN");
        }
    }
    if (MR::isBtkStopped(this)) {
        MR::validateCollisionParts(mOpenCollision);
        MR::invalidateCollisionParts(mCloseCollision);
        setNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAliveAfterDemo));
    }
}

void AstroMapObj::control() {
    MapObjActor::control();
    if (mNamePlate) {
        AstroMapObjFunction::trySetAndShowNamePlate(mNamePlate, mNamePlateTable, mPosition, mObjName, mDomeId, ::checkStrings(mObjName));
    }
}

bool AstroMapObj::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return false;
}

void AstroMapObj::setStateDead() {
    setStateDoorOpenOrClose();
    MR::tryStartAllAnim(this, "Revival");
    MR::setAllAnimFrameAndStop(this, "Revival", 0.0f);
    setStateDoorOpenOrClose();
    if (mRotator) {
        endMapPartsFunctions();
    }
}

void AstroMapObj::setStateAlive() {
    MR::tryStartAllAnim(this, "Revival");
    MR::setAllAnimFrameAtEnd(this, "Revival");
    setStateDoorOpenOrClose();
    tryStartAllAnimAndEffect("AliveWait");
    if (mRotator) {
        startMapPartsFunctions();
    }
}

void AstroMapObj::setStateDoorOpenOrClose() {
    if (!isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvAliveAfterDemo))) {
        if (::checkStrings(mObjName)) {
            if (AstroMapObjFunction::isAlreadyOpen(mObjName, mDomeId)) {
                MR::tryStartAllAnim(this, "Open");
                MR::setAllAnimFrameAtEnd(this, "Open");
                MR::validateCollisionParts(mOpenCollision);
                MR::invalidateCollisionParts(mCloseCollision);
            } else {
                if (!LiveActor::isNerve(GET_NERVE(AstroMapObj, AstroMapObjNrvDead))) {
                    MR::tryStartAllAnim(this, "Open");
                    MR::setAllAnimFrameAndStop(this, "Open", 0.0f);
                }
                MR::invalidateCollisionParts(mOpenCollision);
                MR::validateCollisionParts(mCloseCollision);
            }
        }
    }
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

void AstroMapObj::tryStartAllAnimAndEffect(const char* v1) {
    if (MapObjActor::isObjectName("AstroDomeEntranceKitchen") && MR::isEqualString(v1, "AliveWait")) {
        MR::emitEffect(this, "KitchenSmoke");
    }
    if (MR::isRegisteredEffect(this, v1)) {
        MR::emitEffect(this, v1);
    }
    MR::tryStartAllAnim(this, v1);
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

bool AstroMapObj::isPlayMachineSE() const {
    if (MR::isDemoActive("ロゼッタ状況説明デモ") && MR::isDemoPartActive("状況説明[絵本表示]")) {
        return false;
    }
    if (MR::isDemoActive("グランドスター帰還[２回目以降]")) {
        return false;
    }
    return MR::isDemoActive("パワースター帰還") == 0;
}

AstroSimpleObj::AstroSimpleObj(const char* pName) : SimpleMapObjFarMax(pName) {
    mNamePlateTable = 0;
    mNamePlate = 0;
}

void AstroSimpleObj::init(const TableIter& rIter) {
    SimpleMapObjFarMax::init(rIter);
    mNamePlateTable = AstroMapObjFunction::createAstroNamePlateParser();
    mNamePlate = new GalaxyNamePlate(0, 1);
    mNamePlate->initWithoutIter();
}

void AstroSimpleObj::control() {
    MapObjActor::control();
    if (mNamePlate) {
        AstroMapObjFunction::trySetAndShowNamePlate(mNamePlate, mNamePlateTable, mPosition, mObjectName, -1, false);
    }
}
