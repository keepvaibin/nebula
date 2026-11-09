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

    // FNV-1a over lower-case ASCII: the material table of ray tracing names materials the same way.
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
    if (pHeader->magic != NEBULA_MODEL_MAGIC || pHeader->version != NEBULA_MODEL_VERSION) {
        NEBULA_PANIC("a model file the importer did not write");
    }
    ModelData* pData = new ModelData();
    pData->mFile = pHeader;

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

    if (pHeader->joint_count != 0) {
        pData->mJoints = new Joint[pHeader->joint_count];
        const NebulaModelJoint* pFiles = reinterpret_cast< const NebulaModelJoint* >(pData->at(pHeader->joints_at));
        for (u32 i = 0; i < pHeader->joint_count; i++) {
            const NebulaModelJoint& rFile = pFiles[i];
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
    if (pHeader->mesh_count != 0) {
        pData->mMeshes = new Mesh[pHeader->mesh_count];
    }
    pData->mTextureOwner = pData;
    if (pHeader->texture_count != 0) {
        pData->mTextures = new ResTIMG[pHeader->texture_count];
        const NebulaModelTexture* pFiles = reinterpret_cast< const NebulaModelTexture* >(pData->at(pHeader->textures_at));
        for (u32 i = 0; i < pHeader->texture_count; i++) {
            const NebulaModelTexture& rFile = pFiles[i];
            ResTIMG& rTexture = pData->mTextures[i];
            memset(&rTexture, 0, sizeof(rTexture));
            rTexture.mFormat = rFile.format;
            rTexture.mWidth = rFile.width;
            rTexture.mHeight = rFile.height;
            rTexture.mWrapS = rFile.wrap_s;
            rTexture.mWrapT = rFile.wrap_t;
            rTexture.mMipmap = rFile.mipmap;
            rTexture.mMaxAnisotropy = rFile.anisotropy;
            rTexture.mMinType = rFile.min_filter;
            rTexture.mMagType = rFile.mag_filter;
            rTexture.mMinLod = rFile.min_lod;
            rTexture.mMaxLod = rFile.max_lod;
            rTexture.mImageNum = rFile.levels;
            rTexture.mLodBias = rFile.lod_bias;
            rTexture.mImage = const_cast< u8* >(pData->at(rFile.record_at));
        }
    }
    if (pHeader->material_count != 0) {
        pData->mMaterials = new Material[pHeader->material_count];
        for (u32 i = 0; i < pHeader->material_count; i++) {
            pData->mMaterials[i].init(pData, i);
        }
    }
    if (pHeader->mesh_count != 0) {
        const NebulaModelMesh* pFiles = reinterpret_cast< const NebulaModelMesh* >(pData->at(pHeader->meshes_at));
        for (u32 i = 0; i < pHeader->mesh_count; i++) {
            Mesh& rMesh = pData->mMeshes[i];
            rMesh.mFile = &pFiles[i];
            rMesh.mIndex = i;
            rMesh.mFlags = 0;
            rMesh.mMaterial = pFiles[i].material != NEBULA_MODEL_NONE ? &pData->mMaterials[pFiles[i].material] : nullptr;
        }
    }
    // Each joint's materials, chained in the order its entries take them.
    if (pHeader->joint_count != 0) {
        const NebulaModelJoint* pFiles = reinterpret_cast< const NebulaModelJoint* >(pData->at(pHeader->joints_at));
        const u32* pList = reinterpret_cast< const u32* >(pData->at(pHeader->joint_meshes_at));
        for (u32 i = 0; i < pHeader->joint_count; i++) {
            Material* pLast = nullptr;
            for (u32 k = 0; k < pFiles[i].mesh_count; k++) {
                Material* pMaterial = &pData->mMaterials[pList[pFiles[i].first_mesh + k]];
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
    : mFile(nullptr), mJoints(nullptr), mMaterials(nullptr), mMeshes(nullptr), mTextures(nullptr), mTextureOwner(nullptr),
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
    for (u32 i = 0; i < pData->mFile->mesh_count; i++) {
        pData->mMeshes[i].mGpuMesh = firstMesh + i;
    }
    return pData;
}

ModelData* ModelData::loadMaterials(void* pFile) {
    return loadFile(pFile);
}

s32 ModelData::findJoint(const char* pName) const {
    return findByHash(at(mFile->joints_at), mFile->joint_count, sizeof(NebulaModelJoint), offsetof(NebulaModelJoint, name_hash), pName);
}

s32 ModelData::findMaterial(const char* pName) const {
    return findByHash(at(mFile->materials_at), mFile->material_count, sizeof(NebulaModelMaterial), offsetof(NebulaModelMaterial, name_hash), pName);
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
    ResTIMG& rOwn = mTextureOwner->mTextures[index];
    rOwn = rTexture;
    rOwn.mImage = rTexture.image();
    mTextureOwner->mTextureStamp++;
}

void ModelData::show() {
    for (u32 i = 0; i < mFile->mesh_count; i++) {
        mMeshes[i].show();
    }
}

void ModelData::hide() {
    for (u32 i = 0; i < mFile->mesh_count; i++) {
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
    for (u32 i = 0; i < mFile->material_count; i++) {
        mMaterials[i].mSourceHash = hashName(mResourceHash, getMaterialName(i), false);
    }
}
