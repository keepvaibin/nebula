#pragma once

#include "Game/Util/Array.hpp"
#include "Game/Util/StringUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DStruct.hpp>
#include <JSystem/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DTexMtx;
class ResourceHolder;

class MaterialCtrl {
public:
    MaterialCtrl(J3DModel*, const char*);

    virtual void update();
    virtual void updateMaterial(J3DMaterial*);

    J3DModel* mModel;
    J3DMaterial* mMaterial;
};

class FogCtrl : public MaterialCtrl {
public:
    FogCtrl(J3DModel*, bool);

    virtual void update() override;

    J3DFogInfo mFogInfo;
    s32 mNumMaterials;
    J3DMaterial** mMaterials;
};

class MatColorCtrl : public MaterialCtrl {
public:
    MatColorCtrl(J3DModel*, const char*, u32, const J3DColor*);

    virtual void updateMaterial(J3DMaterial*) override;

    u32 mColorChoice;
    const J3DColor* mColor;
};

class ViewProjmapEffectMtxSetter : public MaterialCtrl {
public:
    ViewProjmapEffectMtxSetter(J3DModel*);

    virtual void update() override;

    J3DTexMtxInfo** mMatricies;
    s32 mNumMatricies;
};

class ProjmapEffectMtxSetter : public MaterialCtrl {
public:
    struct UpdateEffectMtxInfo {
        UpdateEffectMtxInfo() {
        }

        J3DTexMtx* mTexMtx;
        TPos3f mInitEffectMtx;
    };

    ProjmapEffectMtxSetter(J3DModel*, const ResourceHolder*);

    virtual void update() override;
    void getBaseTrans(TVec3f*) const;
    void updateMtxUseBaseMtx();
    void updateMtxUseBaseMtxWithLocalOffset(const TVec3f&);

    UpdateEffectMtxInfo* mUpdatingMtxInfo;
    s32 mNumUpdatingMtx;
    TPos3f mEffectMtx;
};

class MirrorReflectionMtxSetter : public MaterialCtrl {
public:
    MirrorReflectionMtxSetter(J3DModel*, const ResourceHolder*);

    void addUpdatingTexMtxFromName(J3DModel* pModel) {
        J3DModelData* pModelData = pModel->getModelData();
        u16 textureNum = pModelData->getTexture()->getNum();
        for (u16 i = 0; i < textureNum; i++) {
            if (MR::isEqualString(pModelData->getTextureName()->getName(i), sMirrorTextureName)) {
                addUpdatingTexMtxFromTexNo(pModel, i);
            }
        }
    }
    void addUpdatingTexMtxFromTexNo(J3DModel*, u16);
    void addUpdatingTexMtxFromTexCoord(J3DMaterial*);
    void addUpdatingTexMtx(J3DTexMtx*);
    virtual void update() override;

    static char sMirrorTextureName[];

    MR::GrowTable< J3DTexMtx*, 8 > mUpdatingTexMtx;
    s32 mNumUpdatingTexMtx;
};

class MarioShadowProjmapMtxSetter : public MaterialCtrl {
public:
    MarioShadowProjmapMtxSetter(J3DModel*, const ResourceHolder*);

    virtual void update() override;

    ProjmapEffectMtxSetter* mProjmapMtxSetter;
};

class TexMtxCtrl : public MaterialCtrl {
public:
    TexMtxCtrl(J3DModel*, const char*);

    void setTexMtx(u32, J3DTexMtx*);
    virtual void updateMaterial(J3DMaterial*) override;

    J3DTexMtx* mMatricies[8];
};
