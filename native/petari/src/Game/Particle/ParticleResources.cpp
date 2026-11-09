#include "Game/Particle/ParticleResources.hpp"
#include "Game/Particle/ChildShape.hpp"
#include "Game/Particle/EmitterDynamics.hpp"
#include "Game/Particle/ExTexShape.hpp"
#include "Game/Particle/ExtraShape.hpp"
#include "Game/Particle/ParticleField.hpp"
#include "Game/Particle/ParticleKey.hpp"
#include "Game/Particle/ParticleResource.hpp"
#include "Game/Particle/ParticleShape.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>

ParticleResources::ParticleResources(void const* pFile, NebulaArena* pHeap) : mpHeap(pHeap) {
    const ParticleFileHeader* pHeader = static_cast< const ParticleFileHeader* >(pFile);
    if (pHeader->mMagic != cParticleFileMagic || pHeader->mVersion != cParticleFileVersion) {
        NEBULA_PANIC("ParticleResources: not a cooked particle file of this version (the importer cooks it)");
    }
    mResNum = pHeader->mResourceCount;
    mTexNum = pHeader->mTextureCount;
    mpResArr = new (pHeap, 0) ParticleResource*[mResNum];
    mpTexArr = new (pHeap, 0) ParticleTexture*[mTexNum];

    const ParticleResourceData* pResources = particleRecord< ParticleResourceData >(pFile, pHeader->mResources);
    mUserIndexCount = 0;
    for (u32 i = 0; i < mResNum; i++) {
        if (pResources[i].mUserIndex >= mUserIndexCount) {
            mUserIndexCount = pResources[i].mUserIndex + 1;
        }
    }
    mpByUserIndex = new (pHeap, 0) ParticleResource*[mUserIndexCount];
    for (u32 i = 0; i < mUserIndexCount; i++) {
        mpByUserIndex[i] = nullptr;
    }

    for (u32 i = 0; i < mResNum; i++) {
        const ParticleResourceData& data = pResources[i];
        ParticleResource* pRes = new (pHeap, 0) ParticleResource();
        pRes->mUsrIdx = data.mUserIndex;
        pRes->mpDynamicsBlock = new (pHeap, 0) EmitterDynamics(particleRecord< EmitterDynamicsData >(pFile, data.mDynamics));
        pRes->mpBaseShape = new (pHeap, 0) ParticleShape(pFile, particleRecord< ParticleShapeData >(pFile, data.mShape));
        if (data.mExtra != 0) {
            pRes->mpExtraShape = new (pHeap, 0) ExtraShape(particleRecord< ExtraShapeData >(pFile, data.mExtra));
        }
        if (data.mChild != 0) {
            pRes->mpChildShape = new (pHeap, 0) ChildShape(particleRecord< ChildShapeData >(pFile, data.mChild));
        }
        if (data.mExTex != 0) {
            pRes->mpExTexShape = new (pHeap, 0) ExTexShape(particleRecord< ExTexShapeData >(pFile, data.mExTex));
        }
        pRes->mFieldBlockNum = data.mFieldCount;
        if (data.mFieldCount != 0) {
            const ParticleFieldData* pFields = particleRecord< ParticleFieldData >(pFile, data.mFields);
            pRes->mpFieldBlocks = new (pHeap, 0) ParticleField*[data.mFieldCount];
            for (u32 k = 0; k < data.mFieldCount; k++) {
                pRes->mpFieldBlocks[k] = new (pHeap, 0) ParticleField(&pFields[k], pHeap);
            }
        }
        pRes->mKeyBlockNum = data.mKeyCount;
        if (data.mKeyCount != 0) {
            const u32* pKeys = particleRecord< u32 >(pFile, data.mKeys);
            pRes->mpKeyBlocks = new (pHeap, 0) ParticleKey*[data.mKeyCount];
            for (u32 k = 0; k < data.mKeyCount; k++) {
                pRes->mpKeyBlocks[k] = new (pHeap, 0) ParticleKey(particleRecord< ParticleKeyData >(pFile, pKeys[k]));
            }
        }
        pRes->mpTDB1 = particleRecord< u32 >(pFile, data.mTextureIndices);
        pRes->init();
        mpResArr[i] = pRes;
        if (mpByUserIndex[data.mUserIndex] == nullptr) {
            mpByUserIndex[data.mUserIndex] = pRes;
        }
    }

    const ParticleTextureEntry* pTextures = particleRecord< ParticleTextureEntry >(pFile, pHeader->mTextures);
    for (u32 i = 0; i < mTexNum; i++) {
        mpTexArr[i] = new (pHeap, 0) ParticleTexture(particleRecord< char >(pFile, pTextures[i].mName),
                                                     particleRecord< NebulaTextureInfo >(pFile, pTextures[i].mInfo));
    }
}

const ResTIMG* ParticleResources::swapTexture(ResTIMG const* img, char const* swapName) {
    for (u32 i = 0; i < mTexNum; i++) {
        if (MR::isEqualString(swapName, mpTexArr[i]->getName())) {
            GameTexture* tex = mpTexArr[i]->getTexture();
            const ResTIMG* ret = tex->info();
            tex->set(img);
            return ret;
        }
    }
    return nullptr;
}
