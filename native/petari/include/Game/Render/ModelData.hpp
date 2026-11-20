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

// A model's texture as its draws take it (FRAME-MODEL 2.10): its image (the texture record RenderTexture keys by),
// its host id, its size and its sampler, decoded at the cook. The id of a model's own image is taken at load: the
// image is in the model file's read-only view, never rewritten, and its id is freed with that view's arena, which
// also holds the model. A picture swapped in (setTexture) has id 0 and is looked up at use (RenderTexture::idOf),
// so a rewrite of it is handed over again.
struct ModelTexture {
    const u8* mImage;
    u32 mId;
    u32 mWidth;
    u32 mHeight;
    NebulaModelSampler mSampler;
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
        return static_cast< u32 >(mView.joint_count);
    }
    u32 getMaterialCount() const {
        return static_cast< u32 >(mView.material_count);
    }
    u32 getMeshCount() const {
        return static_cast< u32 >(mView.mesh_count);
    }
    u32 getTextureCount() const {
        return static_cast< u32 >(mTextureOwner->mView.texture_count);
    }
    void useTexturesOf(ModelData* pOwner) {
        mTextureOwner = pOwner->mTextureOwner;
    }
    u32 getFullWeightCount() const {
        return static_cast< u32 >(mView.full_weight_count);
    }
    u8 getScaleMode() const {
        return static_cast< u8 >(mFile->flags & 15);
    }

    Joint* getJointAt(u32 index) const {
        return &mJoints[index];
    }
    Joint* getRootNode() const {
        return mView.joint_count != 0 ? &mJoints[0] : nullptr;
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
        return name(entry< NebulaModelJoint >(mView.joints_at, mView.joint_stride, index).name_at);
    }
    const char* getMaterialName(u32 index) const {
        return name(getMaterialFile(index)->name_at);
    }
    const char* getTextureName(u32 index) const {
        const ModelData* pOwner = mTextureOwner;
        return pOwner->name(pOwner->entry< NebulaModelTexture >(pOwner->mView.textures_at, pOwner->mView.texture_stride, index).name_at);
    }

    // A texture's image, size and sampler, as the game may change them (setTexture).
    const ModelTexture* getTexture(u32 index) const {
        return &mTextureOwner->mTextures[index];
    }
    void setTexture(u32 index, const ResTIMG& rTexture);
    // The host id of a texture: kept from load, or looked up for a picture swapped in.
    u32 getTextureId(u32 index) const;
    u32 getTextureStamp() const {
        return mTextureOwner->mTextureStamp;
    }

    u32 getDrawMtxJoint(u32 index) const {
        return entry< NebulaModelDrawMatrix >(mView.draw_matrices_at, mView.draw_matrix_stride, index).index;
    }
    const f32 (*getInverseBinds() const)[4] {
        return reinterpret_cast< const f32 (*)[4] >(at(mView.inverse_binds_at));
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
    NebulaModelView mView;  // the arrays' places and counts, from the section table at load
    Joint* mJoints;
    Material* mMaterials;
    Mesh* mMeshes;
    ModelTexture* mTextures;
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

    // Entry `index` of a record section, by the stride the file wrote (FRAME-MODEL 2.7.2).
    template < class T >
    const T& entry(u32 offset, u32 stride, u32 index) const {
        return *static_cast< const T* >(nebula_model_entry(mFile, offset, stride, index));
    }

    // A material's parts (FRAME-MODEL 2.9), each by its index in its section; nebula_model_view checked every range.
    const NebulaModelMatl* getMaterialFile(u32 index) const {
        return &entry< NebulaModelMatl >(mView.materials_at, mView.material_stride, index);
    }
    const NebulaModelGx* getGx(u32 index) const {
        return &entry< NebulaModelGx >(mView.gx_at, mView.gx_stride, index);
    }
    const NebulaModelFormEntry& getForm(u32 index) const {
        return entry< NebulaModelFormEntry >(mView.forms_at, mView.form_stride, index);
    }
    const NebulaModelSlot& getSlot(u32 index) const {
        return entry< NebulaModelSlot >(mView.slots_at, mView.slot_stride, index);
    }
    const NebulaModelCoord& getCoord(u32 index) const {
        return entry< NebulaModelCoord >(mView.coords_at, mView.coord_stride, index);
    }
    const NebulaModelCoordMatrix& getCoordMatrix(u32 index) const {
        return entry< NebulaModelCoordMatrix >(mView.coord_matrices_at, mView.coord_matrix_stride, index);
    }
    const NebulaModelWarp& getWarp(u32 index) const {
        return entry< NebulaModelWarp >(mView.warps_at, mView.warp_stride, index);
    }
    // A form's rows (FROW, NEBULA_ROW_*), map numbers (FTEX) and inputs (FINP).
    const u32* getFormRows(u32 first) const {
        return reinterpret_cast< const u32* >(at(mView.rows_at)) + first;
    }
    const u8* getFormMaps(u32 first) const {
        return at(mView.maps_at) + first;
    }
    const u8* getFormInputs(u32 first) const {
        return at(mView.inputs_at) + first;
    }
};

// The first 32 bits of the hash the importer names joints and materials by.
u32 nebula_name_hash(const char* pName);
