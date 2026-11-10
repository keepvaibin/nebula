#include "Game/MapObj/DummyDisplayModel.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/CoinRotater.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaMods.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    // By DummyDisplayModelId.
    const DummyDisplayModelInfo cDummyDisplayModelInfoTable[] = {
        {"Coin", {0.0f, 70.0f, 0.0f}, MR::DrawBufferType_NoSilhouettedMapObjStrongLight, nullptr, false},
        {"Kinopio", {0.0f, 50.0f, 0.0f}, MR::DrawBufferType_NPC, "Freeze", true},
        {"SpinDriver", {0.0f, 0.0f, 0.0f}, MR::DrawBufferType_NoShadowedMapObj, nullptr, false},
        {"SuperSpinDriver", {0.0f, 0.0f, 0.0f}, MR::DrawBufferType_NoShadowedMapObj, "Freeze", false},
        {"StarPieceDummy", {-30.0f, 100.0f, -30.0f}, MR::DrawBufferType_NoSilhouettedMapObj, "Freeze", false},
        {"Tico", {0.0f, 50.0f, 0.0f}, MR::DrawBufferType_NPC, nullptr, true},
        {"KeySwitch", {0.0f, 0.0f, 0.0f}, MR::DrawBufferType_MapObjStrongLight, "InRotation", false},
        {"PowerStar", {0.0f, 0.0f, 0.0f}, MR::DrawBufferType_NoSilhouettedMapObj, nullptr, false},
        {"KinokoOneUp", {0.0f, 40.0f, 0.0f}, MR::DrawBufferType_NoSilhouettedMapObj, nullptr, false},
        {"Kuribo", {0.0f, 80.0f, 0.0f}, MR::DrawBufferType_Enemy, nullptr, false},
        {"BlueChip", {0.0f, 0.0f, 0.0f}, MR::DrawBufferType_NoShadowedMapObj, nullptr, false},
        {"YellowChip", {0.0f, 0.0f, 0.0f}, MR::DrawBufferType_NoShadowedMapObj, nullptr, false},
        {"StrayTico", {0.0f, 50.0f, 0.0f}, MR::DrawBufferType_NPC, nullptr, false},
        {"GrandStar", {0.0f, 0.0f, 0.0f}, MR::DrawBufferType_NoSilhouettedMapObj, nullptr, false},
        {"KinokoLifeUp", {0.0f, 40.0f, 0.0f}, MR::DrawBufferType_NoSilhouettedMapObj, nullptr, false},
    };

    const s32 cDummyDisplayModelCount = sizeof(cDummyDisplayModelInfoTable) / sizeof(cDummyDisplayModelInfoTable[0]);

    // Arg 6: the color (ColorChange frame) of models that have one.
    s32 getColorFrame(const TableIter& rIter) {
        s32 colorFrame = 0;

        if (MR::isValidInfo(rIter)) {
            MR::getPlacementArg6NoInit(rIter, &colorFrame);
        }

        return colorFrame;
    }

    // `drawBufferType` -1 takes the model's own; no model for id -1, for a coin where a dark comet shows, or when a mod
    // removes it (a mod may also show another row's model).
    DummyDisplayModel* tryCreateDummyModel(LiveActor* pHost, const TableIter& rIter, s32 defaultModelId, int drawBufferType) {
        s32 modelId = MR::getDummyDisplayModelId(rIter, defaultModelId);

        if (modelId < 0 || modelId >= cDummyDisplayModelCount) {
            return nullptr;
        }

        if (modelId == DummyDisplayModelId_Coin && MR::isGalaxyDarkCometAppearInCurrentStage()) {
            return nullptr;
        }

        const char* pShown = NebulaMods::objectBecomes(NEBULA_OBJECTS_DISPLAY_MODEL, cDummyDisplayModelInfoTable[modelId].mName);
        if (pShown == nullptr) {
            return nullptr;
        }

        for (s32 i = 0; i < cDummyDisplayModelCount; i++) {
            if (MR::isEqualString(cDummyDisplayModelInfoTable[i].mName, pShown)) {
                modelId = i;
                break;
            }
        }

        const DummyDisplayModelInfo* pInfo = &cDummyDisplayModelInfoTable[modelId];
        DummyDisplayModel* pModel =
            new DummyDisplayModel(pHost, pInfo, drawBufferType < 0 ? pInfo->mDrawBufferType : drawBufferType, modelId, getColorFrame(rIter));
        pModel->initWithoutIter();
        return pModel;
    }
};  // namespace

DummyDisplayModel::DummyDisplayModel(LiveActor* pHost, const DummyDisplayModelInfo* pInfo, int drawBufferType, s32 modelId, s32 colorFrame)
    : PartsModel(pHost, pInfo->mName, pInfo->mName, nullptr, drawBufferType, 0) {
    lumacore::setClass(this, lumacore::kClassId< DummyDisplayModel >);
    mModelInfo = pInfo;
    mItemType = modelId;
    mBrkFrame = colorFrame;
    mLodCtrl = nullptr;
    mIsInCrystal = drawBufferType == MR::DrawBufferType_CrystalItem;
    mScale.x = 1.0f;
    mScale.y = 1.0f;
    mScale.z = 1.0f;

    if (modelId == DummyDisplayModelId_Coin && !mIsInCrystal) {
        MR::createCoinRotater();
    }
}

// Stars light themselves (with the host's color); a toad's model level follows the distance. Inside a crystal
// nothing casts a shadow.
void DummyDisplayModel::init(const TableIter& rIter) {
    PartsModel::init(rIter);

    if (mModelInfo->mAnim != nullptr) {
        MR::startBck(this, mModelInfo->mAnim);
    }

    if (mModelInfo->mHasColorChange) {
        MR::startBrk(this, "ColorChange");
        MR::setBrkFrameAndStop(this, mBrkFrame);
    }

    if (mItemType != DummyDisplayModelId_GrandStar && MR::getLightNumMax(this) > 0) {
        MR::initLightCtrl(this);
    }

    switch (mItemType) {
    case DummyDisplayModelId_StarPiece:
        if (!mIsInCrystal) {
            MR::initShadowFromCSV(this, "Shadow");
        }
        break;
    case DummyDisplayModelId_PowerStar:
    case DummyDisplayModelId_GrandStar:
        if (!mIsInCrystal) {
            PowerStar::initShadowPowerStar(this, false);
        }

        PowerStar::setupColor(this, mHost, -1);
        MR::emitEffect(this, "Light");
        break;
    case DummyDisplayModelId_Kinopio:
        mLodCtrl = new LodCtrl(this, rIter);
        break;
    }

    MR::registerDemoSimpleCastAll(this);
}

void DummyDisplayModel::makeActorAppeared() {
    PartsModel::makeActorAppeared();

    if (mLodCtrl != nullptr) {
        mLodCtrl->appear();
    }
}

void DummyDisplayModel::makeActorDead() {
    if (mLodCtrl != nullptr) {
        mLodCtrl->kill();
    }

    PartsModel::makeActorDead();
}

void DummyDisplayModel::control() {
    switch (mItemType) {
    case DummyDisplayModelId_PowerStar:
    case DummyDisplayModelId_GrandStar:
        PowerStar::requestPointLight(this, mHost, -1);
        break;
    case DummyDisplayModelId_Kinopio:
        mLodCtrl->update();
        break;
    }
}

// The model sits its offset below the fixed position; outside a crystal coins share the coin spin and stars turn by
// the host's Y rotation.
void DummyDisplayModel::calcAndSetBaseMtx() {
    PartsModel::calcAndSetBaseMtx();
    MtxPtr baseMtx = getBaseMtx();

    Vec3f modelOffset = mModelInfo->mOffset;
    Vec3f offset;
    offset.negate(modelOffset);
    NebulaMtxMultVec(baseMtx, offset, &mPosition);
    MR::setMtxTrans(baseMtx, mPosition);

    if (mIsInCrystal) {
        return;
    }

    switch (mItemType) {
    case DummyDisplayModelId_Coin:
        MR::multMtx(baseMtx, MR::getCoinRotateYMatrix(), baseMtx);
        break;
    case DummyDisplayModelId_PowerStar:
    case DummyDisplayModelId_GrandStar: {
        Pos3f rot;
        rot.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), MR::toRadian(mRotation.y));
        MR::multMtx(baseMtx, rot, baseMtx);
        break;
    }
    }
}

namespace MR {
    DummyDisplayModel* createDummyDisplayModel(LiveActor* pHost, const TableIter& rIter, s32 defaultModelId, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
        DummyDisplayModel* pModel = tryCreateDummyModel(pHost, rIter, defaultModelId, -1);

        if (pModel == nullptr) {
            return nullptr;
        }

        pModel->initFixedPosition(rLocalTrans, rLocalRotate, nullptr);
        return pModel;
    }

    DummyDisplayModel* createDummyDisplayModel(LiveActor* pHost, const TableIter& rIter, const char* pJointName, s32 defaultModelId) {
        DummyDisplayModel* pModel = tryCreateDummyModel(pHost, rIter, defaultModelId, -1);

        if (pModel == nullptr) {
            return nullptr;
        }

        pModel->initFixedPosition(pJointName);
        return pModel;
    }

    DummyDisplayModel* createDummyDisplayModel(LiveActor* pHost, const TableIter& rIter, MtxPtr pMtx, s32 defaultModelId, const Vec3f& rLocalTrans,
                                               const Vec3f& rLocalRotate) {
        DummyDisplayModel* pModel = tryCreateDummyModel(pHost, rIter, defaultModelId, -1);

        if (pModel == nullptr) {
            return nullptr;
        }

        pModel->initFixedPosition(pMtx, rLocalTrans, rLocalRotate);
        return pModel;
    }

    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor* pHost, const TableIter& rIter, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
        DummyDisplayModel* pModel = tryCreateDummyModel(pHost, rIter, -1, MR::DrawBufferType_CrystalItem);

        if (pModel == nullptr) {
            return nullptr;
        }

        pModel->initFixedPosition(rLocalTrans, rLocalRotate, nullptr);
        return pModel;
    }

    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor* pHost, s32 modelId, const Vec3f& rLocalTrans, const Vec3f& rLocalRotate) {
        DummyDisplayModel* pModel = tryCreateDummyModel(pHost, TableIter(), modelId, MR::DrawBufferType_CrystalItem);

        if (pModel == nullptr) {
            return nullptr;
        }

        pModel->initFixedPosition(rLocalTrans, rLocalRotate, nullptr);
        return pModel;
    }

    // Arg 7, else `defaultModelId`.
    s32 getDummyDisplayModelId(const TableIter& rIter, s32 defaultModelId) {
        s32 modelId = -1;

        if (MR::isValidInfo(rIter)) {
            MR::getPlacementArg7NoInit(rIter, &modelId);
        }

        if (modelId == -1 && defaultModelId != -1) {
            modelId = defaultModelId;
        }

        return modelId;
    }

    s32 getDummyDisplayModelId(const LiveActor* pActor) {
        return static_cast< const DummyDisplayModel* >(pActor)->mItemType;
    }
};  // namespace MR
