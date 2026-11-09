#pragma once

#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class Model;
class Material;
class GameTexture;
class LiveActor;
class ResourceHolder;
struct ResTIMG;
class XanimePlayer;
class XanimeResourceTable;

namespace MR {

    ResTIMG* getTexture(ResourceHolder*, const char*);

    XanimePlayer* newXanimePlayer(const ResourceHolder*, const char*, const ResourceHolder*, ModelFlag, XanimeResourceTable*);
    XanimeResourceTable* newXanimeResourceTable(ResourceHolder*);
    void updateModelManager(LiveActor*);
    void calcAnimModelManager(LiveActor*);
    void updateModelAnimPlayer(LiveActor*);
    void invalidateMtxCalc(ModelData*);
    void invalidateJointCallback(ModelData*);
    Model* getModel(const LiveActor*);
    void calcModel(LiveActor*);
    ModelData* getModelData(const LiveActor*);
    s32 getBckFrameMax(const LiveActor*, const char*);
    s32 getBrkFrameMax(const LiveActor*, const char*);
    s32 getBvaFrameMax(const LiveActor*, const char*);
    bool isBckPlaying(XanimePlayer*, const char*);
    bool findBckNameStringInResource(const char**, const ResourceHolder*, const char*);
    u32 getMaterialNo(ModelData*, const char*);
    u32 getMaterialNo(Model*, const char*);
    Material* getMaterial(ModelData*, const char*);
    Material* getMaterial(ModelData*, int);
    Material* getMaterial(Model*, int);
    Material* getMaterial(const LiveActor*, int);
    s32 getMaterialNum(Model*);
    const char* getMaterialName(const ModelData*, int);
    void updateModelDiffMaterials(LiveActor*);
    void hideMaterial(Model*, const char*);
    void hideMaterial(const LiveActor*, const char*);
    void showMaterial(const Model*, const char*);
    void showMaterial(const LiveActor*, const char*);
    void calcFogStartEnd(Vec3f pos, f32 strength, f32* pFogStart, f32* pFogEnd);
    bool isUseTex(Material*, u32);
    bool isNormalTexMtx(Material*);
    bool isUseTexMtx(Material*, u32);
    bool isExistEffectTexMtx(ModelData*);
    bool isExistEffectTexMtx(LiveActor*);
    ResTIMG* getResTIMG(const LiveActor*, int);
    ResTIMG* getResTIMG(const ModelData*, int);
    const char* getTextureName(const LiveActor*, int);
    void copyJointAnimation(Model*, Model*);
    void copyJointAnimation(LiveActor*, const LiveActor*);
    void syncMaterialAnimation(Model*, Model*);
    void syncMaterialAnimation(LiveActor*, const LiveActor*);
    void getLightNum(Material*, s32*, s32*, s32*, s32*);
    s32 getLightNum(Material*);
    s32 getLightNumMax(const LiveActor*);
    s32 getLightNumMax(ModelData*);
    void calcModelBoundingBox(Box3f*, const LiveActor*);
    void calcModelBoundingRadius(f32*, const LiveActor*);
    bool isExistCollisionResource(const LiveActor*, const char*);
    bool isExistModel(const char*);
    bool isExistSubModel(const char*, const char*);
    Model* createModel(const ResourceHolder*, const char*, ModelFlag);
    ModelData* getModelData(const char*);
};  // namespace MR
