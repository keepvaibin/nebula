#pragma once

#include "Game/Util/Color.hpp"

#include "Game/Util/Array.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Material.hpp"
#include <NebulaGpu.h>
#include <nebula_geometry.hpp>
#include <nebula_math.h>

class CoordMatrix;
class ResourceHolder;

class MaterialCtrl {
public:
    MaterialCtrl(Model*, const char*);

    virtual void update();
    virtual void updateMaterial(Material*);

    Model* mModel;
    Material* mMaterial;
};

class FogCtrl : public MaterialCtrl {
public:
    FogCtrl(Model*, bool);

    virtual void update() override;

    FogInfo mFogInfo;
    s32 mNumMaterials;
    Material** mMaterials;
};

class MatColorCtrl : public MaterialCtrl {
public:
    MatColorCtrl(Model*, const char*, u32, const Color8*);

    virtual void updateMaterial(Material*) override;

    u32 mColorChoice;
    const Color8* mColor;
};

class ViewProjmapEffectMtxSetter : public MaterialCtrl {
public:
    ViewProjmapEffectMtxSetter(Model*);

    virtual void update() override;

    CoordMatrixInfo** mMatricies;
    s32 mNumMatricies;
};

class ProjmapEffectMtxSetter : public MaterialCtrl {
public:
    struct UpdateEffectMtxInfo {
        UpdateEffectMtxInfo() {
        }

        CoordMatrix* mTexMtx;
        Pos3f mInitEffectMtx;
    };

    ProjmapEffectMtxSetter(Model*, const ResourceHolder*);

    virtual void update() override;
    void getBaseTrans(Vec3f*) const;
    void updateMtxUseBaseMtx();
    void updateMtxUseBaseMtxWithLocalOffset(const Vec3f&);

    UpdateEffectMtxInfo* mUpdatingMtxInfo;
    s32 mNumUpdatingMtx;
    Pos3f mEffectMtx;
};

class MirrorReflectionMtxSetter : public MaterialCtrl {
public:
    MirrorReflectionMtxSetter(Model*, const ResourceHolder*);

    void addUpdatingTexMtxFromName(Model* pModel) {
        ModelData* pModelData = pModel->getModelData();
        u32 textureNum = pModelData->getTextureCount();
        for (u32 i = 0; i < textureNum; i++) {
            if (MR::isEqualString(pModelData->getTextureName(i), sMirrorTextureName)) {
                addUpdatingTexMtxFromTexNo(pModel, i);
            }
        }
    }
    void addUpdatingTexMtxFromTexNo(Model*, u32);
    void addUpdatingTexMtxFromTexCoord(Material*);
    void addUpdatingTexMtx(CoordMatrix*);
    virtual void update() override;

    static char sMirrorTextureName[];

    MR::GrowTable< CoordMatrix*, 8 > mUpdatingTexMtx;
    s32 mNumUpdatingTexMtx;
};

class MarioShadowProjmapMtxSetter : public MaterialCtrl {
public:
    MarioShadowProjmapMtxSetter(Model*, const ResourceHolder*);

    virtual void update() override;

    ProjmapEffectMtxSetter* mProjmapMtxSetter;
};

class TexMtxCtrl : public MaterialCtrl {
public:
    TexMtxCtrl(Model*, const char*);

    void setTexMtx(u32, CoordMatrix*);
    virtual void updateMaterial(Material*) override;

    MR::GrowTable< CoordMatrix*, 4 > mMatricies;  // by slot, null: the material's own
};
