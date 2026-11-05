#pragma once

#include "Game/Util/Array.hpp"

class FogCtrl;
class J3DAnmBase;
struct J3DColor;
class J3DModel;
class J3DModelData;
class MarioShadowProjmapMtxSetter;
class MatColorCtrl;
class MaterialCtrl;
class MirrorReflectionMtxSetter;
class ProjmapEffectMtxSetter;
class ResourceHolder;
class TexMtxCtrl;
class ViewProjmapEffectMtxSetter;

class MaterialUpdater {
public:
    MaterialUpdater(J3DModel*, const ResourceHolder*);

    void update();
    // The copies the model's materials ask for by what they are (a matrix that follows the model or the view).
    void prepareMaterials();
    FogCtrl* addFogCtrl(bool);
    MatColorCtrl* addMatColorCtrl(const char*, u32, const J3DColor*);
    TexMtxCtrl* addTexMtxCtrl(const char*);
    ProjmapEffectMtxSetter* addProjmapEffectMtxSetter();
    MirrorReflectionMtxSetter* addMirrorReflectionMtxSetter();
    void push(MaterialCtrl*);
    void checkMaterial();
    void checkViewProjmapEffectMtx();
    ViewProjmapEffectMtxSetter* addViewProjmapEffectMtxSetter();
    MarioShadowProjmapMtxSetter* addMarioShadowProjmapMtxSetter();
    void checkTexture();
    static bool hasMovingMaterial(const J3DModelData*);

    MR::Vector< MR::AssignableArray< MaterialCtrl* > > mMaterialCtrl;
    J3DModel* mModel;
    FogCtrl* mFogCtrl;
    const ResourceHolder* mResHolder;
};
