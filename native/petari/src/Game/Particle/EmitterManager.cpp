#include "Game/Particle/EmitterManager.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleChunk.hpp"
#include "Game/Particle/ParticleResources.hpp"
#include "Game/Particle/ParticleDraw.hpp"
#include "Game/Util/MathUtil.hpp"

void EmitterManager::addEmitters(u32 num) {
    EmitterCore* p_emtr_link = new (mHeap, 0) EmitterCore[num];

    for (u32 i = 0; i < num; i++) {
        p_emtr_link[i].mpNextEmtr = mpFreeEmtrs;
        mpFreeEmtrs = &p_emtr_link[i];
    }
}

// Out of free ones: as many again, for as long as the scene lasts.
void EmitterManager::growEmitters() {
    addEmitters(emtrNum);
    emtrNum *= 2;
}

ParticleChunk* EmitterManager::takeChunk() {
    ParticleChunk* pChunk = mpFreeChunks;
    if (pChunk != NULL) {
        mpFreeChunks = pChunk->mpNext;
    } else {
        pChunk = new (mHeap, 0) ParticleChunk();
    }
    pChunk->mpNext = NULL;
    return pChunk;
}

void EmitterManager::giveChunks(ParticleChunk* pFirst, ParticleChunk* pLast) {
    pLast->mpNext = mpFreeChunks;
    mpFreeChunks = pFirst;
}

EmitterManager::EmitterManager(u32 i_emtrNum, NebulaArena* pHeap, u8 i_gidMax, u8 i_ridMax) {
    emtrNum = i_emtrNum;
    mpFreeChunks = NULL;
    mpFreeEmtrs = NULL;
    gidMax = i_gidMax;
    ridMax = i_ridMax;
    mHeap = pHeap;

    addEmitters(i_emtrNum);

    pEmtrUseList = new (pHeap, 0) EmitterList[gidMax];
    for (int i = 0; i < gidMax; i++) {
        pEmtrUseList[i].mpFirst = NULL;
        pEmtrUseList[i].mpLast = NULL;
    }
    pResMgrAry = new (pHeap, 0) ParticleResources*[ridMax];
    mpMadeCounts = new (pHeap, 0) u32*[ridMax];
    for (int i = 0; i < ridMax; i++) {
        pResMgrAry[i] = NULL;
        mpMadeCounts[i] = NULL;
    }
    mSceneSeed = 0;

    pWd = new (pHeap, 0) EmitterWork();
}

namespace {
    // splitmix64's output for the given state and step.
    u32 mixSeed(u64 state, u32 step) {
        u64 z = state + (u64(step) + 1u) * 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return u32(z ^ (z >> 31));
    }
}  // namespace

EmitterCore* EmitterManager::createSimpleEmitterID(Vec3f const& pos, u16 resID, u8 group_id, u8 res_mgr_id,
                                                         EmitterCallBack* emtrCB, ParticleCallBack* ptclCB) {
    ParticleResource* pRes = pResMgrAry[res_mgr_id]->getResource(resID);

    if (pRes == NULL) {
    } else {
        if (mpFreeEmtrs == NULL) {
            growEmitters();
        }

        EmitterCore* emtr = mpFreeEmtrs;
        mpFreeEmtrs = emtr->mpNextEmtr;
        EmitterList& list = pEmtrUseList[group_id];
        emtr->mpPrevEmtr = list.mpLast;
        emtr->mpNextEmtr = NULL;
        if (list.mpLast != NULL) {
            list.mpLast->mpNextEmtr = emtr;
        } else {
            list.mpFirst = emtr;
        }
        list.mpLast = emtr;
        static_assert(sizeof(resID) == 2 && sizeof(res_mgr_id) == 1, "the resource fields of the seed state overlap");
        const u64 resource = (u64(mSceneSeed) << 32) | (u64(res_mgr_id) << 16) | resID;
        emtr->init(this, pRes, mixSeed(resource, mpMadeCounts[res_mgr_id][resID]++));
        emtr->mpEmtrCallBack = emtrCB;
        emtr->mpPtclCallBack = ptclCB;
        emtr->mGroupID = group_id;
        emtr->mResMgrID = res_mgr_id;
        emtr->mGlobalTrs.set(pos);
        return emtr;
    }

    return NULL;
}

void EmitterManager::calc(u8 group_id) {
    EmitterCore* pNext = NULL;
    for (EmitterCore* emtr = pEmtrUseList[group_id].mpFirst; emtr != NULL; emtr = pNext) {
        pNext = emtr->mpNextEmtr;

        if (emtr->mpRes->calc(pWd, emtr) && !emtr->checkStatus(0x200))
            forceDeleteEmitter(emtr);
    }
}

void EmitterManager::calcYBBCam() {
    Vec3f axis(0.0f, pWd->mPosCamMtx[1][1], pWd->mPosCamMtx[2][1]);
    if (!MR::isNearZero(axis)) {
        MR::normalize(&axis);
        pWd->mYBBCamMtx[0][0] = 1.0f;
        pWd->mYBBCamMtx[0][1] = 0.0f;
        pWd->mYBBCamMtx[0][2] = 0.0f;
        pWd->mYBBCamMtx[0][3] = pWd->mPosCamMtx[0][3];
        pWd->mYBBCamMtx[1][0] = 0.0f;
        pWd->mYBBCamMtx[1][1] = axis.y;
        pWd->mYBBCamMtx[1][2] = -axis.z;
        pWd->mYBBCamMtx[1][3] = pWd->mPosCamMtx[1][3];
        pWd->mYBBCamMtx[2][0] = 0.0f;
        pWd->mYBBCamMtx[2][1] = axis.z;
        pWd->mYBBCamMtx[2][2] = axis.y;
        pWd->mYBBCamMtx[2][3] = pWd->mPosCamMtx[2][3];
    }
}

void EmitterManager::draw(ParticleCamera const* drawInfo, u8 group_id) {
    drawInfo->getCamMtx(pWd->mPosCamMtx);
    drawInfo->getPrjMtx(pWd->mPrjMtx);
    calcYBBCam();
    ParticleDraw::camera(pWd->mPosCamMtx);

    for (EmitterCore* emtr = pEmtrUseList[group_id].mpFirst; emtr != NULL; emtr = emtr->mpNextEmtr) {
        if (!emtr->checkStatus(0x04)) {
            pWd->mpResMgr = pResMgrAry[emtr->mResMgrID];
            emtr->mpRes->draw(pWd, emtr);
        }
    }
}

void EmitterManager::forceDeleteEmitter(EmitterCore* emtr) {
    emtr->deleteAllParticle();
    emtr->releaseChunks();
    emtr->setStatus(0x300);
    EmitterList& list = pEmtrUseList[emtr->getGroupID()];
    if (emtr->mpPrevEmtr != NULL) {
        emtr->mpPrevEmtr->mpNextEmtr = emtr->mpNextEmtr;
    } else {
        list.mpFirst = emtr->mpNextEmtr;
    }
    if (emtr->mpNextEmtr != NULL) {
        emtr->mpNextEmtr->mpPrevEmtr = emtr->mpPrevEmtr;
    } else {
        list.mpLast = emtr->mpPrevEmtr;
    }
    emtr->mpPrevEmtr = NULL;
    emtr->mpNextEmtr = mpFreeEmtrs;
    mpFreeEmtrs = emtr;
}

void EmitterManager::entryResourceManager(ParticleResources* resMgr, u8 resMgrID) {
    pResMgrAry[resMgrID] = resMgr;
    mpMadeCounts[resMgrID] = new (mHeap, 0) u32[resMgr->mUserIndexCount];
    for (u32 i = 0; i < resMgr->mUserIndexCount; i++) {
        mpMadeCounts[resMgrID][i] = 0;
    }
}

void EmitterManager::startScene(u32 sceneSeed) {
    mSceneSeed = sceneSeed;
    for (int id = 0; id < ridMax; id++) {
        if (pResMgrAry[id] != NULL) {
            for (u32 i = 0; i < pResMgrAry[id]->mUserIndexCount; i++) {
                mpMadeCounts[id][i] = 0;
            }
        }
    }
}
