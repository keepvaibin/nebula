#include "Game/Util/FurCtrl.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/FurDrawer.hpp"
#include "Game/Util/FurMulti.hpp"
#include "Game/Util/FurShader.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/LumaCore.hpp"

FurMulti* FurBank::check(ModelData* pModelData, u32 layer) {
    for (u32 i = 0; i < mCount; i++) {
        if (mEntries[i]->mModel->getModelData() == pModelData && ((1 << layer) & mLayerMasks[i])) {
            return mEntries[i];
        }
    }

    return nullptr;
}

void FurDrawManager::add(FurCtrl* pCtrl, u8 index) {
    mFurCtrls[index].push_back(pCtrl);
}

void FurDrawManager::draw() const {
    for (s32 i = 0; i < mFurCtrls[1].size(); i++) {
        FurCtrl* const ctrl = mFurCtrls[1][i];
        if (!MR::isClipped(ctrl->mActor) && !MR::isDead(ctrl->mActor) && !MR::isHiddenModel(ctrl->mActor)) {
            if (ctrl->mVisible == 1) {
                ctrl->drawFur();
            }
        }
    }

}

FurCtrl::FurCtrl(LiveActor* pActor, FurParam* pParam, bool addToManager, u8 drawOrder) : mParam(pParam), mVisible(), mFurUVScale(1.0f) {
    if (addToManager) {
        MR::getFurDrawManager()->add(this, drawOrder);
    }

    mActor = pActor;
    mModel = nullptr;
    mShapeIndex = -1;
    mNumLayers = 0;
    mDrawer = nullptr;
    mMesh = nullptr;
    mIsClone = 0;
    mDynamicParam.mFogCtrl = nullptr;
    mDynamicParam.mLightParam = nullptr;
}

void FurCtrl::calcLayerForm() {
    mDrawer->mFurUVScale = mParam->mFurUVScale;
    mDrawer->mBodyUVScale = mParam->mBodyUVScale;
    mDrawer->mLength.mTip = mParam->mLength;
    mDrawer->mLength.mRoot = 0.0f;
    mDrawer->mLength.mCurve = mParam->mLengthCurve;
    mDrawer->mNumLayers = mParam->mNumLayers;

    if (mDrawer->mNumLayers > mNumLayers) {
        mDrawer->mNumLayers = mNumLayers;
    }

    for (s32 i = 0; i < mDrawer->mNumLayers; i++) {
        mDrawer->mShells[i] = mDrawer->mLength.calcValue(i, mDrawer->mNumLayers);
    }
}

void FurCtrl::drawFur() {
    ModelData* pModelData = mModel->getModelData();

    if (MR::getModel(mActor)->getMeshEntry(mShapeIndex)->checkFlag(0x10)) {
        return;
    }

    Mesh* pShape = pModelData->getMeshAt(mShapeIndex);
    if (pShape->isHidden()) {
        return;
    }

    if (mDynamicParam.mLightParam->mLightType == -1) {
        if (MR::getLightCtrl(mActor) != nullptr) {
            MR::loadActorLight(mActor);
        }
    } else {
        MR::loadLight(mDynamicParam.mLightParam->mLightType);
    }

    mDrawer->mFurUVScale = mParam->mFurUVScale * mFurUVScale;
    mDrawer->mBodyUVScale = mParam->mBodyUVScale;
    mDrawer->mColor = mParam->mColor;
    mDrawer->mBrightness.mTip = mParam->mBrightnessTip;
    mDrawer->mBrightness.mRoot = mParam->mBrightnessRoot;
    mDrawer->mBrightness.mCurve = mParam->mBrightnessCurve;
    mDrawer->mAlpha.mTip = mParam->mAlphaTip;
    mDrawer->mAlpha.mRoot = mParam->mAlphaRoot;
    mDrawer->mAlpha.mCurve = mParam->mAlphaCurve;
    mDrawer->mColorBlend.mTip = mParam->mSkinAlphaTip;
    mDrawer->mColorBlend.mRoot = mParam->mSkinAlphaRoot;
    mDrawer->mColorBlend.mCurve = mParam->mSkinAlphaCurve;
    mDrawer->setupMaterial(mDynamicParam);

    // The manager skips the fur of an actor clipped, dead or hidden: each fur draws as its model's object, in a range past
    // the model's own meshes' numbers by its shape (a layer each), so the fur after a skipped one is not renumbered.
    Frame::setObject(mModel->getSlot(), mModel->mMotionEpoch);
    Frame::setPart(0x1000u + static_cast< u32 >(mShapeIndex) * 64u);
    TypedDraw site(mDrawer->material(), nullptr);
    site.setModelBlock(pModelData, mModel->block(pShape->getMaterial()));
    for (s32 i = 0; i < mDrawer->mNumLayers; i++) {
        mDrawer->setupLayerMaterial(i);
        site.setMaterial(mDrawer->material());
        mMesh->drawJointed(site);
    }
}

void FurCtrl::createFurMap() {
    if (mIsClone) {
        return;
    }

    mDrawer->createFurMap(*mParam);
}

namespace MR {
    FurDrawManager* getFurDrawManager() {
        return static_cast< FurDrawManager* >(getSceneObjHolder()->getObj(SceneObj_FurDrawManager));
    }
};  // namespace MR

FurDrawManager::FurDrawManager(u32 capacity) : NameObj("ファー描画マネージャ") {
    lumacore::setClass(this, lumacore::kClassId< FurDrawManager >);
    mFurCtrls[0].init(capacity);
    mFurCtrls[1].init(capacity);
    mBank = new FurBank;
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_Fur);
}

void FurCtrl::setupFur(Model* pModel, ResTIMG* pLength, ResTIMG* pBody, u32 shape, u32 numLayers) {
    ModelData* pData = pModel->getModelData();
    mModel = pModel;
    mShapeIndex = shape;
    mMesh = createFurMesh(pData, shape, pLength);
    mDrawer = new FurDrawer(numLayers, pBody);
    mNumLayers = numLayers;

    mVisible = 1;
}

void FurCtrl::setupFurClone(Model* pModel, FurCtrl* pOther) {
    mModel = pModel;
    mIsClone = 1;
    mShapeIndex = pOther->mShapeIndex;
    mMesh = pOther->mMesh;
    mDrawer = pOther->mDrawer;
    mNumLayers = pOther->mNumLayers;

    mVisible = 1;
}

namespace MR {
    void applyFurFile(FurParam* pParam, FurLightParam* pLight, const FurFile& rFile) {
        const u32 present = rFile.mPresent;
        if (present & (1u << FurFile::cNumLayers)) {
            pParam->mNumLayers = rFile.mNumLayers;
        }

        f32* const floats[13] = {&pParam->mLength,       &pParam->mLengthCurve,   &pParam->mBrightnessTip,  &pParam->mBrightnessRoot,
                                 &pParam->mBrightnessCurve, &pParam->mAlphaTip,    &pParam->mAlphaRoot,      &pParam->mAlphaCurve,
                                 &pParam->mSkinAlphaTip, &pParam->mSkinAlphaRoot, &pParam->mSkinAlphaCurve, &pParam->mFurUVScale,
                                 &pParam->mBodyUVScale};
        for (u32 i = 0; i < 13; i++) {
            if (present & (1u << (FurFile::cFirstFloat + i))) {
                *floats[i] = rFile.mFloats[i];
            }
        }

        if (present & (1u << FurFile::cColor)) {
            pParam->mColor.r = rFile.mColor[0];
            pParam->mColor.g = rFile.mColor[1];
            pParam->mColor.b = rFile.mColor[2];
            pParam->mColor.a = rFile.mColor[3];
        }

        for (u32 i = 0; i < 4; i++) {
            if (present & (1u << FurFile::cDensity)) {
                pParam->mDensity[i] = rFile.mDensity[i];
            }
            if (present & (1u << FurFile::cThickness)) {
                pParam->mThickness[i] = rFile.mThickness[i];
            }
            if (present & (1u << FurFile::cLengthMap)) {
                pParam->mLengthMap[i] = rFile.mLengthMap[i];
            }
        }

        pLight->mLightMask = rFile.mLight[0];
        pLight->mMatIntensity = rFile.mLight[1];
        pLight->mAmbIntensity = rFile.mLight[2];
        pLight->mSpecularLightMask = rFile.mLight[3];
        pLight->mSpecularMatIntensity = rFile.mLight[4];
        pLight->mSpecularAmbIntensity = rFile.mLight[5];
        pLight->mColorSource = rFile.mLight[6];
    }
};  // namespace MR

FurDrawManager::~FurDrawManager() {
}
