#include "Game/MapObj/MiniatureGalaxy.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/SphereSelector.hpp"
#include "Game/MapObj/AstroDomeOrbit.hpp"
#include "Game/MapObj/MiniatureGalaxyHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/GalaxyNamePlate.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const Vec cNamePlateOffset = {0.0f, 1500.0f, 0.0f};
    const f32 cPointingRadius = 2500.0f;
    const f32 cZoomInScale = 1.0f;
    const f32 cZoomOutScale = 0.65f;
    const s32 cZoomFrame = 20;
    const f32 cZoomInReactionFreq = 1.5f;
    const f32 cZoomInReactionAmplitude = -0.08f;
    const f32 cRotateSpeedY = 0.4f;
    // Below the dome centre along the selector's up axis.
    const f32 cShadowDepth = 7000.0f;
    const s32 cOpenDisappearStep = 75;
    const s32 cOpenMiniatureAppearStep = 45;
    const s32 cOpenTotalFrame = 150;
    const u32 cOpenBgmFadeFrame = 30;
    // The star plate's digit textures hold 0..9 in tenths of the V range.
    const s32 cDigitBase = 10;
    const f32 cDigitTexStep = 0.1f;
    const char* const cDemoNameDomeLecture2 = "ドームレクチャー２";
    const char* const cEyeLightEffect = "EyeLight";

    // Indexed by MiniatureGalaxyFunction::calcMiniatureGalaxyIndex.
    const f32 cInitialRotateY[] = {0.0f, 210.0f, 80.0f, 330.0f, 130.0f};

    const char* const cPointedME[] = {
        "ME_ASTRO_DOME_HIT_GALAXY1", "ME_ASTRO_DOME_HIT_GALAXY2", "ME_ASTRO_DOME_HIT_GALAXY3",
        "ME_ASTRO_DOME_HIT_GALAXY4", "ME_ASTRO_DOME_HIT_GALAXY5",
    };
    const char* const cNeedStarME[] = {
        "ME_ASTRO_DOME_HIT_GALAXY_N1", "ME_ASTRO_DOME_HIT_GALAXY_N2", "ME_ASTRO_DOME_HIT_GALAXY_N3",
        "ME_ASTRO_DOME_HIT_GALAXY_N4", "ME_ASTRO_DOME_HIT_GALAXY_N5",
    };

    // Object names are "Mini" + the galaxy name.
    const char* getGalaxyNameFromObjectName(const char* pObjName) {
        return pObjName + strlen("Mini");
    }

    bool isUseKoopaFaceModel(const char* pObjName, const char* pGalaxyName) {
        return MR::isEqualSubString(pObjName, "KoopaBattleVs") || !MR::isAlreadyVisitedStage(pGalaxyName);
    }

    bool isExistGrandStar(const char* pGalaxyName) {
        return MR::makeGalaxyStatusAccessor(pGalaxyName).isExistGrandStar();
    }

    // getRandom(0, n) is below n: the unit draw is at most 1 - 2^-23.
    void startRandomME(const char* const* pNames, s32 count) {
        MR::startSystemME(pNames[MR::getRandom(0, count)]);
    }

    ModelObj* createGalaxyPart(const char* pName, const char* pModelName, MtxPtr pMtx, bool isDeferMaterials) {
        return new ModelObj(pName, pModelName, pMtx, MR::DrawBufferType_NoShadowedMapObj, -2, -2, isDeferMaterials);
    }

    void initGalaxyPart(ModelObj* pObj) {
        pObj->initWithoutIter();
        MR::invalidateClipping(pObj);
        pObj->kill();
    }

    void setDigitTexMtx(ModelObj* pModel, const char* pMaterialName, CoordMatrix* pTexMtx) {
        MR::initMaterialTexMtx(pModel, pMaterialName)->setTexMtx(0, pTexMtx);
    }
}  // namespace

namespace NrvMiniatureGalaxy {
    NEW_NERVE(MiniatureGalaxyNrvWait, MiniatureGalaxy, Wait);
    NEW_NERVE(MiniatureGalaxyNrvSelected, MiniatureGalaxy, Selected);
    NEW_NERVE(MiniatureGalaxyNrvNotSelected, MiniatureGalaxy, NotSelected);
    NEW_NERVE(MiniatureGalaxyNrvConfirmed, MiniatureGalaxy, Confirmed);
    NEW_NERVE(MiniatureGalaxyNrvOpen, MiniatureGalaxy, Open);
};  // namespace NrvMiniatureGalaxy

MiniatureGalaxy::MiniatureGalaxy(const char* pName)
    : LiveActor(pName), mType(-1), mState(MiniatureGalaxyState_Open), mMysteryModel(), mShadowModel(), mSelectModel(), mStarPlateModel(),
      mProjmapEffectMtxSetter(), mObjectName(), mGalaxyName(), mIsZoomOut(true), mZoomStep(::cZoomFrame), mOnesTexMtx(), mTensTexMtx(), mOrbit(),
      mNamePlate() {
    lumacore::setClass(this, lumacore::kClassId< MiniatureGalaxy >);
    mPosMtx.identity();
    mShadowBaseMtx.identity();
}

void MiniatureGalaxy::init(const TableIter& rIter) {
    MR::getObjectName(&mObjectName, rIter);
    mGalaxyName = ::getGalaxyNameFromObjectName(mObjectName);

    MR::initDefaultPos(this, rIter);

    mType = -1;
    MR::getPlacementArg0NoInit(rIter, &mType);

    initModelManagerWithAnm(isUseKoopaFaceModel() ? "MiniKoopaGalaxy" : mObjectName, nullptr, false);

    initEffectKeeper(0, nullptr);
    MR::connectToSceneNoShadowedMapObj(this);

    MR::initStarPointerTarget(this, ::cPointingRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateClipping(this);

    initPartsModel();
    initNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvWait));

    MR::tryRegisterDemoCast(this, rIter);

    MR::registerDemoCast(this, ::cDemoNameDomeLecture2, rIter);
    MR::registerDemoCast(mShadowModel, ::cDemoNameDomeLecture2, rIter);
    MR::registerDemoCast(mMysteryModel, ::cDemoNameDomeLecture2, rIter);
    MR::registerDemoCast(mSelectModel, ::cDemoNameDomeLecture2, rIter);

    if (mStarPlateModel != nullptr) {
        MR::registerDemoCast(mStarPlateModel, ::cDemoNameDomeLecture2, rIter);
    }

    SphereSelectorFunction::registerTarget(this);
    MiniatureGalaxyFunction::registerMiniatureGalaxyToHolder(this, rIter);

    mOrbit = new AstroDomeOrbit();
    mOrbit->initWithoutIter();

    mNamePlate = new GalaxyNamePlate(mGalaxyName, true);
    mNamePlate->initWithoutIter();

    MR::registerDemoSimpleCastAll(mNamePlate);

    makeActorDead();
}

void MiniatureGalaxy::initAfterPlacement() {
    mOrbit->setup(MiniatureGalaxyFunction::calcMiniatureGalaxyIndex(this));
}

void MiniatureGalaxy::appear() {
    mRotation.set< f32 >(0.0f, ::cInitialRotateY[MR::clamp(MiniatureGalaxyFunction::calcMiniatureGalaxyIndex(this), 0, ARRAY_SIZE(::cInitialRotateY) - 1)], 0.0f);

    mIsZoomOut = true;
    mZoomStep = ::cZoomFrame;
    setScale(::cZoomOutScale);

    if (!MR::isAppearGalaxy(mGalaxyName)) {
        mState = MiniatureGalaxyState_Hatena;
    } else if (MR::isOnGameEventFlagGalaxyOpen(mGalaxyName)) {
        mState = MiniatureGalaxyState_Open;
    } else if (MR::canOpenGalaxy(mGalaxyName)) {
        mState = MiniatureGalaxyState_New;
    } else {
        mState = MiniatureGalaxyState_Unknown;
    }

    LiveActor::appear();

    MR::tryStartAllAnim(this, mObjectName);

    mShadowModel->appear();
    MR::startBtk(mShadowModel, "MiniatureGalaxyShadow");

    mSelectModel->appear();
    MR::startBtk(mSelectModel, "MiniatureGalaxySelect");
    showSelectModel();

    mOrbit->appear();

    switch (mState) {
    case MiniatureGalaxyState_Hatena:
    case MiniatureGalaxyState_Unknown:
        appearUnknownModel("Unknown");
        break;
    case MiniatureGalaxyState_New:
        appearUnknownModel("New");
        break;
    case MiniatureGalaxyState_Open:
        showOpenModel();
        break;
    }

    setNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvWait));
}

void MiniatureGalaxy::kill() {
    if (mType == MiniatureGalaxyType_Koopa) {
        if (isUseKoopaFaceModel()) {
            MR::forceDeleteEffect(this, ::cEyeLightEffect);
        }

        MR::forceDeleteEffect(mMysteryModel, ::cEyeLightEffect);
    }

    LiveActor::kill();

    if (!MR::isDead(mShadowModel)) {
        mShadowModel->kill();
    }

    if (!MR::isDead(mMysteryModel)) {
        mMysteryModel->kill();
    }

    if (!MR::isDead(mSelectModel)) {
        mSelectModel->kill();
    }

    if (mStarPlateModel != nullptr && !MR::isDead(mStarPlateModel)) {
        mStarPlateModel->kill();
    }

    mOrbit->kill();
}

void MiniatureGalaxy::control() {
    updateZoomInOut();
    mOrbit->calcGalaxyPos(&mPosition);

    mRotation.y = MR::repeatDegree(mRotation.y + ::cRotateSpeedY);

    updateNamePlate();
}

void MiniatureGalaxy::calcAndSetBaseMtx() {
    Pos3f rotMtx;
    SphereSelectorFunction::calcHandledRotateMtx(Vec3f(0.0f), &rotMtx);

    Pos3f baseMtx = rotMtx;
    MR::rotateMtxLocalYDegree(baseMtx, mRotation.y);
    baseMtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, baseMtx);

    // The select model faces the camera.
    Vec3f front;
    front.sub(MR::getCamPos(), mPosition);

    Vec3f up;
    up.set(MR::getCamYdir());

    if (!MR::normalizeOrZero(&front) && !MR::isSameDirection(front, up)) {
        MR::makeMtxFrontUpPos(&mPosMtx, front, up, mPosition);
    }

    // The shadow sits on the plane cShadowDepth below the centre, under the galaxy.
    Vec3f domeUp;
    rotMtx.getYDir(domeUp);

    Vec3f shadowPos;
    shadowPos.scale(-::cShadowDepth, domeUp);
    shadowPos += mPosition;

    Vec3f height;
    height.scale(domeUp.dot(mPosition), domeUp);
    shadowPos -= height;

    mShadowBaseMtx.set(rotMtx);
    mShadowBaseMtx.setTrans(shadowPos);
    MR::setBaseTRMtx(mShadowModel, mShadowBaseMtx);

    mProjmapEffectMtxSetter->updateMtxUseBaseMtx();
}

bool MiniatureGalaxy::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (SphereSelectorFunction::trySyncAppearMsgSelectStart(this, msg)) {
        return true;
    }

    if (SphereSelectorFunction::isMsgTargetSelected(msg)) {
        if (!isNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvWait))) {
            return false;
        }

        if (mState == MiniatureGalaxyState_New) {
            SphereSelectorFunction::invalidatePointing();
            setNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvOpen));
            return false;
        }

        if (mState == MiniatureGalaxyState_Open) {
            SphereSelectorFunction::confirmStart();
            setNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvSelected));
            return true;
        }

        return false;
    }

    if (SphereSelectorFunction::isMsgConfirmStart(msg) && !isNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvSelected))) {
        setNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvNotSelected));
        return true;
    }

    if (SphereSelectorFunction::isMsgConfirmCancel(msg)) {
        setNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvWait));
        return true;
    }

    if (SphereSelectorFunction::isMsgConfirmed(msg)) {
        setNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvConfirmed));
        return true;
    }

    return false;
}

void MiniatureGalaxy::initPartsModel() {
    const char* pModelName = "MiniatureGalaxyUnknown";

    if (mType == MiniatureGalaxyType_Hatena) {
        pModelName = "MiniHatenaGalaxy";
    } else if (mType == MiniatureGalaxyType_Koopa) {
        pModelName = "MiniatureGalaxyUnknownKoopa";
    }

    mMysteryModel = ::createGalaxyPart("Unknownモデル", pModelName, getBaseMtx(), false);
    ::initGalaxyPart(mMysteryModel);

    mShadowModel = ::createGalaxyPart("影モデル", "MiniatureGalaxyShadow", mShadowBaseMtx, true);
    mProjmapEffectMtxSetter = MR::initMaterialProjmapEffectMtxSetter(mShadowModel);
    MR::prepareMaterials(mShadowModel);
    ::initGalaxyPart(mShadowModel);

    mSelectModel = ::createGalaxyPart("選択時モデル", "MiniatureGalaxySelect", mPosMtx, false);
    ::initGalaxyPart(mSelectModel);

    if (mType == MiniatureGalaxyType_Hatena) {
        return;
    }

    // One digit material below ten stars, two from ten up.
    mStarPlateModel = ::createGalaxyPart("スター数モデル", "MiniatureGalaxyStarNumber", mPosMtx, true);

    s32 powerStarNum = MR::getPowerStarNumToOpenGalaxy(mGalaxyName);

    if (powerStarNum < ::cDigitBase) {
        ::setDigitTexMtx(mStarPlateModel, "StarNumber1", &mOnesTexMtx);
        MR::hideMaterial(mStarPlateModel, "StarNumber01");
        MR::hideMaterial(mStarPlateModel, "StarNumber10");
    } else {
        ::setDigitTexMtx(mStarPlateModel, "StarNumber01", &mOnesTexMtx);
        ::setDigitTexMtx(mStarPlateModel, "StarNumber10", &mTensTexMtx);
        MR::hideMaterial(mStarPlateModel, "StarNumber1");
    }

    mOnesTexMtx.mInfo.mSRT.mTranslationY = powerStarNum % ::cDigitBase * ::cDigitTexStep;
    mTensTexMtx.mInfo.mSRT.mTranslationY = powerStarNum / ::cDigitBase * ::cDigitTexStep;

    MR::prepareMaterials(mStarPlateModel);
    ::initGalaxyPart(mStarPlateModel);
}

void MiniatureGalaxy::setScale(f32 scale) {
    mScale.setAll< f32 >(scale);
    MR::setStarPointerTargetRadius3d(this, ::cPointingRadius * scale);

    mShadowModel->mScale.setAll< f32 >(scale);

    // The opening keeps the unknown model and the plate at full size.
    if (isNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvOpen))) {
        return;
    }

    mMysteryModel->mScale.setAll< f32 >(scale);

    if (mStarPlateModel != nullptr) {
        mStarPlateModel->mScale.setAll< f32 >(scale);
    }
}

// A zoom starts only after the previous one has finished.
void MiniatureGalaxy::tryZoomIn(bool isDisappearSelect) {
    if (mIsZoomOut && isZoomEnd()) {
        mIsZoomOut = false;
        mZoomStep = 0;
    }

    if (isDisappearSelect) {
        disappearSelectModel();
    } else {
        appearSelectModel();
    }
}

void MiniatureGalaxy::tryZoomOut() {
    if (!mIsZoomOut && isZoomEnd()) {
        mIsZoomOut = true;
        mZoomStep = 0;
    }

    disappearSelectModel();
}

bool MiniatureGalaxy::isZoomEnd() const {
    return mZoomStep == ::cZoomFrame;
}

void MiniatureGalaxy::updateZoomInOut() {
    if (isZoomEnd()) {
        return;
    }

    mZoomStep++;
    f32 zoomRate = static_cast< f32 >(mZoomStep) / ::cZoomFrame;

    if (mIsZoomOut) {
        setScale(MR::getEaseOutValue(zoomRate, ::cZoomInScale, ::cZoomOutScale, ::cZoomInScale));
        return;
    }

    f32 reaction = MR::getScaleWithReactionValueZeroToOne(zoomRate, ::cZoomInReactionFreq, ::cZoomInReactionAmplitude);
    setScale(MR::getLinerValue(reaction, ::cZoomOutScale, ::cZoomInScale, ::cZoomInScale));

    if (mZoomStep != 1) {
        return;
    }

    if (mState == MiniatureGalaxyState_New || mState == MiniatureGalaxyState_Open) {
        playPointedME();
    } else {
        playNeedStarME();
    }
}

void MiniatureGalaxy::updateNamePlate() {
    if (MR::isDead(mNamePlate)) {
        return;
    }

    Vec3f platePos;
    platePos.add(mPosition, ::cNamePlateOffset);
    mNamePlate->setPos3D(platePos);
}

void MiniatureGalaxy::appearUnknownModel(const char* pBrkName) {
    mMysteryModel->appear();
    MR::startBrk(mMysteryModel, pBrkName);

    if (mType == MiniatureGalaxyType_Normal) {
        MR::startBtk(mMysteryModel, "MiniatureGalaxyUnknown");
    } else if (mType == MiniatureGalaxyType_Hatena) {
        MR::startBtk(mMysteryModel, "MiniHatenaGalaxy");
    }

    if (mStarPlateModel != nullptr) {
        mStarPlateModel->appear();
        MR::startBrk(mStarPlateModel, pBrkName);
        MR::startBck(mStarPlateModel, "MiniatureGalaxyStarNumber");
        // Frame 1 is the Koopa plate.
        MR::setBckFrameAndStop(mStarPlateModel, mType == MiniatureGalaxyType_Koopa ? 1.0f : 0.0f);
    }

    hideUnknownModel();
}

void MiniatureGalaxy::showUnknownModel() {
    MR::hideModelAndOnCalcAnimIfShown(this);
    MR::showModelIfHidden(mMysteryModel);

    if (mStarPlateModel != nullptr) {
        MR::showModelIfHidden(mStarPlateModel);
    }

    if (mType == MiniatureGalaxyType_Koopa) {
        MR::emitEffect(mMysteryModel, ::cEyeLightEffect);
    }
}

void MiniatureGalaxy::showSelectModel() {
    MR::showModelIfHidden(mSelectModel);
    MR::startBrk(mSelectModel, "Disappear");
    MR::setBrkFrame(mSelectModel, MR::getBrkFrameMax(mSelectModel));
}

void MiniatureGalaxy::hideUnknownModel() {
    MR::hideModelAndOnCalcAnimIfShown(this);
    MR::hideModelIfShown(mMysteryModel);

    if (mStarPlateModel != nullptr) {
        MR::hideModelIfShown(mStarPlateModel);
    }

    if (mType == MiniatureGalaxyType_Koopa) {
        MR::forceDeleteEffect(mMysteryModel, ::cEyeLightEffect);
    }
}

void MiniatureGalaxy::showOpenModel() {
    MR::showModelIfHidden(this);

    if (isUseKoopaFaceModel()) {
        MR::emitEffect(this, ::cEyeLightEffect);
    }
}

void MiniatureGalaxy::hideOpenModel() {
    MR::hideModelAndOnCalcAnimIfShown(this);

    if (isUseKoopaFaceModel()) {
        MR::forceDeleteEffect(this, ::cEyeLightEffect);
    }
}

// Appear and Disappear mirror each other, so a reversal continues from the mirrored frame.
void MiniatureGalaxy::appearSelectModel() {
    if (MR::isBrkPlaying(mSelectModel, "Disappear")) {
        f32 frame = MR::getBrkFrameMax(mSelectModel) - MR::getBrkFrame(mSelectModel);

        MR::startBrk(mSelectModel, "Appear");
        MR::setBrkFrame(mSelectModel, frame);
    }
}

void MiniatureGalaxy::disappearSelectModel() {
    if (MR::isBrkPlaying(mSelectModel, "Appear")) {
        f32 frame = MR::getBrkFrameMax(mSelectModel) - MR::getBrkFrame(mSelectModel);

        MR::startBrk(mSelectModel, "Disappear");
        MR::setBrkFrame(mSelectModel, frame);
    }
}

void MiniatureGalaxy::showNamePlate(bool isAppearNow) {
    if (MR::isDemoActive()) {
        return;
    }

    switch (mState) {
    case MiniatureGalaxyState_Hatena:
    case MiniatureGalaxyState_Unknown:
        mNamePlate->showUnknown(isAppearNow);
        break;
    case MiniatureGalaxyState_New:
        mNamePlate->showNew(isAppearNow);
        break;
    case MiniatureGalaxyState_Open:
        mNamePlate->showOpen(isAppearNow);
        break;
    }
}

bool MiniatureGalaxy::isUseKoopaFaceModel() const {
    return mType == MiniatureGalaxyType_Koopa && ::isUseKoopaFaceModel(mObjectName, mGalaxyName);
}

void MiniatureGalaxy::playPointedME() {
    ::startRandomME(::cPointedME, ARRAY_SIZE(::cPointedME));
}

void MiniatureGalaxy::playNeedStarME() {
    ::startRandomME(::cNeedStarME, ARRAY_SIZE(::cNeedStarME));
}

void MiniatureGalaxy::exeWait() {
    if (MR::isFirstStep(this)) {
        if (mState == MiniatureGalaxyState_Open) {
            showOpenModel();
        } else {
            showUnknownModel();
        }

        showSelectModel();
    }

    SphereSelectorFunction::tryRegisterPointingTarget(this, HandlePointingPriority_Depth);

    if (SphereSelectorFunction::isPointingTarget(this)) {
        tryZoomIn(false);
        showNamePlate(true);
    } else {
        tryZoomOut();

        if (SphereSelectorFunction::isSelectWait() && SphereSelectorFunction::isValidPointing() &&
            MiniatureGalaxyFunction::getPointingMiniatureGalaxy() == nullptr) {
            showNamePlate(false);
        }
    }

    mOrbit->moveCoord();
}

void MiniatureGalaxy::exeNotSelected() {
    if (MR::isFirstStep(this)) {
        if (mState == MiniatureGalaxyState_Open) {
            hideOpenModel();
        } else {
            hideUnknownModel();
        }

        MR::hideModelIfShown(mSelectModel);
    }
}

void MiniatureGalaxy::exeOpen() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(mMysteryModel, "Select");
        MR::emitEffect(mMysteryModel, "MiniatureGalaxyNewOpenSign");
        MR::startSystemSE("SE_SY_NEW_GALAXY_OPEN_SIGN");

        mIsZoomOut = false;
        mZoomStep = ::cZoomFrame;
        setScale(::cZoomInScale);

        disappearSelectModel();

        MR::onGameEventFlagGalaxyOpen(mGalaxyName);

        if (::isExistGrandStar(mGalaxyName)) {
            MR::stopStageBGM(::cOpenBgmFadeFrame);
        }
    }

    if (MR::isLessStep(this, ::cOpenMiniatureAppearStep)) {
        MR::startSystemLevelSE("SE_SY_LV_NEW_GALAXY_OPENING");
    }

    if (MR::isStep(this, ::cOpenMiniatureAppearStep)) {
        MR::startSystemSE("SE_SY_NEW_GALAXY_OPEN");
        MR::emitEffect(mMysteryModel, "MiniatureGalaxyNewOpen");
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, ::cOpenDisappearStep)) {
        mState = MiniatureGalaxyState_Open;
        showOpenModel();

        mMysteryModel->kill();

        if (mStarPlateModel != nullptr) {
            mStarPlateModel->kill();
        }

        mIsZoomOut = true;
        mZoomStep = ::cZoomFrame;
        setScale(::cZoomOutScale);

        if (::isExistGrandStar(mGalaxyName)) {
            MR::startStageBGM("BGM_KOOPA_APPEAR", false);
            MR::setNextStageBGM("MBGM_ASTRO_DOME_2");
        }
    }

    if (MR::isLessStep(this, ::cOpenDisappearStep)) {
        MR::tryRumblePadVeryWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, ::cOpenTotalFrame)) {
        SphereSelectorFunction::validatePointing();

        if (::isExistGrandStar(mGalaxyName)) {
            MR::startSystemSE("SE_SY_KOOPA_LAUGH");
        }

        setNerve(GET_NERVE(MiniatureGalaxy, MiniatureGalaxyNrvWait));
    }
}

void MiniatureGalaxy::exeSelected() {
    tryZoomIn(true);
}

// Waits for the selector to leave the scene.
void MiniatureGalaxy::exeConfirmed() {
}
