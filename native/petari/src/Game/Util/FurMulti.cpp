#include "Game/Util/FurMulti.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/FurCtrl.hpp"
#include "Game/Util/FurDrawer.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cstring>
#include <nebula_format.h>

namespace {
    FurParam sFurParam = {6,
                          10.0f,
                          0.778809f,
                          0.0f,
                          1.0f,
                          0.0f,
                          0.0f,
                          0.6f,
                          255.0f,
                          0.0f,
                          1.0f,
                          80.0f,
                          0.0f,
                          5.419921875f,
                          16.2109375f,
                          1.0f,
                          {54, 48, 44, 0},
                          {0.8f, 0.6f, 0.3f, 0.09f},
                          {0.517f, 0.386f, 0.3645f, 0.1713f},
                          {26, 64, 98, 212}};
};  // namespace

FurMulti::FurMulti(LiveActor* pActor, u32 count) {
    mActor = pActor;
    mModel = MR::getModel(pActor);
    mCount = count;
    mShapeIndices = new u32[count];
    mDrawOrders = new u8[count];
    mFurCtrls = new FurCtrl*[count];

    for (u32 i = 0; i < count; i++) {
        mDrawOrders[i] = 255;
    }

    mAddedToManager = 0;
    _1 = 1;
}

void FurMulti::setLayerDirect(u32 layer, u32 drawOrder, u32 shape, u32 numLayers, FurParam* pParam, ResTIMG* pBody, ResTIMG* pLength,
                              ResTIMG* pIndirect, ResTIMG* pDensity) {
    mShapeIndices[layer] = shape;
    mDrawOrders[layer] = drawOrder;
    mFurCtrls[layer] = new FurCtrl(mActor, pParam, false, 1);
    ModelData* pModelData = mModel->getModelData();
    FurMulti* pClone = MR::getFurDrawManager()->getFurBank()->check(pModelData, layer);

    if (pClone) {
        mFurCtrls[layer]->setupFurClone(mModel, pClone->mFurCtrls[layer]);
        return;
    }

    mFurCtrls[layer]->setupFur(mModel, pLength, pIndirect, pBody, shape, numLayers);
    MR::getFurDrawManager()->mBank->regist(this, layer);
    mFurCtrls[layer]->calcLayerForm();

    if (pDensity) {
        pDensity->mWidth = 32;
        pDensity->mHeight = 32;
        pDensity->mWrapS = NEBULA_WRAP_REPEAT;
        pDensity->mWrapT = NEBULA_WRAP_REPEAT;
        pDensity->mMipmap = false;
        pDensity->mMaxAnisotropy = 0;
        pDensity->mMinType = NEBULA_FILTER_LINEAR;
        pDensity->mMagType = NEBULA_FILTER_LINEAR;
        pDensity->mMinLod = 0;
        pDensity->mMaxLod = 0;
        pDensity->mImageNum = 1;
        pDensity->mLodBias = 0;
        JUTTexture* pTexture = new JUTTexture(pDensity);
        FurDrawer* pDrawer = mFurCtrls[layer]->mDrawer;
        pDrawer->mFurTexture = pTexture;
        pDrawer->mUseDensityMap = 1;
    } else {
        mFurCtrls[layer]->createFurMap();
    }

    mFurCtrls[layer]->mDrawer->update();
}

void FurMulti::addToManager() {
    for (u32 i = 0; i < mCount; i++) {
        MR::getFurDrawManager()->add(mFurCtrls[i], 1);
    }

    mAddedToManager = 1;
}

void FurMulti::offDraw(u32 mask) {
    for (u32 i = 0; i < mCount; i++) {
        if (mask & (1 << i)) {
            mFurCtrls[i]->mVisible = 0;
        }
    }
}

void FurMulti::onDraw(u32 mask) {
    for (u32 i = 0; i < mCount; i++) {
        if (mask & (1 << i)) {
            mFurCtrls[i]->mVisible = 1;
        }
    }
}

namespace {
    void loadFurText(LiveActor* pActor, FurParam* pParam, DynamicFurParam* pDynamic, const char* pName) {
        const FurFile* pFile = static_cast< const FurFile* >(MR::getResourceHolder(pActor)->mFileInfoTable->getRes(pName));
        MR::applyFurFile(pParam, pDynamic->mLightParam, *pFile);
    }
};  // namespace

namespace MR {
    FurMulti* initMultiFur(LiveActor* pActor, s32 lightType) {
        u32 count;
        u32 i;
        u32 shape;
        FurMulti* pMulti;
        CurrentHeapRestorer heapRestorer(getSceneHeap());
        ModelData* pModelData = getModelData(pActor);
        count = 0;

        for (i = 0; i < pModelData->getMaterialCount(); i++) {
            if (strstr(pModelData->getMaterialName(i), "Fur")) {
                count++;
            }
        }

        if (count == 0) {
            return nullptr;
        }

        pMulti = new FurMulti(pActor, count);
        DynamicFurParam dynamicParam;
        dynamicParam.mFogCtrl = new FogCtrl(MR::getModel(pActor), true);
        dynamicParam.mLightParam = new FurLightParam;
        dynamicParam.mLightParam->mLightType = lightType;
        u32 layer = 0;

        for (u32 i = 0; i < pModelData->getMaterialCount(); i++) {
            if (strstr(pModelData->getMaterialName(i), "Fur")) {
                shape = pModelData->getMaterialAt(i)->getMesh()->getIndex();
                char name[256];
                nebula_snprintf(name, sizeof(name), "%sBody", pModelData->getMaterialName(i));
                ResTIMG* pBody;

                if (isExistTexture(pActor, name)) {
                    pBody = getTexFromModel(name, pActor);
                } else {
                    pBody = getResTIMG(pActor, 0);
                }

                nebula_snprintf(name, sizeof(name), "%sLength.bti", pModelData->getMaterialName(i));
                ResTIMG* pLength = nullptr;

                if (getResourceHolder(pActor)->mFileInfoTable->isExistRes(name)) {
                    pLength = static_cast< ResTIMG* >(getResourceHolder(pActor)->mFileInfoTable->getRes(name));
                }

                nebula_snprintf(name, sizeof(name), "%sIndirect.bti", pModelData->getMaterialName(i));
                ResTIMG* pIndirect = nullptr;

                if (getResourceHolder(pActor)->mFileInfoTable->isExistRes(name)) {
                    pIndirect = static_cast< ResTIMG* >(getResourceHolder(pActor)->mFileInfoTable->getRes(name));
                }

                nebula_snprintf(name, sizeof(name), "%sDensity.bti", pModelData->getMaterialName(i));
                ResTIMG* pDensity = nullptr;

                if (getResourceHolder(pActor)->mFileInfoTable->isExistRes(name)) {
                    pDensity = static_cast< ResTIMG* >(getResourceHolder(pActor)->mFileInfoTable->getRes(name));
                }

                FurParam* pParam = new FurParam;
                *pParam = ::sFurParam;
                nebula_snprintf(name, sizeof(name), "%s.fur.txt", pModelData->getMaterialName(i));

                if (getResourceHolder(pActor)->mFileInfoTable->isExistRes(name)) {
                    loadFurText(pActor, pParam, &dynamicParam, name);
                }

                pMulti->setLayerDirect(layer, 0, shape, pParam->mNumLayers, pParam, pBody, pLength, pIndirect, pDensity);
                pMulti->mFurCtrls[layer]->mDynamicParam.mLightParam = dynamicParam.mLightParam;
                pMulti->mFurCtrls[layer]->mDynamicParam.mFogCtrl = dynamicParam.mFogCtrl;
                layer++;
            }
        }

        pMulti->addToManager();
        return pMulti;
    }
};  // namespace MR

void FurBank::regist(FurMulti* pMulti, u32 layer) {
    mEntries.at(mCount) = pMulti;
    mLayerMasks.at(mCount) |= 1 << layer;
    mCount++;
}
