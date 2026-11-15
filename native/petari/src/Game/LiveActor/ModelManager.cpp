#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Animation/BckCtrl.hpp"
#include "Game/Animation/BpkPlayer.hpp"
#include "Game/Animation/BrkPlayer.hpp"
#include "Game/Animation/BtkPlayer.hpp"
#include "Game/Animation/BtpPlayer.hpp"
#include "Game/Animation/BvaPlayer.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/LiveActor/MaterialUpdater.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Util/StringUtil.hpp"

ModelManager::ModelManager()
    : mBtkPlayer(), mBrkPlayer(), mBtpPlayer(), mBpkPlayer(), mBvaPlayer(), mXanimeResourceTable(), mXanimePlayer(), mModel(), mModelResourceHolder(),
      mMaterialUpdater(), mNoBckCtrl() {
}

void ModelManager::update() {
    XanimePlayer* pXanimePlayer = mXanimePlayer;

    if (pXanimePlayer != nullptr) {
        pXanimePlayer->updateBeforeMovement();
        pXanimePlayer->updateAfterMovement();
    }

    if (mBtkPlayer != nullptr) {
        mBtkPlayer->update();
    }

    if (mBrkPlayer != nullptr) {
        mBrkPlayer->update();
    }

    if (mBtpPlayer != nullptr) {
        mBtpPlayer->update();
    }

    if (mBpkPlayer != nullptr) {
        mBpkPlayer->update();
    }

    if (mBvaPlayer != nullptr) {
        mBvaPlayer->update();
    }
}

void ModelManager::calcAnim() {
    calc();
    updateList();
}

void ModelManager::calcView() {
    getModel()->viewCalc();
}

void ModelManager::entry() {
    getModel()->entry();
}

void ModelManager::prepareMaterials() {
    mMaterialUpdater->prepareMaterials();
}

void ModelManager::updateList() {
    Model* pModel = getModel();
    if (mMaterialUpdater != nullptr) {
        mMaterialUpdater->update();

        if (mBtkPlayer != nullptr) {
            mBtkPlayer->applyFrame(pModel);
        }

        if (mBrkPlayer != nullptr) {
            mBrkPlayer->applyFrame(pModel);
        }

        if (mBtpPlayer != nullptr) {
            mBtpPlayer->applyFrame(pModel);
        }

        if (mBpkPlayer != nullptr) {
            mBpkPlayer->applyFrame(pModel);
        }
    }

    pModel->calcMaterial();
}

void ModelManager::startBck(const char* pBckName, const char* pActorAnimName) {
    if (mXanimePlayer == nullptr) {
        return;
    }

    mXanimePlayer->changeAnimationBck(pBckName);
    mXanimePlayer->changeInterpoleFrame(0);
    changeBckSetting(pBckName, pActorAnimName, nullptr);
}

void ModelManager::startBckWithInterpole(const char* pBckName, s32 interpole) {
    if (mXanimePlayer == nullptr) {
        return;
    }

    mXanimePlayer->changeAnimationBck(pBckName);
    mXanimePlayer->changeInterpoleFrame(0);
    mXanimePlayer->changeInterpoleFrame(interpole);
}

void ModelManager::startBtk(const char* pBtkName) {
    stopBtk();
    btkPlayer()->start(pBtkName);
}

void ModelManager::startBrk(const char* pBrkName) {
    stopBrk();
    brkPlayer()->start(pBrkName);
}

void ModelManager::startBtp(const char* pBtpName) {
    stopBtp();
    btpPlayer()->start(pBtpName);
}

void ModelManager::startBpk(const char* pBpkName) {
    stopBpk();
    bpkPlayer()->start(pBpkName);
}

void ModelManager::startBva(const char* pBvaName) {
    stopBva();
    bvaPlayer()->start(pBvaName);
}

void ModelManager::stopBtk() {
    if (mBtkPlayer != nullptr && mBtkPlayer->mAnmRes != nullptr) {
        mBtkPlayer->stop();
    }
}

void ModelManager::stopBrk() {
    if (mBrkPlayer != nullptr && mBrkPlayer->mAnmRes != nullptr) {
        mBrkPlayer->stop();
    }
}

void ModelManager::stopBtp() {
    if (mBtpPlayer != nullptr && mBtpPlayer->mAnmRes != nullptr) {
        mBtpPlayer->stop();
    }
}

void ModelManager::stopBpk() {
    if (mBpkPlayer != nullptr && mBpkPlayer->mAnmRes != nullptr) {
        mBpkPlayer->stop();
    }
}

void ModelManager::stopBva() {
    if (mBvaPlayer != nullptr && mBvaPlayer->mAnmRes != nullptr) {
        mBvaPlayer->stop();
    }
}

FrameCtrl* ModelManager::getBckCtrl() const {
    if (mXanimePlayer == nullptr) {
        if (mNoBckCtrl == nullptr) {
            const NebulaArena::CurrentScope scope(arena());
            mNoBckCtrl = new FrameCtrl();
        }

        return mNoBckCtrl;
    }

    return mXanimePlayer->getFrameCtrl();
}

FrameCtrl* ModelManager::getBtkCtrl() const {
    return &btkPlayer()->mFrameCtrl;
}

FrameCtrl* ModelManager::getBrkCtrl() const {
    return &brkPlayer()->mFrameCtrl;
}

FrameCtrl* ModelManager::getBtpCtrl() const {
    return &btpPlayer()->mFrameCtrl;
}

FrameCtrl* ModelManager::getBpkCtrl() const {
    return &bpkPlayer()->mFrameCtrl;
}

FrameCtrl* ModelManager::getBvaCtrl() const {
    return &bvaPlayer()->mFrameCtrl;
}

bool ModelManager::isBckStopped() const {
    return mXanimePlayer == nullptr || mXanimePlayer->isTerminate();
}

bool ModelManager::isBtkStopped() const {
    if (mBtkPlayer != nullptr) {
        return mBtkPlayer->isStop();
    }

    return false;
}

bool ModelManager::isBrkStopped() const {
    if (mBrkPlayer != nullptr) {
        return mBrkPlayer->isStop();
    }

    return false;
}

bool ModelManager::isBtpStopped() const {
    if (mBtpPlayer != nullptr) {
        return mBtpPlayer->isStop();
    }

    return false;
}

bool ModelManager::isBpkStopped() const {
    if (mBpkPlayer != nullptr) {
        return mBpkPlayer->isStop();
    }

    return false;
}

bool ModelManager::isBvaStopped() const {
    if (mBvaPlayer != nullptr) {
        return mBvaPlayer->isStop();
    }

    return false;
}

bool ModelManager::isBtkPlaying(const char* pBtkName) const {
    return mBtkPlayer != nullptr && mBtkPlayer->isPlaying(pBtkName);
}

bool ModelManager::isBrkPlaying(const char* pBrkName) const {
    return mBrkPlayer != nullptr && mBrkPlayer->isPlaying(pBrkName);
}

bool ModelManager::isBpkPlaying(const char* pBpkName) const {
    return mBpkPlayer != nullptr && mBpkPlayer->isPlaying(pBpkName);
}

bool ModelManager::isBtpPlaying(const char* pBtpName) const {
    return mBtpPlayer != nullptr && mBtpPlayer->isPlaying(pBtpName);
}

bool ModelManager::isBvaPlaying(const char* pBvaName) const {
    return mBvaPlayer != nullptr && mBvaPlayer->isPlaying(pBvaName);
}

void ModelManager::initJointTransform() {
    mXanimePlayer->mCore->enableJointTransform(getModelData());
}

XjointTransform* ModelManager::getJointTransform(const char* pJointName) {
    s32 index = getModel()->getModelData()->findJoint(pJointName);
    // A joint name the model lacks is the root joint.
    return mXanimePlayer->mCore->getJointTransform(index >= 0 ? index : 0);
}

ResourceHolder* ModelManager::getResourceHolder() const {
    if (mXanimeResourceTable == nullptr) {
        return mModelResourceHolder;
    }

    return mXanimeResourceTable->mResourceHolder;
}

ResourceHolder* ModelManager::getModelResourceHolder() const {
    return mModelResourceHolder;
}

Model* ModelManager::getModel() const {
    if (mXanimePlayer == nullptr) {
        return mModel;
    }

    return mXanimePlayer->mModel;
}

ModelData* ModelManager::getModelData() const {
    return getModel()->mModelData;
}

const char* ModelManager::getPlayingBckName() const {
    if (mXanimePlayer != nullptr) {
        return mXanimePlayer->getCurrentBckName();
    }

    return nullptr;
}

void ModelManager::initModelAndAnimation(ResourceHolder* pModelHolder, const char* pModelName, ResourceHolder* pAnimationHolder, ModelFlag flags) {
    mModelResourceHolder = pModelHolder;
    if (pAnimationHolder->mMotionResTable->mCount == 0) {
        mModel = MR::createModel(pModelHolder, pModelName, flags);
    } else {
        mXanimeResourceTable = MR::newXanimeResourceTable(pAnimationHolder);
        mXanimePlayer = MR::newXanimePlayer(pModelHolder, pModelName, pAnimationHolder, flags, mXanimeResourceTable);
    }
}

// The arena the manager lies in (its actor's): what it makes later lives as long as it does.
NebulaArena* ModelManager::arena() const {
    NebulaArena* const pArena = NebulaArena::findFromRoot(const_cast< ModelManager* >(this));
    return pArena != nullptr ? pArena : NebulaArena::current();
}

BtkPlayer* ModelManager::btkPlayer() const {
    if (mBtkPlayer == nullptr) {
        const NebulaArena::CurrentScope scope(arena());
        mBtkPlayer = new BtkPlayer(getResourceHolder(), getModelData());
    }

    return mBtkPlayer;
}

BrkPlayer* ModelManager::brkPlayer() const {
    if (mBrkPlayer == nullptr) {
        const NebulaArena::CurrentScope scope(arena());
        mBrkPlayer = new BrkPlayer(getResourceHolder(), getModelData());
    }

    return mBrkPlayer;
}

BtpPlayer* ModelManager::btpPlayer() const {
    if (mBtpPlayer == nullptr) {
        const NebulaArena::CurrentScope scope(arena());
        mBtpPlayer = new BtpPlayer(getResourceHolder(), getModelData());
    }

    return mBtpPlayer;
}

BpkPlayer* ModelManager::bpkPlayer() const {
    if (mBpkPlayer == nullptr) {
        const NebulaArena::CurrentScope scope(arena());
        mBpkPlayer = new BpkPlayer(getResourceHolder(), getModelData());
    }

    return mBpkPlayer;
}

BvaPlayer* ModelManager::bvaPlayer() const {
    if (mBvaPlayer == nullptr) {
        const NebulaArena::CurrentScope scope(arena());
        mBvaPlayer = new BvaPlayer(getResourceHolder()->getBvaResTable(), getModel());
    }

    return mBvaPlayer;
}

void ModelManager::initMaterialAnm() {
    ResourceHolder* pResourceHolder = getResourceHolder();
    ModelData* pModelData = getModelData();

    if (!pResourceHolder->mMaterialBound) {
        pResourceHolder->bindMaterialAnimations(pModelData);
    }

    if (pResourceHolder->mBtkResTable->mCount != 0) {
        mBtkPlayer = new BtkPlayer(pResourceHolder, pModelData);
    }

    if (pResourceHolder->mBrkResTable->mCount != 0) {
        mBrkPlayer = new BrkPlayer(pResourceHolder, pModelData);
    }

    if (pResourceHolder->mBtpResTable->mCount != 0) {
        mBtpPlayer = new BtpPlayer(pResourceHolder, pModelData);
    }

    if (pResourceHolder->mBpkResTable->mCount != 0) {
        mBpkPlayer = new BpkPlayer(pResourceHolder, pModelData);
    }
}

void ModelManager::initVisibilityAnm() {
    ResourceHolder* pResourceHolder = getResourceHolder();

    if (pResourceHolder->mBvaResTable->mCount != 0) {
        mBvaPlayer = new BvaPlayer(pResourceHolder->getBvaResTable(), getModel());
    }
}

void ModelManager::calc() {
    if (mXanimePlayer != nullptr) {
        mXanimePlayer->calcAnm(0);
    }

    if (mBvaPlayer != nullptr) {
        mBvaPlayer->calc();
    }

    getModel()->calc();

    if (mXanimePlayer != nullptr) {
        mXanimePlayer->clearAnm(0);
    }
}

void ModelManager::changeBckSetting(const char* pBckName, const char* pActorAnimName, XanimePlayer* pXanimePlayer) {
    if (pXanimePlayer == nullptr) {
        pXanimePlayer = mXanimePlayer;
    }

    BckCtrl* pBckCtrl = getResourceHolder()->mBckCtrl;

    if (pBckCtrl != nullptr) {
        if (pActorAnimName == nullptr) {
            pActorAnimName = pBckName;
        }

        pBckCtrl->changeBckSetting(pActorAnimName, pXanimePlayer);
    }
}

void ModelManager::init(const char* pModelName, const char* pAnimationName, bool deferMaterials) {
    ResourceHolder* modelHolder = MR::createAndAddResourceHolder(pModelName);
    ResourceHolder* animationHolder = pAnimationName == nullptr ? modelHolder : MR::createAndAddResourceHolder(pAnimationName);
    // The archive by its name, the model in it by the model's (a mod's "<id>:<path>" holds <leaf>.bmd: FileUtil).
    const char* const pResName = MR::getArchiveModelName(pModelName);

    bool hasMaterialAnm = animationHolder->isExistMaterialAnm();
    bool hasMovingMaterial = MaterialUpdater::hasMovingMaterial(static_cast< ModelData* >(modelHolder->mModelResTable->getRes(pResName)));
    initModelAndAnimation(modelHolder, pResName, animationHolder, ModelFlag_None);
    initVisibilityAnm();
    if (hasMaterialAnm) {
        initMaterialAnm();
    }

    if (deferMaterials || hasMaterialAnm || hasMovingMaterial) {
        mMaterialUpdater = new MaterialUpdater(getModel(), getResourceHolder());
        if (!deferMaterials) {
            mMaterialUpdater->prepareMaterials();
        }
    }
}
