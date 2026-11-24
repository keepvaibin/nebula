#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_format.h>
#include <nebula_module.h>
#include <string.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    PlainMtxCalc< Skeleton::calcBasic, Skeleton::initBasic > sBasicCalc;
    PlainMtxCalc< Skeleton::calcSoftimage, Skeleton::initSoftimage > sSoftimageCalc;
    PlainMtxCalc< Skeleton::calcMaya, Skeleton::initMaya > sMayaCalc;

    // FNV-1a over lower-case ASCII.
    u32 hashName(u32 hash, const char* pName, bool stopAtDot) {
        for (const char* c = pName; c != nullptr && *c != '\0' && !(stopAtDot && *c == '.'); ++c) {
            const u8 v = static_cast< u8 >(*c >= 'A' && *c <= 'Z' ? *c + ('a' - 'A') : *c);
            hash = (hash ^ v) * 16777619u;
        }
        return hash;
    }

    s32 findByHash(const u8* pEntries, u32 count, u32 stride, u32 hashAt, const char* pName) {
        // No name (CrystalCageMoving asks for the joint matrix of nullptr) finds nothing; the caller takes its fallback.
        if (pName == nullptr) {
            return -1;
        }

        const u32 hash = nebula_name_hash(pName);
        for (u32 i = 0; i < count; i++) {
            u32 entryHash;
            memcpy(&entryHash, pEntries + i * stride + hashAt, sizeof(entryHash));
            if (entryHash == hash) {
                return static_cast< s32 >(i);
            }
        }
        return -1;
    }
}  // namespace

u32 nebula_name_hash(const char* pName) {
    u32 hash = 0x811C9DC5u;
    // No name hashes as the empty name does.
    for (const char* c = pName; c != nullptr && *c != '\0'; ++c) {
        hash = (hash ^ static_cast< u8 >(*c)) * 0x01000193u;
    }
    return hash;
}

static ModelData* loadFile(void* pFile) {
    const NebulaModelHeader* pHeader = static_cast< const NebulaModelHeader* >(pFile);
    ModelData* pData = new ModelData();
    pData->mFile = pHeader;
    // Its arrays by name, once (FRAME-MODEL 2.8): a model this reader cannot read stops the load, naming why.
    const NebulaModelCheck check =
        nebula_model_view(pFile, pHeader->magic == NEBULA_MODEL_MAGIC ? pHeader->size : 0u, &pData->mView);
    if (check.fault != NEBULA_MODEL_FINE) {
        char section[5];
        nebula_model_section_text(check.what, section);
        switch (check.fault) {
        case NEBULA_MODEL_OTHER_VERSION:
            NEBULA_PANIC("a model file of version %u; this build reads version %u", check.what, NEBULA_MODEL_VERSION);
        case NEBULA_MODEL_MISSING_SECTION:
            NEBULA_PANIC("a model file without its section %s", section);
        case NEBULA_MODEL_UNKNOWN_REQUIRED:
            NEBULA_PANIC("a model file needs section %s, which this build does not read", section);
        case NEBULA_MODEL_BAD_SECTION:
            NEBULA_PANIC("a model file whose section %s does not fit the file", section);
        case NEBULA_MODEL_BAD_RANGE:
            NEBULA_PANIC("a model file with a material part outside its section %s", section);
        default:
            NEBULA_PANIC("a model file the importer did not write");
        }
    }

    switch (pHeader->flags & 15) {
    case NEBULA_SCALE_SOFTIMAGE:
        pData->mBasicMtxCalc = &sSoftimageCalc;
        break;
    case NEBULA_SCALE_MAYA:
        pData->mBasicMtxCalc = &sMayaCalc;
        break;
    default:
        pData->mBasicMtxCalc = &sBasicCalc;
        break;
    }

    if (pData->mView.joint_count != 0) {
        pData->mJoints = new Joint[pData->mView.joint_count];
        for (u32 i = 0; i < pData->mView.joint_count; i++) {
            const NebulaModelJoint& rFile = pData->entry< NebulaModelJoint >(pData->mView.joints_at, pData->mView.joint_stride, i);
            Joint& rJoint = pData->mJoints[i];
            memset(static_cast< void* >(&rJoint), 0, sizeof(rJoint));
            rJoint.mFile = &rFile;
            rJoint.mNumber = i;
            rJoint.mChild = rFile.child != NEBULA_MODEL_NONE ? &pData->mJoints[rFile.child] : nullptr;
            rJoint.mYounger = rFile.younger != NEBULA_MODEL_NONE ? &pData->mJoints[rFile.younger] : nullptr;
            rJoint.mTransformInfo.mScale.x = rFile.scale[0];
            rJoint.mTransformInfo.mScale.y = rFile.scale[1];
            rJoint.mTransformInfo.mScale.z = rFile.scale[2];
            rJoint.mTransformInfo.mRotation.x = rFile.rotation[0];
            rJoint.mTransformInfo.mRotation.y = rFile.rotation[1];
            rJoint.mTransformInfo.mRotation.z = rFile.rotation[2];
            rJoint.mTransformInfo.mTranslate.x = rFile.translation[0];
            rJoint.mTransformInfo.mTranslate.y = rFile.translation[1];
            rJoint.mTransformInfo.mTranslate.z = rFile.translation[2];
        }
    }
    if (pData->mView.mesh_count != 0) {
        pData->mMeshes = new Mesh[pData->mView.mesh_count];
    }
    pData->mTextureOwner = pData;
    if (pData->mView.texture_count != 0) {
        pData->mTextures = new ModelTexture[pData->mView.texture_count];
        for (u32 i = 0; i < pData->mView.texture_count; i++) {
            const NebulaModelTexture& rFile = pData->entry< NebulaModelTexture >(pData->mView.textures_at, pData->mView.texture_stride, i);
            ModelTexture& rTexture = pData->mTextures[i];
            rTexture.mImage = pData->at(rFile.record_at);
            rTexture.mId = RenderTexture::idOf(rTexture.mImage);
            rTexture.mWidth = rFile.width;
            rTexture.mHeight = rFile.height;
            rTexture.mSampler = pData->entry< NebulaModelSampler >(pData->mView.samplers_at, pData->mView.sampler_stride, rFile.sampler);
        }
    }
    if (pData->mView.material_count != 0) {
        pData->mMaterials = new Material[pData->mView.material_count];
        for (u32 i = 0; i < pData->mView.material_count; i++) {
            pData->mMaterials[i].init(pData, i);
        }
    }
    if (pData->mView.mesh_count != 0) {
        for (u32 i = 0; i < pData->mView.mesh_count; i++) {
            const NebulaModelMesh& rFile = pData->entry< NebulaModelMesh >(pData->mView.meshes_at, pData->mView.mesh_stride, i);
            Mesh& rMesh = pData->mMeshes[i];
            rMesh.mFile = &rFile;
            rMesh.mIndex = i;
            rMesh.mFlags = 0;
            rMesh.mMaterial = rFile.material != NEBULA_MODEL_NONE ? &pData->mMaterials[rFile.material] : nullptr;
        }
    }
    // Each joint's materials, chained in the order its entries take them.
    if (pData->mView.joint_count != 0) {
        const u32* pList = reinterpret_cast< const u32* >(pData->at(pData->mView.joint_meshes_at));
        for (u32 i = 0; i < pData->mView.joint_count; i++) {
            const NebulaModelJoint& rFile = pData->entry< NebulaModelJoint >(pData->mView.joints_at, pData->mView.joint_stride, i);
            Material* pLast = nullptr;
            for (u32 k = 0; k < rFile.mesh_count; k++) {
                Material* pMaterial = &pData->mMaterials[pList[rFile.first_mesh + k]];
                if (pLast != nullptr) {
                    pLast->mNext = pMaterial;
                } else {
                    pData->mJoints[i].mMaterials = pMaterial;
                }
                pLast = pMaterial;
            }
        }
    }
    return pData;
}

ModelData::ModelData()
    : mFile(nullptr), mView{}, mJoints(nullptr), mMaterials(nullptr), mMeshes(nullptr), mTextures(nullptr), mTextureOwner(nullptr),
      mBasicMtxCalc(nullptr), mGpuModel(0), mTextureStamp(0), mResourceHash(0) {
}

ModelData::~ModelData() {
    if (mGpuModel != 0) {
        const NebulaHostServices* pHost = nebula_host();
        pHost->model_free(pHost->user, mGpuModel);
    }
}

ModelData* ModelData::load(void* pFile) {
    ModelData* pData = loadFile(pFile);
    const NebulaHostServices* pHost = nebula_host();
    u32 firstMesh = 0;
    pData->mGpuModel = pHost->model_register(pHost->user, pData->mFile, &firstMesh);
    for (u32 i = 0; i < pData->mView.mesh_count; i++) {
        pData->mMeshes[i].mGpuMesh = firstMesh + i;
    }
    return pData;
}

ModelData* ModelData::loadMaterials(void* pFile) {
    return loadFile(pFile);
}

s32 ModelData::findJoint(const char* pName) const {
    return findByHash(at(mView.joints_at), mView.joint_count, mView.joint_stride, offsetof(NebulaModelJoint, name_hash), pName);
}

s32 ModelData::findMaterial(const char* pName) const {
    return findByHash(at(mView.materials_at), mView.material_count, mView.material_stride, offsetof(NebulaModelMatl, name_hash), pName);
}

s32 ModelData::findTexture(const char* pName) const {
    for (u32 i = 0; i < getTextureCount(); i++) {
        if (MR::isEqualString(getTextureName(i), pName)) {
            return static_cast< s32 >(i);
        }
    }
    return -1;
}

void ModelData::setTexture(u32 index, const ResTIMG& rTexture) {
    // A disc picture: its sampler from the console's codes until E3 step 4 cooks them (P-155).
    ModelTexture& rOwn = mTextureOwner->mTextures[index];
    rOwn.mImage = rTexture.image();
    rOwn.mId = 0;
    rOwn.mWidth = rTexture.mWidth;
    rOwn.mHeight = rTexture.mHeight;
    rOwn.mSampler = nebula_sampler_from_codes(rTexture.mWrapS, rTexture.mWrapT, rTexture.mMinType, rTexture.mMagType,
                                              rTexture.mMaxAnisotropy, rTexture.mMinLod, rTexture.mMaxLod, rTexture.mLodBias);
    mTextureOwner->mTextureStamp++;
}

u32 ModelData::getTextureId(u32 index) const {
    const ModelTexture& rTexture = mTextureOwner->mTextures[index];
    return rTexture.mId != 0 ? rTexture.mId : RenderTexture::idOf(rTexture.mImage);
}

void ModelData::show() {
    for (u32 i = 0; i < mView.mesh_count; i++) {
        mMeshes[i].show();
    }
}

void ModelData::hide() {
    for (u32 i = 0; i < mView.mesh_count; i++) {
        mMeshes[i].hide();
    }
}

void ModelData::setResourceName(const char* pName) {
    const char* pBase = pName;
    for (const char* c = pName; *c != '\0'; ++c) {
        if (*c == '/' || *c == '\\') {
            pBase = c + 1;
        }
    }
    mResourceHash = hashName(hashName(2166136261u, pBase, true), "/", false);
    for (u32 i = 0; i < mView.material_count; i++) {
        mMaterials[i].mSourceHash = hashName(mResourceHash, getMaterialName(i), false);
    }
}
