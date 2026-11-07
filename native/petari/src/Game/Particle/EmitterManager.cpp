#define JPA_LIST_DEFER_INLINE
#define JPA_EMITTER_DEFER_INLINE
#define JSU_LIST_DEFER_INLINE
#include "JSystem/JParticle/JPANativeColor.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleChunk.hpp"
#include "Game/Particle/ParticleResources.hpp"

void EmitterManager::addEmitters(u32 num) {
    EmitterCore* p_emtr_link = new (mHeap, 0) EmitterCore[num];

    for (u32 i = 0; i < num; i++)
        mFreeEmtrList.prepend(&p_emtr_link[i].mLink);
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
    gidMax = i_gidMax;
    ridMax = i_ridMax;
    mHeap = pHeap;

    addEmitters(i_emtrNum);

    pEmtrUseList = new (pHeap, 0) JSUList< EmitterCore >[gidMax];
    pResMgrAry = new (pHeap, 0) ParticleResources*[ridMax];
    for (int i = 0; i < ridMax; i++) {
        pResMgrAry[i] = NULL;
    }

    pWd = new (pHeap, 0) EmitterWork();
}

EmitterCore* EmitterManager::createSimpleEmitterID(Vec3f const& pos, u16 resID, u8 group_id, u8 res_mgr_id,
                                                         EmitterCallBack* emtrCB, ParticleCallBack* ptclCB) {
    ParticleResource* pRes = pResMgrAry[res_mgr_id]->getResource(resID);

    if (pRes == NULL) {
    } else {
        if (mFreeEmtrList.getNumLinks() == 0) {
            growEmitters();
        }

        JSULink< EmitterCore >* pLink = mFreeEmtrList.getFirst();
        mFreeEmtrList.remove(pLink);
        pEmtrUseList[group_id].append(pLink);
        EmitterCore* emtr = pLink->getObject();
        emtr->init(this, pRes);
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
    JSULink< EmitterCore >* pNext = NULL;
    for (JSULink< EmitterCore >* pLink = pEmtrUseList[group_id].getFirst(); pLink != pEmtrUseList[group_id].getEnd(); pLink = pNext) {
        pNext = pLink->getNext();

        EmitterCore* emtr = pLink->getObject();

        if (emtr->mpRes->calc(pWd, emtr) && !emtr->checkStatus(0x200))
            forceDeleteEmitter(emtr);
    }
}

void EmitterManager::draw(ParticleCamera const* drawInfo, u8 group_id) {
    drawInfo->getCamMtx(pWd->mPosCamMtx);
    drawInfo->getPrjMtx(pWd->mPrjMtx);
    calcYBBCam();
    JPAParticleBatch::camera(pWd->mPosCamMtx);

    for (JSULink< EmitterCore >* pLink = pEmtrUseList[group_id].getFirst(); pLink != pEmtrUseList[group_id].getEnd(); pLink = pLink->getNext()) {
        EmitterCore* emtr = pLink->getObject();
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
    pEmtrUseList[emtr->getGroupID()].remove(&emtr->mLink);
    mFreeEmtrList.prepend(&emtr->mLink);
}

void EmitterManager::entryResourceManager(ParticleResources* resMgr, u8 resMgrID) {
    pResMgrAry[resMgrID] = resMgr;
}

#include "JSystem/JParticle/JPAListInline.hpp"

template JPAList< ParticleCore >::JPAList();
template JPANode< ParticleCore >::JPANode();
template JPANode< ParticleCore >::~JPANode();

#include "JSystem/JParticle/JPAEmitterInline.hpp"
#include "JSystem/JSupport/JSUListInline.hpp"
