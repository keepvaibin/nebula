#include "Game/Util/DrawUtil.hpp"
#include "Game/LiveActor/MaterialUpdater.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/MapObj/CollectCounter.hpp"
#include "Game/MapObj/IceStep.hpp"
#include "Game/Player/DrawAdaptor.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Player/TornadoMario.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/FootPrint.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Material.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cstring>

namespace MR {
    void showMaterial(Model*, const char*);
};  // namespace MR

class JetTurtleShadow : public LiveActor {
public:
    void drawType0() const;
};

void MarioActor::initDrawAndModel() {
    _218 = new DrawAdaptor(MR::Functor(this, &MarioActor::drawShadow), MR::DrawType_AlphaShadow);
    _21C = new DrawAdaptor(MR::Functor(this, &MarioActor::drawSilhouette), MR::DrawType_0x28);
    _220 = new DrawAdaptor(MR::Functor(this, &MarioActor::drawPreWipe), MR::DrawType_CometScreenFilter);
    _228 = new DrawAdaptor(MR::Functor(this, &MarioActor::drawScreenBlend), MR::DrawType_CenterScreenBlur);
    _22C = new DrawAdaptor(MR::Functor(this, &MarioActor::drawIndirect), MR::DrawType_0x24);

    if (gIsLuigi) {
        initModelManagerWithAnm("Luigi", "MarioAnime", true);
    } else {
        initModelManagerWithAnm("Mario", "MarioAnime", true);
    }

    PlayerModel* baseModel = static_cast< PlayerModel* >(MR::getModel(this));
    baseModel->_DD = 8;

    baseModel->initExtraPoses(baseModel->_DD);

    MR::initMaterialFog(this, true);
    MR::prepareMaterials(this);

    mModels[0] = baseModel;
    _9E4 = nullptr;
    _9C0 = nullptr;
    _B4C = nullptr;
    _9C8 = nullptr;
    _A00 = nullptr;
    _A04 = nullptr;
    mTornadoMario = nullptr;
    initFireBall();

    _3D2 |= 0x4;

    // With mods on, any power-up can be given anywhere, not only where a level placed its item.
    if (NebulaMods::hasCode()) {
        static const char* const cPowerUpArchives[] = {"BeeMarioWing", "IceMarioHandL", "IceMarioHandR", "InvincibleMarioHandL", "InvincibleMarioHandR"};
        static const char* const cMario[] = {"BeeMario", "HopperMario", "IceMario", "InvincibleMario", "TeresaMario", "MarioTornado"};
        static const char* const cLuigi[] = {"BeeLuigi", "HopperLuigi", "IceLuigi", "InvincibleLuigi", "TeresaLuigi", "LuigiTornado"};
        for (u32 i = 0; i < sizeof(cPowerUpArchives) / sizeof(cPowerUpArchives[0]); i++) {
            MR::mountArchiveByObjectOrLayoutName(cPowerUpArchives[i], nullptr);
        }
        for (u32 i = 0; i < sizeof(cMario) / sizeof(cMario[0]); i++) {
            MR::mountArchiveByObjectOrLayoutName(gIsLuigi ? cLuigi[i] : cMario[i], nullptr);
        }
    }

    initBeeMario();
    initIceMario();
    initInvincibleMario();
    initHopperMario();
    initTeresaMario();
    _3D2 |= 0x80;
    initBoneMario();
    initTornadoMario();
    initModPowerUpModels();
    initBlur();

    mCurrModel = 0;
    _A0B = 0;
    initShadow();
    initHand();

    initFace();

    _1A4 = 0.0f;

    swapTextureInit();
    createRainbowMesh();

    {
        MR::CurrentHeapRestorer restorer(static_cast< NebulaArena* >(MR::getSceneHeap()));
        _B7C = JUTTexture::createTarget(0.2f, 0.14f);

        for (u32 i = 0; i < 2; i++) {
            mMaskTextures[i] = new JUTTexture(8, 8, NEBULA_TEXTURE_LINEAR_LA8);
            mMaskTextures[i]->mWrapS = 1;
            mMaskTextures[i]->mWrapT = 1;
            mMaskTextures[i]->mMinType = 0;
            mMaskTextures[i]->mMagType = 0;
            mMaskTextures[i]->init();
        }

        _B88 = 0;
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
        archiveName = "BeeLuigi.arc";
    } else {
        modelName = "BeeMario";
        archiveName = "BeeMario.arc";
    }

    if (!MR::isLoadedObjectOrLayoutArchive(modelName)) {
        return;
    }

    if (!MR::isLoadedObjectOrLayoutArchive("BeeMarioWing")) {
        return;
    }

    MR::createAndAddResourceHolder(archiveName);

    _9E4 = new ModelHolder(modelName, true);
    _9E4->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(_9E4));
    model->copyExtraPoses(mModels[0]);

    MR::initMaterialFog(_9E4, true);
    MR::prepareMaterials(_9E4);

    mModels[2] = model;
    _9E4->kill();
    MR::hideModel(_9E4);
    _9EC = MR::initFurPlayer(_9E4);
    MR::hideJoint(_9E4, "Face0");

    _9E8 = new MarioParts(this, "B-WING", "BeeMarioWing", false, nullptr, nullptr);
    _9E8->initWithoutIter();

    _9E8->initFixedPosition(_DDC.toMtxPtr(), Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    _9E8->makeActorDead();

    _9F0 = 0;
    _3D2 |= 0x10;
}

void MarioActor::initHopperMario() {
    PlayerModel* model;
    const char* modelName;
    const char* archiveName;
    if (gIsLuigi) {
        modelName = "HopperLuigi";
        archiveName = "HopperLuigi.arc";
    } else {
        modelName = "HopperMario";
        archiveName = "HopperMario.arc";
    }

    if (!MR::isLoadedObjectOrLayoutArchive(modelName)) {
        return;
    }

    MR::createAndAddResourceHolder(archiveName);

    _A00 = new ModelHolder(modelName, true);
    _A00->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(_A00));
    model->copyExtraPoses(mModels[0]);

    MR::initMaterialFog(_A00, true);
    MR::prepareMaterials(_A00);

    mModels[5] = model;
    _A00->kill();
    MR::hideModel(_A00);
    MR::hideJoint(_A00, "Face0");
    _3D2 |= 0x20;
}

void MarioActor::initIceMario() {
    PlayerModel* model;
    const char* modelName;
    const char* archiveName;
    if (gIsLuigi) {
        modelName = "IceLuigi";
        archiveName = "IceLuigi.arc";
    } else {
        modelName = "IceMario";
        archiveName = "IceMario.arc";
    }

    if (!MR::isLoadedObjectOrLayoutArchive(modelName)) {
        return;
    }

    MR::createAndAddResourceHolder(archiveName);

    _9C0 = new ModelHolder(modelName, true);
    _9C0->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(_9C0));
    model->copyExtraPoses(mModels[0]);
    model->copyAnmMtxBuffer(mModels[0]);

    MR::initMaterialFog(_9C0, true);
    MR::prepareMaterials(_9C0);

    mModels[3] = model;
    _9C0->kill();
    MR::hideModel(_9C0);

    _B4C = new IceStep*[20];
    for (u32 i = 0; i < 20; i++) {
        _B4C[i] = new IceStep("\203\101\203\103\203\130\217\260" /* アイス床 */);
        _B4C[i]->initWithoutIter();
        _B4C[i]->kill();
    }

    _B50 = 0;
    _3D2 |= 0x8;
}

void MarioActor::initInvincibleMario() {
    PlayerModel* model;
    const char* modelName;
    const char* archiveName;
    if (gIsLuigi) {
        modelName = "InvincibleLuigi";
        archiveName = "InvincibleLuigi.arc";
    } else {
        modelName = "InvincibleMario";
        archiveName = "InvincibleMario.arc";
    }

    if (!MR::isLoadedObjectOrLayoutArchive(modelName)) {
        return;
    }

    MR::createAndAddResourceHolder(archiveName);

    _9C8 = new ModelHolder(modelName, true);
    _9C8->initWithoutIter();

    model = static_cast< PlayerModel* >(MR::getModel(_9C8));
    model->copyExtraPoses(mModels[0]);
    model->copyAnmMtxBuffer(mModels[0]);

    MR::initMaterialFog(_9C8, true);
    MR::prepareMaterials(_9C8);

    mModels[4] = model;
    _9C8->kill();
    MR::hideModel(_9C8);
    MR::startBrk(_9C8, "InvincibleMario");
    _3D2 |= 0x2;
}

void MarioActor::initTeresaMario() {
    const char* modelName = gIsLuigi ? "TeresaLuigi" : "TeresaMario";
    if (!MR::isLoadedObjectOrLayoutArchive(modelName)) {
        return;
    }

    _9A4 = new MarioParts(this, "\203\145\203\214\203\124\202\314\224\347" /* テレサの皮 */, modelName, nullptr);
    if (gIsLuigi) {
        _9A4->_9C = "TeresaMario";
    }

    _9A4->initWithoutIter();
    _9A4->kill();

    _9A4->initFixedPosition(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);

    _9A8 = 0.0f;
    _9AC = 0.0f;
    _9B4 = 0;

    MR::initShadowVolumeSphere(_9A4, 90.0f);
    _3D2 |= 0x40;
    initTeresaMarioAnimation();
}

void MarioActor::initTornadoMario() {
    const char* modelName = gIsLuigi ? "LuigiTornado" : "MarioTornado";
    if (!MR::isLoadedObjectOrLayoutArchive(modelName)) {
        return;
    }

    mTornadoMario = new TornadoMario(modelName);
    mTornadoMario->initWithoutIter();
    _990 = 0;
    _3D2 |= 0x200;
}

void MarioActor::initBoneMario() {
    const char* modelName = gIsLuigi ? "BoneLuigi" : "BoneMario";

    _A04 = new ModelHolder(modelName, true);
    _A04->initWithoutIter();

    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(_A04));
    model->copyExtraPoses(mModels[0]);

    MR::initMaterialFog(_A04, true);
    MR::prepareMaterials(_A04);

    mModels[1] = model;
    _A04->kill();
    MR::hideModel(_A04);
    _3D2 |= 0x100;
}

void MarioActor::initModPowerUpModels() {
    for (s32 i = 0; i < NebulaMods::powerUpCount() && i < 58; i++) {
        const char* pName = NebulaMods::powerUpModel(i);
        if (pName == nullptr) {
            continue;
        }
        MR::mountArchiveByObjectOrLayoutName(pName, nullptr);
        if (!MR::isLoadedObjectOrLayoutArchive(pName)) {
            continue;
        }
        char archive[256];
        nebula_snprintf(archive, sizeof(archive), "%s.arc", pName);
        MR::createAndAddResourceHolder(archive);
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

    if (_494 != nullptr) {
        MtxPtr handMtx = MR::getJointMtx(this, "HandR");
        _494->setBaseMtx(handMtx);
    }

    MtxPtr handMtx = MR::getJointMtx(this, "HandR");
    mSearchLightThrowPos->setBaseMtx(handMtx);
}

void MarioActor::calcViewAndEntry() {
    decideShadowMode();

    PlayerModel* model = getModel();
    model->mFlags._1C = false;
    model->swapDrawBuffer(4);
    getSimpleModel()->swapDrawBuffer(4);

    calcViewMainModel();
    calcViewSilhouetteModel();
    calcViewWallShadowModel();
    model->setDrawView(0);
    calcViewBlurModel();

    if (_B48 != nullptr) {
        calcViewFootPrint();
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        calc1stPersonView();
    } else {
        calcFogLighting();
    }

    switch (mCurrModel) {
    case 0:
        swapTexture("EyeLid_v", static_cast< u8 >(_B6A));
        break;
    case 2:
        swapTexture("EyeLid_v", static_cast< u8 >(_B6A + 4));
        break;
    case 5:
        swapTexture("EyeLid_v", static_cast< u8 >(_B6A + 8));
        break;
    default:
        break;
    }

    if (_A08 > 1) {
        calcViewReflectionModel();
    } else {
        _A24 = 0;
        _A25 = 0;
    }

    calcViewSearchLight();

    if (_A40 != nullptr) {
        _A40->updateMtx(_C8C.toMtxPtr());
    }

    if (_A44 != nullptr) {
        _A44->updateMtx(_CBC.toMtxPtr());
    }

    _A48->updateMtx(_C8C.toMtxPtr());

    _A4C->updateMtx(_CBC.toMtxPtr());

    if (_A50 != nullptr) {
        _A50->updateMtx(_C8C.toMtxPtr());
    }

    if (_A54 != nullptr) {
        _A54->updateMtx(_CBC.toMtxPtr());
    }

    _A5C->updateMtx(_CEC.toMtxPtr());

    updateDarkMask(0x96);
    updateDarkMask(0x96);

    if (MR::isDemoActive()) {
        _1B8->kill();
    }
}

void MarioActor::drawMarioModel() const {
    if (isAllHidden()) {
        return;
    }

    if (mTornadoMario != nullptr) {
        if (mMario->mMovementStates._F) {
            if (mMario->_544 > 2) {
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
        model->mFlags._10 = true;
    }

    if (!mFlag.mIsHiddenModel) {
        model->mFlags._1C = false;
        if (mMario->isStatusActive(MarioStatus_FpView)) {
            if (_1A1) {
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
        model->mFlags.clear();
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        MR::hideJoint(model, "HandR0");
        MR::hideJoint(model, "HandL0");
        MR::hideJoint(model, "Face0");
    }

    drawHand();

    if (!MR::isHiddenModel(_A5C)) {
        PlayerModel* cool = static_cast< PlayerModel* >(MR::getModel(_A5C));
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
    if (_482 || _483 || _481) {
        hidden = true;
    }

    return hidden;
}

void MarioActor::swapTextureInit() {
    ModelData* actorData = MR::getModelData(this);
    const u32 textureCount = actorData->getTextureNum();
    _B60 = textureCount;
    _B64 = new ResTIMG*[textureCount];
    _B6A = 0;

    for (u32 i = 0; i < textureCount; i++) {
        _B64[i] = MR::getResTIMG(this, i);
    }

    _B6C = new const void*[12]();

    u32 texNo = 0;
    for (u32 i = 0; i < textureCount; i++) {
        ModelData* modelData = MR::getModelData(this);
        const char* texName = modelData->getTextureName(i);
        if (strcmp(texName, "mario_eyeLid.0") == 0) {
            texNo = i;
            break;
        }
    }

    _B70 = texNo;
    _B6C[0] = _B64[texNo]->image();
    _B6C[1] = _B64[texNo + 1]->image();
    _B6C[2] = _B64[texNo + 2]->image();
    _B6C[3] = _B64[texNo + 3]->image();

    if (_9E4 != nullptr) {
        ModelData* beeData = MR::getModelData(_9E4);
        const u32 textureNum = beeData->getTextureNum();

        for (u32 i = 0; i < textureNum; i++) {
            ModelData* modelData = MR::getModelData(_9E4);
            const char* texName = modelData->getTextureName(i);
            if (strcmp(texName, "mario_eyeLid.0") == 0) {
                texNo = i;
                break;
            }
        }

        for (u32 i = 0; i < textureNum; i++) {
            _B64[i] = MR::getResTIMG(_9E4, i);
        }

        _B6C[4] = _B64[texNo]->image();
        _B6C[5] = _B64[texNo + 1]->image();
        _B6C[6] = _B64[texNo + 2]->image();
        _B6C[7] = _B64[texNo + 3]->image();
    }

    if (_A00 != nullptr) {
        ModelData* hopperData = MR::getModelData(_A00);
        const u32 textureNum = hopperData->getTextureNum();

        for (u32 i = 0; i < textureNum; i++) {
            ModelData* modelData = MR::getModelData(_A00);
            const char* texName = modelData->getTextureName(i);
            if (strcmp(texName, "mario_eyeLid.0") == 0) {
                texNo = i;
                break;
            }
        }

        for (u32 i = 0; i < textureNum; i++) {
            _B64[i] = MR::getResTIMG(_A00, i);
        }

        _B6C[8] = _B64[texNo]->image();
        _B6C[9] = _B64[texNo + 1]->image();
        _B6C[10] = _B64[texNo + 2]->image();
        _B6C[11] = _B64[texNo + 3]->image();
    }

    initBlink();
}

void MarioActor::initFace() {
    _A5B = 8;
    MR::hideJoint(mModels[0], "Face0");
    _A60 = 0;

    if (gIsLuigi) {
        _A5C = new ModelHolder("LuigiFace", true);
    } else {
        _A5C = new ModelHolder("MarioFace", true);
    }

    MR::initMaterialFog(_A5C, true);
    MR::prepareMaterials(_A5C);

    MR::getModelData(_A5C)->useTexturesOf(MR::getModelData(this));

    for (u32 i = 0; i < _A5B; i++) {
        copyMaterial(MR::getModel(_A5C), MR::getJointIndex(this, "Face0"), i);
    }
}

void MarioActor::updateFace() {
    switch (mCurrModel) {
    case 1:
    case 3:
    case 4:
        MR::hideModel(_A5C);
        return;
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        MR::hideModel(_A5C);
        return;
    }

    if (MR::isHiddenModel(this) || is481or482On() || mMario->mMovementStates._F || _1C3) {
        MR::hideModel(_A5C);
        return;
    }

    MR::showModel(_A5C);

    Vec3f partsControl;
    if (mMarioAnim->_6C) {
        XanimePlayer* player = mMarioAnim->mXanimePlayerUpper;
        const s32 partsControlJoint = MR::getJointIndex(this, "PartsControl");
        player->getMainAnimationTrans(static_cast< u32 >(partsControlJoint), &partsControl);
    } else {
        XanimePlayer* player = mMarioAnim->mXanimePlayer;
        const s32 partsControlJoint = MR::getJointIndex(this, "PartsControl");
        player->getMainAnimationTrans(static_cast< u32 >(partsControlJoint), &partsControl);
    }

    _A60 = MR::clamp(static_cast< s32 >(0.49f + partsControl.x / 10.0f), 0, _A5B - 1);

    for (u32 i = 0; i < _A5B; i++) {
        Joint* eyeJoint = MR::getJoint(_A5C, i + 1);
        if (_A60 == i) {
            MR::showJoint(eyeJoint);
        } else {
            MR::hideJoint(eyeJoint);
        }
    }
}

void MarioActor::initBlur() {
    for (u32 i = 0; i < 2; i++) {
        for (u32 j = 0; j < 8; j++) {
            Model::initPose(&mBlurPoses[i][j]);
        }
    }

    _A6E = false;
    _B12 = 0;
    _B10 = 0;
}

PlayerModel* MarioActor::getModel() const {
    return mModels[mCurrModel];
}

ModelData* MarioActor::getModelData() const {
    return getModel()->mModelData;
}

PlayerModel* MarioActor::getSimpleModel() const {
    return _A14;
}

void MarioActor::calcViewMainModel() {
    PlayerModel* model = mModels[mCurrModel];
    if (mFlag.mIsNoCalcView) {
        return;
    }

    model->viewCalc3(0, nullptr);

    Mtx invView;
    Mtx invBase;
    NebulaMtxInverse(_BF8, invView);
    NebulaMtxInverse(mModels[mCurrModel]->mBaseTransformMtx, invBase);
    MR::multMtx(_BC8, invBase, invView);
}

void MarioActor::draw() const {
    if (_B48 != nullptr) {
        _B48->draw();
    }

    if (!is481or482On() && !_A61 && !_1C3) {
        if (_A0C) {
            drawWallShade(_1F0, _1FC, _208);
        }

        drawMarioModel();
        mMario->draw();
    }
}

void MarioActor::drawIndirect() const {
    drawModelBlur();
    drawIndirectModel();

    if (mMario->_1C._3) {
        drawColdWaterDamage();
    }

    if (_1E4 != 0.0f) {
        drawRasterScroll(_1E4, static_cast< s16 >(_1E8), _1EC);
    }

    if (_390) {
        drawMosaic();
    }

    if (_3DA) {
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
        if (mMario->mMovementStates._F) {
            if (mMario->_544 > 2) {
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
        model->mFlags._1C = false;
        if (mMario->isStatusActive(MarioStatus_FpView)) {
            if (_1A1) {
                return;
            }

            model->setDynamicList(&mAlphaTint);
        } else {
            model->setDynamicList(&mLightTint);
        }

        MR::showJoint(model, "Face0");
        model->setDrawView(0);
        model->directDraw();
        model->mFlags.clear();
    }

    drawIndirectHand();
}

void MarioActor::drawReflectModel() const {
    if (isAllHidden()) {
        return;
    }

    if (_A08 >= 6 && _A24 < 2) {
        return;
    }

    if (_A08 == 3 && _A25 < 2) {
        return;
    }

    if (!(_A08 & 2)) {
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

    if ((_468 == 0 ? nullptr : _428[0]) != nullptr) {
        if (isUseScreenBox()) {
            static_cast< PlayerModel* >(MR::getModel(_9A0))->setDynamicList(&mAlphaTint);
        }

        _9A0->drawType0();
    }

    model->mFlags.clear();

    if (_A08 == 2) {
        Frame::setZScaleOffset(1.0f, 0.00001f);
        model->mFlags._1A = true;
        model->setDrawView(3);
        model->directDraw();
        model->mFlags._1A = false;
        model->mFlags._1D = true;
        model->setDynamicList(&mAlphaTint);
    } else {
        if (isUseScreenBox()) {
            model->setDynamicList(&mAlphaTint);
        }

        model->mFlags._14 = true;
    }

    Frame::setZScaleOffset(1.0f, 0.0f);
    model->setDrawView(3);
    model->directDraw();
    model->mFlags.clear();

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
    if (!model->_1E4) {
        model->_1E5 = true;
        return;
    }

    model->_1E4 = false;
    if (!_A6E) {
        return;
    }

    model->mFlags._13 = true;

    Mtx inv;
    NebulaMtxInverse(const_cast< Mtx34f& >(_AB0).toMtxPtr(), inv);
    MR::multMtx(inv, inv, MR::getCameraViewMtx());
    for (u32 i = 1; i < 8; i++) {
        const u32 idx = static_cast< u32 >(i + _B12) & 7;
        ModelPose& rPose = const_cast< MarioActor* >(this)->mBlurPoses[_B10][idx];
        if (!_1C1) {
            Model::copyPose(&rPose, mBlurPoses[1 - _B10][idx]);
            Model::movePoseView(&rPose, inv);
        }
        model->setDrawPose(&rPose);


        model->setDynamicList(&mGhostTints[idx + ((i - 1) << 3)]);
        model->directDraw();
    }

    model->mFlags._13 = false;
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

    mModels[mCurrModel]->_1C8[materialNo] = _B6C[texNo];
}

void MarioActor::copyMaterial(Model* pModel, u32 materialNo, s32 packetIndex) {
    ModelData* modelData = getModel()->getModelData();
    Material* material = modelData->getJointNodePointer(materialNo)->getMesh();
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
        MaterialEntry* matPacket = pModel->getMatPacket(i);
        matPacket->mMaterial = modelData->getMaterialNodePointer(materialIndex);
        ShapeEntry* shapePacket = pModel->getShapePacket(i);
        matPacket->mInitShapeEntry = shapePacket;
        matPacket->addShapeEntry(shapePacket);
        shapePacket->mMaterial = matPacket->mMaterial;
    }
}
