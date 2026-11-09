#pragma once

#include "Game/Render/Clip.hpp"
#include "Game/Render/Material.hpp"
#include <NebulaGpu.h>
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaArena.hpp"
#include <nebula_math.h>
#include <nebula_model.h>

class Joint;
class MtxCalc;
class Mesh;
struct ResTIMG;

typedef int (*JointCallBack)(Joint*, int);

// A joint of a model file: its rest place, and what the game hooks on it (shared by the file's models).
class Joint {
public:
    u32 getJntNo() const {
        return mNumber;
    }
    u8 getScaleCompensate() const {
        return mFile->scale_compensate;
    }
    u8 getKind() const {
        return mFile->kind & 15;
    }
    u8 getMtxType() const {
        return mFile->billboard;
    }
    f32 getRadius() const {
        return mFile->radius;
    }
    const Vec* getMin() const {
        return reinterpret_cast< const Vec* >(mFile->min);
    }
    const Vec* getMax() const {
        return reinterpret_cast< const Vec* >(mFile->max);
    }
    TransformInfo& getTransformInfo() {
        return mTransformInfo;
    }
    Joint* getChild() const {
        return mChild;
    }
    Joint* getYounger() const {
        return mYounger;
    }
    Material* getMesh() const {
        return mMaterials;
    }
    void setCallBack(JointCallBack callback) {
        mCallBack = callback;
    }
    JointCallBack getCallBack() const {
        return mCallBack;
    }
    void setMtxCalc(MtxCalc* pCalc) {
        mMtxCalc = pCalc;
    }
    MtxCalc* getMtxCalc() const {
        return mMtxCalc;
    }

    const NebulaModelJoint* mFile;
    JointCallBack mCallBack;
    void* mCallBackUserData;
    MtxCalc* mMtxCalc;
    Joint* mChild;
    Joint* mYounger;
    Material* mMaterials;  // the first of its materials in entry order, joined by Material::getNext
    TransformInfo mTransformInfo;
    u32 mNumber;
};

// A mesh of a model file: what draws, in which GPU mesh.
class Mesh {
public:
    enum {
        Flag_Hidden = 1,
    };

    void show() {
        mFlags &= ~Flag_Hidden;
    }
    void hide() {
        mFlags |= Flag_Hidden;
    }
    bool isHidden() const {
        return (mFlags & Flag_Hidden) != 0;
    }
    u32 getIndex() const {
        return mIndex;
    }
    Material* getMaterial() const {
        return mMaterial;
    }
    f32 getRadius() const {
        return mFile->radius;
    }
    const Vec* getMin() const {
        return reinterpret_cast< const Vec* >(mFile->min);
    }
    const Vec* getMax() const {
        return reinterpret_cast< const Vec* >(mFile->max);
    }

    const NebulaModelMesh* mFile;
    Material* mMaterial;
    u32 mGpuMesh;
    u32 mFlags;
    u32 mIndex;
};

// A loaded model file (nebula_model.h), used in place. One set of materials, joints and meshes; each Model draws from it.
// It lives in the arena of the holder that loads it, which is its file's (ResourceHolderManager), so the arena destroys
// it before the file goes; its joints, materials and meshes are that arena's memory.
class ModelData : public ArenaOwned {
public:
    ModelData();
    // The renderer's copy goes (the host holds it until the frames naming it are drawn).
    ~ModelData() override;
    ModelData(const ModelData&) = delete;
    ModelData& operator=(const ModelData&) = delete;

    static ModelData* load(void* pFile);
    // A material table's file (no joints or meshes).
    static ModelData* loadMaterials(void* pFile);

    u32 getJointCount() const {
        return static_cast< u32 >(mFile->joint_count);
    }
    u32 getMaterialCount() const {
        return static_cast< u32 >(mFile->material_count);
    }
    u32 getMeshCount() const {
        return static_cast< u32 >(mFile->mesh_count);
    }
    u32 getTextureCount() const {
        return static_cast< u32 >(mTextureOwner->mFile->texture_count);
    }
    void useTexturesOf(ModelData* pOwner) {
        mTextureOwner = pOwner->mTextureOwner;
    }
    u32 getFullWeightCount() const {
        return static_cast< u32 >(mFile->full_weight_count);
    }
    u8 getScaleMode() const {
        return static_cast< u8 >(mFile->flags & 15);
    }

    Joint* getJointAt(u32 index) const {
        return &mJoints[index];
    }
    Joint* getRootNode() const {
        return mFile->joint_count != 0 ? &mJoints[0] : nullptr;
    }
    Material* getMaterialAt(u32 index) const {
        return &mMaterials[index];
    }
    Mesh* getMeshAt(u32 index) const {
        return &mMeshes[index];
    }

    s32 findJoint(const char* pName) const;
    s32 findMaterial(const char* pName) const;
    s32 findTexture(const char* pName) const;
    const char* getJointName(u32 index) const {
        return name(reinterpret_cast< const NebulaModelJoint* >(at(mFile->joints_at))[index].name_at);
    }
    const char* getMaterialName(u32 index) const {
        return name(reinterpret_cast< const NebulaModelMaterial* >(at(mFile->materials_at))[index].name_at);
    }
    const char* getTextureName(u32 index) const {
        return mTextureOwner->name(reinterpret_cast< const NebulaModelTexture* >(mTextureOwner->at(mTextureOwner->mFile->textures_at))[index].name_at);
    }

    // A texture's record and how it samples, as the game may change them.
    ResTIMG* getTexture(u32 index) const {
        return &mTextureOwner->mTextures[index];
    }
    void setTexture(u32 index, const ResTIMG& rTexture);
    u32 getTextureStamp() const {
        return mTextureOwner->mTextureStamp;
    }

    u32 getDrawMtxJoint(u32 index) const {
        return reinterpret_cast< const NebulaModelDrawMatrix* >(at(mFile->draw_matrices_at))[index].index;
    }
    const f32 (*getInverseBinds() const)[4] {
        return reinterpret_cast< const f32 (*)[4] >(at(mFile->inverse_binds_at));
    }

    // The first vertex of the first mesh: its place, and its normal when it has them.
    const f32* getFirstPosition() const {
        return reinterpret_cast< const f32* >(at(mMeshes[0].mFile->positions_at));
    }
    const f32* getFirstNormal() const {
        return reinterpret_cast< const f32* >(at(mMeshes[0].mFile->attributes_at));
    }

    void show();
    void hide();

    MtxCalc* getBasicMtxCalc() const {
        return mBasicMtxCalc;
    }

    // The renderer's number for it, the model's name's hash for its materials' identities.
    void setResourceName(const char* pName);

    const NebulaModelHeader* mFile;
    Joint* mJoints;
    Material* mMaterials;
    Mesh* mMeshes;
    ResTIMG* mTextures;
    ModelData* mTextureOwner;
    MtxCalc* mBasicMtxCalc;
    u32 mGpuModel;
    u32 mTextureStamp;
    u32 mResourceHash;

    const u8* at(u32 offset) const {
        return reinterpret_cast< const u8* >(mFile) + offset;
    }
    const char* name(u32 offset) const {
        return reinterpret_cast< const char* >(at(offset));
    }
};

// The first 32 bits of the hash the importer names joints and materials by.
u32 nebula_name_hash(const char* pName);
