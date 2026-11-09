#include "Game/Util/FurMulti.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/FurCtrl.hpp"
#include "Game/Util/FurDrawer.hpp"
#include "Game/Util/FurParam.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include <cstring>
#include <nebula_format.h>
#include "Game/Util/StringUtil.hpp"

namespace {
    FurParam sFurParam = {6,
                          10.0f,
                          0.778809f,
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
    mFurCtrls = new FurCtrl*[count];
}

void FurMulti::setLayerDirect(u32 layer, u32 shape, u32 numLayers, FurParam* pParam, ResTIMG* pBody, ResTIMG* pLength, ResTIMG* pDensity) {
    mFurCtrls[layer] = new FurCtrl(mActor, pParam, false, 1);
    ModelData* pModelData = mModel->getModelData();
    FurMulti* pClone = MR::getFurDrawManager()->getFurBank()->check(pModelData, layer);

    if (pClone) {
        mFurCtrls[layer]->setupFurClone(mModel, pClone->mFurCtrls[layer]);
        return;
    }

    mFurCtrls[layer]->setupFur(mModel, pLength, pBody, shape, numLayers);
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
        GameTexture* pTexture = new GameTexture(pDensity);
        mFurCtrls[layer]->mDrawer->mFurTexture = pTexture;
    } else {
        mFurCtrls[layer]->createFurMap();
    }
}

void FurMulti::addToManager() {
    for (u32 i = 0; i < mCount; i++) {
        MR::getFurDrawManager()->add(mFurCtrls[i], 1);
    }
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
        NebulaArena::CurrentScope heapRestorer(Arenas::scene());
        ModelData* pModelData = getModelData(pActor);
        count = 0;

        for (i = 0; i < pModelData->getMaterialCount(); i++) {
            if (MR::isEqualSubString(pModelData->getMaterialName(i), "Fur")) {
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
            if (MR::isEqualSubString(pModelData->getMaterialName(i), "Fur")) {
                shape = pModelData->getMaterialAt(i)->getMesh()->getIndex();
                MR::FormattedName name("%sBody", pModelData->getMaterialName(i));
                ResTIMG* pBody;

                if (isExistTexture(pActor, name.c_str())) {
                    pBody = getTexFromModel(name.c_str(), pActor);
                } else {
                    pBody = getResTIMG(pActor, 0);
                }

                name.format("%sLength.bti", pModelData->getMaterialName(i));
                ResTIMG* pLength = nullptr;

                if (getResourceHolder(pActor)->mFileInfoTable->isExistRes(name.c_str())) {
                    pLength = static_cast< ResTIMG* >(getResourceHolder(pActor)->mFileInfoTable->getRes(name.c_str()));
                }

                name.format("%sDensity.bti", pModelData->getMaterialName(i));
                ResTIMG* pDensity = nullptr;

                if (getResourceHolder(pActor)->mFileInfoTable->isExistRes(name.c_str())) {
                    pDensity = static_cast< ResTIMG* >(getResourceHolder(pActor)->mFileInfoTable->getRes(name.c_str()));
                }

                FurParam* pParam = new FurParam;
                *pParam = ::sFurParam;
                name.format("%s.fur.txt", pModelData->getMaterialName(i));

                if (getResourceHolder(pActor)->mFileInfoTable->isExistRes(name.c_str())) {
                    loadFurText(pActor, pParam, &dynamicParam, name.c_str());
                }

                pMulti->setLayerDirect(layer, shape, pParam->mNumLayers, pParam, pBody, pLength, pDensity);
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
