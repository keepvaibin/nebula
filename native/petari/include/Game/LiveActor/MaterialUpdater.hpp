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
    void diff();
    void newDifferedSnapshot();
    bool isValidDiff();
    void onPrgFlag(u16, u32);
    void onCurFlag(u16, u32);
    u32 getDiffFlag(s32) const;
    FogCtrl* addFogCtrl(bool);
    MatColorCtrl* addMatColorCtrl(const char*, u32, const J3DColor*);
    TexMtxCtrl* addTexMtxCtrl(const char*);
    ProjmapEffectMtxSetter* addProjmapEffectMtxSetter();
    MirrorReflectionMtxSetter* addMirrorReflectionMtxSetter();
    void onCurFlagBpk(const J3DAnmBase*);
    void offCurFlagBpk(const J3DAnmBase*);
    void onCurFlagBtp(const J3DAnmBase*);
    void offCurFlagBtp(const J3DAnmBase*);
    void onCurFlagBtk(const J3DAnmBase*);
    void offCurFlagBtk(const J3DAnmBase*);
    void onCurFlagBrk(const J3DAnmBase*);
    void offCurFlagBrk(const J3DAnmBase*);
    void push(MaterialCtrl*);
    void onBothFlag(u16, u32);
    void onBothFlagAll(u32);
    void checkMaterial();
    void checkViewProjmapEffectMtx();
    ViewProjmapEffectMtxSetter* addViewProjmapEffectMtxSetter();
    MarioShadowProjmapMtxSetter* addMarioShadowProjmapMtxSetter();
    void checkTexture();
    static bool isExistDiffMaterial(const J3DModelData*);

    MR::Vector< MR::AssignableArray< MaterialCtrl* > > mMaterialCtrl;
    u32* mPrgFlag;
    u32* mCurFlag;
    J3DModel* mModel;
    FogCtrl* mFogCtrl;
    const ResourceHolder* mResHolder;
};
