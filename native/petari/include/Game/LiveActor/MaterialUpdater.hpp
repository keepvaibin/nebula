#pragma once

#include "Game/Util/Color.hpp"

#include "Game/Util/Array.hpp"

class FogCtrl;
class Clip;
class Model;
class ModelData;
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
    MaterialUpdater(Model*, const ResourceHolder*);

    void update();
    // The copies the model's materials ask for by what they are (a matrix that follows the model or the view).
    void prepareMaterials();
    FogCtrl* addFogCtrl(bool);
    MatColorCtrl* addMatColorCtrl(const char*, u32, const Color8*);
    TexMtxCtrl* addTexMtxCtrl(const char*);
    ProjmapEffectMtxSetter* addProjmapEffectMtxSetter();
    MirrorReflectionMtxSetter* addMirrorReflectionMtxSetter();
    void push(MaterialCtrl*);
    void checkMaterial();
    void checkViewProjmapEffectMtx();
    ViewProjmapEffectMtxSetter* addViewProjmapEffectMtxSetter();
    MarioShadowProjmapMtxSetter* addMarioShadowProjmapMtxSetter();
    void checkTexture();
    static bool hasMovingMaterial(const ModelData*);

    MR::Vector< MR::AssignableArray< MaterialCtrl* > > mMaterialCtrl;
    Model* mModel;
    FogCtrl* mFogCtrl;
    const ResourceHolder* mResHolder;
};
