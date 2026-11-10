#include "Game/Util/DrawUtil.hpp"
#include "Game/LiveActor/MaterialUpdater.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/MapObj/CollectCounter.hpp"
#include "Game/MapObj/IceStep.hpp"
#include "Game/Player/DrawAdaptor.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Player/TornadoMario.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Material.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/StringUtil.hpp"

namespace MR {
    void showMaterial(Model*, const char*);
};  // namespace MR

class JetTurtleShadow : public LiveActor {
public:
    void drawType0() const;
};

void MarioActor::initDrawAndModel() {
    mNumModPowerUps = NebulaMods::powerUpCount();
    mModels = new PlayerModel*[6 + mNumModPowerUps]();
    if (mNumModPowerUps > 0) {
        mModPowerUpModels = new ModelHolder*[mNumModPowerUps]();
    }

    mShadowDrawer = new DrawAdaptor(MR::Functor(this, &MarioActor::drawShadow), MR::DrawType_AlphaShadow);
    // The scene holds each drawer (a NameObj).
    new DrawAdaptor(MR::Functor(this, &MarioActor::drawSilhouette), MR::DrawType_PlayerSilhouette);
    new DrawAdaptor(MR::Functor(this, &MarioActor::drawPreWipe), MR::DrawType_CometScreenFilter);
    new DrawAdaptor(MR::Functor(this, &MarioActor::drawScreenBlend), MR::DrawType_CenterScreenBlur);
    new DrawAdaptor(MR::Functor(this, &MarioActor::drawIndirect), MR::DrawType_PlayerIndirect);

    // MarioAnime's material animations are bound to this player's model, also when a ghost's model used the archive first
    // or the other player's did before a restart of the same stage.
    ResourceHolder* const pPlayerHolder = MR::createAndAddResourceHolder(gIsLuigi ? "Luigi" : "Mario");
    MR::createAndAddResourceHolder("MarioAnime")
        ->bindMaterialAnimations(static_cast< ModelData* >(pPlayerHolder->mModelResTable->getRes(gIsLuigi ? "luigi" : "mario")));

    if (gIsLuigi) {
        initModelManagerWithAnm("Luigi", "MarioAnime", true);
    } else {
        initModelManagerWithAnm("Mario", "MarioAnime", true);
    }

    PlayerModel* baseModel = static_cast< PlayerModel* >(MR::getModel(this));
    baseModel->initExtraPoses(8);

    MR::initMaterialFog(this, true);
    MR::prepareMaterials(this);

    mModels[0] = baseModel;
    mBeeModel = nullptr;
    mIceModel = nullptr;
    mIceSteps = nullptr;
    mInvincibleModel = nullptr;
    mHopperModel = nullptr;
    mBoneModel = nullptr;
    mTornadoMario = nullptr;
    initFireBall();

    initBeeMario();
    initIceMario();
    initInvincibleMario();
    initHopperMario();
    initTeresaMario();
    initBoneMario();
    initTornadoMario();
    initModPowerUpModels();
    initBlur();

    mCurrModel = 0;
    mDisplayModel = 0;
    initShadow();
    initHand();

    initFace();

    mFogRate = 0.0f;

    swapTextureInit();
    createRainbowMesh();

    {
        NebulaArena::CurrentScope restorer(Arenas::scene());
        mBlurTexture = GameTexture::createTarget(0.2f, 0.14f);

        for (u32 i = 0; i < 2; i++) {
            mMaskTextures[i] = new GameTexture(8, 8, NEBULA_TEXTURE_LINEAR_LA8);
            mMaskTextures[i]->setSampling(1, 1, 0, 0);
        }

        mMaskIndex = 0;
        updateRandomTexture(1000.0f);
        updateRandomTexture(1000.0f);
    }

    initScreenBox();
    MR::startBtp(this, "ElementEnd");
}

void MarioActor::initBeeMario() {
    PlayerModel* model;
    const char* modelName;
    const char* archiveName;
    if (gIsLuigi) {
        modelName = "BeeLuigi";
        archiveName = "BeeLuigi";
    } else {
        modelName = "BeeMario";
        archiveName = "BeeMario";
    }

    MR::createAndAddResourceHolder(archiveName);

    mBeeModel = new ModelHolder(modelName, true);
    mBeeModel->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(mBeeModel));
    model->copyExtraPoses(mModels[0]);

    MR::initMaterialFog(mBeeModel, true);
    MR::prepareMaterials(mBeeModel);

    mModels[2] = model;
    mBeeModel->kill();
    MR::hideModel(mBeeModel);
    mBeeFur = MR::initFurPlayer(mBeeModel);
    MR::hideJoint(mBeeModel, "Face0");

    mBeeWing = new MarioParts(this, "B-WING", "BeeMarioWing", false, nullptr, nullptr);
    mBeeWing->initWithoutIter();

    mBeeWing->initFixedPosition(mSpine2Mtx.toMtxPtr(), Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    mBeeWing->makeActorDead();

    mBeeWingAnim = 0;
}

void MarioActor::initHopperMario() {
    PlayerModel* model;
    const char* modelName;
    const char* archiveName;
    if (gIsLuigi) {
        modelName = "HopperLuigi";
        archiveName = "HopperLuigi";
    } else {
        modelName = "HopperMario";
        archiveName = "HopperMario";
    }

    MR::createAndAddResourceHolder(archiveName);

    mHopperModel = new ModelHolder(modelName, true);
    mHopperModel->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(mHopperModel));
    model->copyExtraPoses(mModels[0]);

    MR::initMaterialFog(mHopperModel, true);
    MR::prepareMaterials(mHopperModel);

    mModels[5] = model;
    mHopperModel->kill();
    MR::hideModel(mHopperModel);
    MR::hideJoint(mHopperModel, "Face0");
}

void MarioActor::initIceMario() {
    PlayerModel* model;
    const char* modelName;
    const char* archiveName;
    if (gIsLuigi) {
        modelName = "IceLuigi";
        archiveName = "IceLuigi";
    } else {
        modelName = "IceMario";
        archiveName = "IceMario";
    }

    MR::createAndAddResourceHolder(archiveName);

    mIceModel = new ModelHolder(modelName, true);
    mIceModel->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(mIceModel));
    model->copyExtraPoses(mModels[0]);
    model->copyAnmMtxBuffer(mModels[0]);

    MR::initMaterialFog(mIceModel, true);
    MR::prepareMaterials(mIceModel);

    mModels[3] = model;
    mIceModel->kill();
    MR::hideModel(mIceModel);

    mIceSteps = new IceStep*[cIceStepNum];
    for (u32 i = 0; i < cIceStepNum; i++) {
        mIceSteps[i] = new IceStep("アイス床");
        mIceSteps[i]->initWithoutIter();
        mIceSteps[i]->kill();
    }

    mIceStepIndex = 0;
}

void MarioActor::initInvincibleMario() {
    PlayerModel* model;
    const char* modelName;
    const char* archiveName;
    if (gIsLuigi) {
        modelName = "InvincibleLuigi";
        archiveName = "InvincibleLuigi";
    } else {
        modelName = "InvincibleMario";
        archiveName = "InvincibleMario";
    }

    MR::createAndAddResourceHolder(archiveName);

    mInvincibleModel = new ModelHolder(modelName, true);
    mInvincibleModel->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(mInvincibleModel));
    model->copyExtraPoses(mModels[0]);
    model->copyAnmMtxBuffer(mModels[0]);

    MR::initMaterialFog(mInvincibleModel, true);
    MR::prepareMaterials(mInvincibleModel);

    mModels[4] = model;
    mInvincibleModel->kill();
    MR::hideModel(mInvincibleModel);
    MR::startBrk(mInvincibleModel, "InvincibleMario");
}

void MarioActor::initTeresaMario() {
    const char* modelName = gIsLuigi ? "TeresaLuigi" : "TeresaMario";
    mTeresaModel = new MarioParts(this, "テレサの皮", modelName, nullptr);
    if (gIsLuigi) {
        mTeresaModel->mEffectName = "TeresaMario";
    }

    mTeresaModel->initWithoutIter();
    mTeresaModel->kill();

    mTeresaModel->initFixedPosition(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);

    mTeresaAlpha = 0.0f;
    mTeresaTilt = 0.0f;
    mTeresaBlinkTimer = 0;

    MR::initShadowVolumeSphere(mTeresaModel, 90.0f);
    initTeresaMarioAnimation();
}

void MarioActor::initTornadoMario() {
    const char* modelName = gIsLuigi ? "LuigiTornado" : "MarioTornado";
    mTornadoMario = new TornadoMario(modelName);
    mTornadoMario->initWithoutIter();
    mTornadoEnd = 0;
}

void MarioActor::initBoneMario() {
    const char* modelName = gIsLuigi ? "BoneLuigi" : "BoneMario";

    mBoneModel = new ModelHolder(modelName, true);
    mBoneModel->initWithoutIter();

    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(mBoneModel));
    model->copyExtraPoses(mModels[0]);

    MR::initMaterialFog(mBoneModel, true);
    MR::prepareMaterials(mBoneModel);

    mModels[1] = model;
    mBoneModel->kill();
    MR::hideModel(mBoneModel);
}

namespace {
    void showWholeModel(ModelHolder* pHolder, bool show) {
        if (pHolder != nullptr) {
            Model* pModel = MR::getModel(pHolder);
            MR::showJointAndChildrenOn(pModel, pModel->mModelData->getJointAt(0), show);
        }
    }
}  // namespace

// show_joint on the player: each of his body models with the joint, and the face and hand models with the joint
// they hang from.
void MarioActor::showJointByPlugin(const char* pName, bool show) {
    for (s32 i = 0; i < 6 + mNumModPowerUps; i++) {
        PlayerModel* model = mModels[i];
        if (model == nullptr) {
            continue;
        }
        const s32 idx = model->mModelData->findJoint(pName);
        if (idx < 0) {
            continue;
        }
        Joint* pTop = model->mModelData->getJointAt(static_cast< u32 >(idx));
        MR::showJointAndChildrenOn(model, pTop, show);
        if (i != 0) {
            continue;
        }
        if (MR::isJointUnder(pTop, MR::getJoint(model, "Face0"))) {
            showWholeModel(mFaceModel, show);
        }
        if (MR::isJointUnder(pTop, MR::getJoint(model, "HandL0"))) {
            showWholeModel(mHandL, show);
            showWholeModel(mInvincibleHandL, show);
            showWholeModel(mIceHandL, show);
        }
        if (MR::isJointUnder(pTop, MR::getJoint(model, "HandR0"))) {
            showWholeModel(mHandR, show);
            showWholeModel(mInvincibleHandR, show);
            showWholeModel(mIceHandR, show);
        }
    }
}

void MarioActor::initModPowerUpModels() {
    for (s32 i = 0; i < mNumModPowerUps; i++) {
        const char* pName = NebulaMods::powerUpModel(i);
        if (pName == nullptr) {
            continue;
        }
        if (!MR::isExistObjectOrLayoutArchive(pName)) {
            continue;
        }
        MR::createAndAddResourceHolder(pName);
        ModelHolder* pHolder = new ModelHolder(pName, true);
        pHolder->initWithoutIter();
        PlayerModel* pModel = static_cast< PlayerModel* >(MR::getModel(pHolder));
        pModel->copyExtraPoses(mModels[0]);
        pModel->copyAnmMtxBuffer(mModels[0]);
        MR::initMaterialFog(pHolder, true);
        MR::prepareMaterials(pHolder);
        mModels[6 + i] = pModel;
        mModPowerUpModels[i] = pHolder;
        pHolder->kill();
        MR::hideModel(pHolder);
    }
}

void MarioActor::changeDisplayMode(u8 mode) {
    mCurrModel = mode;
    mMarioAnim->mXanimePlayer->setModel(mModels[mCurrModel]);
    mMarioAnim->mXanimePlayerUpper->setModel(mModels[mCurrModel]);

    mModelManager->mMaterialUpdater->mModel = mModels[mCurrModel];

    if (mTakingFixedPos != nullptr) {
        MtxPtr handMtx = MR::getJointMtx(this, "HandR");
        mTakingFixedPos->setBaseMtx(handMtx);
    }

    MtxPtr handMtx = MR::getJointMtx(this, "HandR");
    mSearchLightThrowPos->setBaseMtx(handMtx);
}

void MarioActor::calcViewAndEntry() {
    decideShadowMode();

    PlayerModel* model = getModel();
    model->mDrawPasses &= ~PlayerModel::Pass_BehindMark;
    model->swapDrawBuffer(4);
    getSimpleModel()->swapDrawBuffer(4);

    calcViewMainModel();
    calcViewSilhouetteModel();
    calcViewWallShadowModel();
    model->setDrawView(0);
    calcViewBlurModel();

    if (mFootPrint != nullptr) {
        calcViewFootPrint();
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        calc1stPersonView();
    } else {
        calcFogLighting();
    }

    switch (mCurrModel) {
    case 0:
        swapTexture("EyeLid_v", static_cast< u8 >(mEyeLidFrame));
        break;
    case 2:
        swapTexture("EyeLid_v", static_cast< u8 >(mEyeLidFrame + 4));
        break;
    case 5:
        swapTexture("EyeLid_v", static_cast< u8 >(mEyeLidFrame + 8));
        break;
    default:
        break;
    }

    if (mShadowMode > 1) {
        calcViewReflectionModel();
    } else {
        mReflectSimpleAlpha = 0;
        mWaterReflectAlpha = 0;
    }

    calcViewSearchLight();

    if (mIceHandL != nullptr) {
        mIceHandL->updateMtx(mHandLMtx.toMtxPtr());
    }

    if (mIceHandR != nullptr) {
        mIceHandR->updateMtx(mHandRMtx.toMtxPtr());
    }

    mHandL->updateMtx(mHandLMtx.toMtxPtr());

    mHandR->updateMtx(mHandRMtx.toMtxPtr());

    if (mInvincibleHandL != nullptr) {
        mInvincibleHandL->updateMtx(mHandLMtx.toMtxPtr());
    }

    if (mInvincibleHandR != nullptr) {
        mInvincibleHandR->updateMtx(mHandRMtx.toMtxPtr());
    }

    mFaceModel->updateMtx(mFaceMtx.toMtxPtr());

    updateDarkMask(0x96);
    updateDarkMask(0x96);

    if (MR::isDemoActive()) {
        mTrampleCounter->kill();
    }
}

void MarioActor::drawMarioModel() const {
    if (isAllHidden()) {
        return;
    }

    if (mTornadoMario != nullptr) {
        if (mMario->mMovementStates.tornado) {
            if (mMario->mTornadoTimer > 2) {
                return;
            }
        }

        if (!MR::isDead(mTornadoMario) &&
            (MR::isBckPlaying(mTornadoMario, "MarioTornadoEnd") || MR::isBckPlaying(mTornadoMario, "MarioTornadoLoop"))) {
            return;
        }
    }

    switch (mCurrModel) {
    case 3:
        return;
    default:
        break;
    }

    drawSpinInhibit();
    drawSphereMask();
    bool res = drawDarkMask();

    PlayerModel* model = mModels[mCurrModel];

    if (res) {
        model->mDrawPasses |= PlayerModel::Pass_OutsideDarkMask;
    }

    if (!mFlag.mIsHiddenModel) {
        model->mDrawPasses &= ~PlayerModel::Pass_BehindMark;
        if (mMario->isStatusActive(MarioStatus_FpView)) {
            if (mFpViewHidden) {
                return;
            }

            model->setDynamicList(&mAlphaTint);

            MR::showJoint(model, "HandR0");
            MR::showJoint(model, "HandL0");
            MR::showJoint(model, "Face0");
        } else {
            if (mCurrModel == 4) {
                model->setDynamicList(nullptr);
            } else {
                model->setDynamicList(&mLightTint);
            }
        }

        // Mario is in front of the wall's shadow: his pixels lose its mark.
        if (mBeeWallWalk != 0) {
            Frame::setPassStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
        }

        model->setDrawView(0);
        model->directDraw();
        model->mDrawPasses = 0;
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        MR::hideJoint(model, "HandR0");
        MR::hideJoint(model, "HandL0");
        MR::hideJoint(model, "Face0");
    }

    drawHand();

    if (!MR::isHiddenModel(mFaceModel)) {
        PlayerModel* cool = static_cast< PlayerModel* >(MR::getModel(mFaceModel));
        cool->setDynamicList(&mLightTint);
        cool->directDraw();
    }

    if (mBeeWallWalk != 0) {
        Frame::setPassStencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
    }

    // The dark mask has done its work.
    if (res) {
        MR::clearStencil();
        MR::loadViewMtx();
        MR::loadProjectionMtx();
    }
}

bool MarioActor::isAllHidden() const {
    bool hidden = false;
    if (mHidden || mMorphHidden || mHiddenByDemo || mModHidden) {
        hidden = true;
    }

    return hidden;
}

// The first of a model's four eyelid textures; texNo when it has none.
static u32 findEyeLidTexture(ModelData* pModelData, u32 textureNum, u32 texNo) {
    for (u32 i = 0; i < textureNum; i++) {
        if (MR::isEqualString(pModelData->getTextureName(i), "mario_eyeLid.0")) {
            return i;
        }
    }
    return texNo;
}

void MarioActor::swapTextureInit() {
    ModelData* actorData = MR::getModelData(this);
    const u32 textureCount = actorData->getTextureCount();
    mSwapTextures = new ResTIMG*[textureCount];
    mEyeLidFrame = 0;

    for (u32 i = 0; i < textureCount; i++) {
        mSwapTextures[i] = MR::getResTIMG(this, i);
    }

    mSwapImages = new const void*[12]();

    u32 texNo = findEyeLidTexture(actorData, textureCount, 0);

    mEyeLidTexBase = texNo;
    mSwapImages[0] = mSwapTextures[texNo]->image();
    mSwapImages[1] = mSwapTextures[texNo + 1]->image();
    mSwapImages[2] = mSwapTextures[texNo + 2]->image();
    mSwapImages[3] = mSwapTextures[texNo + 3]->image();

    if (mBeeModel != nullptr) {
        ModelData* beeData = MR::getModelData(mBeeModel);
        const u32 textureNum = beeData->getTextureCount();

        texNo = findEyeLidTexture(beeData, textureNum, texNo);

        for (u32 i = 0; i < textureNum; i++) {
            mSwapTextures[i] = MR::getResTIMG(mBeeModel, i);
        }

        mSwapImages[4] = mSwapTextures[texNo]->image();
        mSwapImages[5] = mSwapTextures[texNo + 1]->image();
        mSwapImages[6] = mSwapTextures[texNo + 2]->image();
        mSwapImages[7] = mSwapTextures[texNo + 3]->image();
    }

    if (mHopperModel != nullptr) {
        ModelData* hopperData = MR::getModelData(mHopperModel);
        const u32 textureNum = hopperData->getTextureCount();

        texNo = findEyeLidTexture(hopperData, textureNum, texNo);

        for (u32 i = 0; i < textureNum; i++) {
            mSwapTextures[i] = MR::getResTIMG(mHopperModel, i);
        }

        mSwapImages[8] = mSwapTextures[texNo]->image();
        mSwapImages[9] = mSwapTextures[texNo + 1]->image();
        mSwapImages[10] = mSwapTextures[texNo + 2]->image();
        mSwapImages[11] = mSwapTextures[texNo + 3]->image();
    }

    initBlink();
}

void MarioActor::initFace() {
    mFacePoseCount = 8;
    MR::hideJoint(mModels[0], "Face0");
    mFacePose = 0;

    if (gIsLuigi) {
        mFaceModel = new ModelHolder("LuigiFace", true);
    } else {
        mFaceModel = new ModelHolder("MarioFace", true);
    }

    MR::initMaterialFog(mFaceModel, true);
    MR::prepareMaterials(mFaceModel);

    MR::getModelData(mFaceModel)->useTexturesOf(MR::getModelData(this));

    for (u32 i = 0; i < mFacePoseCount; i++) {
        copyMaterial(MR::getModel(mFaceModel), MR::getJointIndex(this, "Face0"), i);
    }
}

void MarioActor::updateFace() {
    switch (mCurrModel) {
    case 1:
    case 3:
    case 4:
        MR::hideModel(mFaceModel);
        return;
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        MR::hideModel(mFaceModel);
        return;
    }

    if (MR::isHiddenModel(this) || isHidden() || mMario->mMovementStates.tornado || mDarkMaskOn) {
        MR::hideModel(mFaceModel);
        return;
    }

    MR::showModel(mFaceModel);

    Vec3f partsControl;
    if (mMarioAnim->mUpperRunning) {
        XanimePlayer* player = mMarioAnim->mXanimePlayerUpper;
        const s32 partsControlJoint = MR::getJointIndex(this, "PartsControl");
        player->getMainAnimationTrans(static_cast< u32 >(partsControlJoint), &partsControl);
    } else {
        XanimePlayer* player = mMarioAnim->mXanimePlayer;
        const s32 partsControlJoint = MR::getJointIndex(this, "PartsControl");
        player->getMainAnimationTrans(static_cast< u32 >(partsControlJoint), &partsControl);
    }

    mFacePose = MR::clamp(static_cast< s32 >(0.49f + partsControl.x / 10.0f), 0, mFacePoseCount - 1);

    for (u32 i = 0; i < mFacePoseCount; i++) {
        Joint* eyeJoint = MR::getJoint(mFaceModel, i + 1);
        if (mFacePose == i) {
            MR::showJoint(eyeJoint);
        } else {
            MR::hideJoint(eyeJoint);
        }
    }
}

void MarioActor::initBlur() {
    for (u32 i = 0; i < 2; i++) {
        for (u32 j = 0; j < cBlurPoseNum; j++) {
            Model::initPose(&mBlurPoses[i][j]);
        }
    }

    mBlurState = false;
    mBlurPoseIndex = 0;
    mBlurPoseSet = 0;
}

PlayerModel* MarioActor::getModel() const {
    return mModels[mCurrModel];
}

ModelData* MarioActor::getModelData() const {
    return getModel()->mModelData;
}

PlayerModel* MarioActor::getSimpleModel() const {
    return mSimpleModel;
}

void MarioActor::calcViewMainModel() {
    PlayerModel* model = mModels[mCurrModel];
    if (mFlag.mIsNoCalcView) {
        return;
    }

    model->viewCalc3(0, nullptr);

    Mtx invView;
    Mtx invBase;
    NebulaMtxInverse(mBaseScaleMtx, invView);
    NebulaMtxInverse(mModels[mCurrModel]->mBaseTransformMtx, invBase);
    MR::multMtx(mModelUnbaseMtx, invBase, invView);
}

void MarioActor::draw() const {
    if (mFootPrint != nullptr) {
        mFootPrint->draw();
    }

    if (!isHidden() && !mPreWipeOn && !mDarkMaskOn) {
        if (mWallShadeOn) {
            drawWallShade(mWallShadePos, mWallShadeNormal, mWallShadeDist);
        }

        drawMarioModel();
        mMario->draw();
    }
}

void MarioActor::drawIndirect() const {
    drawModelBlur();
    drawIndirectModel();

    if (mMario->mHitStates.swimLifeDrain) {
        drawColdWaterDamage();
    }

    if (mRasterScrollAmp != 0.0f) {
        drawRasterScroll(mRasterScrollAmp, static_cast< s16 >(mRasterScrollPhase), mRasterScrollWave);
    }

    if (mPressTimer) {
        drawMosaic();
    }

    if (mMorphEndTimer) {
        drawLifeUp();
    }

    drawSpinEffect();
}

void MarioActor::drawIndirectModel() const {
    if (isAllHidden()) {
        return;
    }

    drawReflectModel();

    if (mTornadoMario != nullptr) {
        if (mMario->mMovementStates.tornado) {
            if (mMario->mTornadoTimer > 2) {
                return;
            }
        }

        if (!MR::isDead(mTornadoMario) &&
            (MR::isBckPlaying(mTornadoMario, "MarioTornadoEnd") || MR::isBckPlaying(mTornadoMario, "MarioTornadoLoop"))) {
            return;
        }
    }

    switch (mCurrModel) {
    case 3:
        break;
    default:
        return;
    }

    drawSpinInhibit();

    PlayerModel* model = getModel();
    if (!mFlag.mIsHiddenModel) {
        model->mDrawPasses &= ~PlayerModel::Pass_BehindMark;
        if (mMario->isStatusActive(MarioStatus_FpView)) {
            if (mFpViewHidden) {
                return;
            }

            model->setDynamicList(&mAlphaTint);
        } else {
            model->setDynamicList(&mLightTint);
        }

        MR::showJoint(model, "Face0");
        model->setDrawView(0);
        model->directDraw();
        model->mDrawPasses = 0;
    }

    drawIndirectHand();
}

void MarioActor::drawReflectModel() const {
    if (isAllHidden()) {
        return;
    }

    if (mShadowMode >= 6 && mReflectSimpleAlpha < 2) {
        return;
    }

    if (mShadowMode == 3 && mWaterReflectAlpha < 2) {
        return;
    }

    if (!(mShadowMode & 2)) {
        return;
    }

    if (isUseScreenBox()) {
        captureScreenBox();
    }

    PlayerModel* model = getModel();

    MR::showJoint(model, "HandR0");
    MR::showJoint(model, "HandL0");
    MR::showJoint(model, "Face0");

    if (static_cast< s8 >(mCurrModel) != 1U) {
        MR::hideMaterial(model, "EyeLid_v");
    }

    if ((mTakeCount == 0 ? nullptr : mTakeSensors[0]) != nullptr) {
        if (isUseScreenBox()) {
            static_cast< PlayerModel* >(MR::getModel(mTurtleShadow))->setDynamicList(&mAlphaTint);
        }

        mTurtleShadow->drawType0();
    }

    model->mDrawPasses = 0;

    if (mShadowMode == 2) {
        Frame::setZScaleOffset(1.0f, 0.00001f);
        model->mDrawPasses |= PlayerModel::Pass_DepthOnly;
        model->setDrawView(3);
        model->directDraw();
        model->mDrawPasses &= ~PlayerModel::Pass_DepthOnly;
        model->mDrawPasses |= PlayerModel::Pass_AddTexture;
        model->setDynamicList(&mAlphaTint);
    } else {
        if (isUseScreenBox()) {
            model->setDynamicList(&mAlphaTint);
        }

        model->mDrawPasses |= PlayerModel::Pass_Reflection;
    }

    Frame::setZScaleOffset(1.0f, 0.0f);
    model->setDrawView(3);
    model->directDraw();
    model->mDrawPasses = 0;

    MR::hideJoint(model, "HandR0");
    MR::hideJoint(model, "HandL0");

    if (static_cast< s8 >(mCurrModel) != 4U) {
        MR::hideJoint(model, "Face0");
    }

    if (static_cast< s8 >(mCurrModel) != 1U) {
        MR::showMaterial(model, "EyeLid_v");
    }

    if (isUseScreenBox()) {
        writeBackScreenBox();
    }
}

void MarioActor::drawModelBlur() const {
    if (isAllHidden()) {
        return;
    }

    PlayerModel* model = getModel();
    if (!model->mBlurPoseFresh) {
        model->mBlurDrawMissed = true;
        return;
    }

    model->mBlurPoseFresh = false;
    if (!mBlurState) {
        return;
    }

    model->mDrawPasses |= PlayerModel::Pass_BlurGhost;

    Mtx inv;
    NebulaMtxInverse(const_cast< Mtx34f& >(mPrevViewMtx).toMtxPtr(), inv);
    MR::multMtx(inv, inv, MR::getCameraViewMtx());
    for (u32 i = 1; i < cBlurPoseNum; i++) {
        const u32 idx = static_cast< u32 >(i + mBlurPoseIndex) % cBlurPoseNum;
        ModelPose& rPose = const_cast< MarioActor* >(this)->mBlurPoses[mBlurPoseSet][idx];
        if (!mBaseMtxStale) {
            Model::copyPose(&rPose, mBlurPoses[1 - mBlurPoseSet][idx]);
            Model::movePoseView(&rPose, inv);
        }
        model->setDrawPose(&rPose);


        model->setDynamicList(&mGhostTints[(i - 1) * cBlurPoseNum + idx]);
        model->directDraw();
    }

    model->mDrawPasses &= ~PlayerModel::Pass_BlurGhost;
}

void MarioActor::swapTexture(const char* pMaterialName, u8 texNo) const {
    switch (mPlayerMode) {
    case 4:
        if (texNo > 7) {
            return;
        }

        break;
    case 5:
        if (texNo > 11) {
            return;
        }

        break;
    default:
        if (texNo > 3) {
            return;
        }
    }

    ModelData* modelData = MR::getModelData(this);
    u32 materialNo = MR::getMaterialNo(modelData, pMaterialName);

    mModels[mCurrModel]->mNextImages[materialNo] = mSwapImages[texNo];
}

void MarioActor::copyMaterial(Model* pModel, u32 materialNo, s32 packetIndex) {
    ModelData* modelData = getModel()->getModelData();
    Material* material = modelData->getJointAt(materialNo)->getMesh();
    if (material == nullptr) {
        return;
    }

    u32 materialIndex = material->mIndex;

    u32 start;
    u32 end;

    if (packetIndex == -1) {
        start = 0;
        end = 7;
    } else {
        start = static_cast< u32 >(packetIndex);
        end = static_cast< u32 >(packetIndex);
    }

    for (u32 i = start; i <= end; i++) {
        MaterialEntry* matPacket = pModel->getMaterialEntry(i);
        matPacket->mMaterial = modelData->getMaterialAt(materialIndex);
        MeshEntry* shapePacket = pModel->getMeshEntry(i);
        matPacket->mInitMeshEntry = shapePacket;
        matPacket->addMeshEntry(shapePacket);
        shapePacket->mMaterial = matPacket->mMaterial;
    }
}
