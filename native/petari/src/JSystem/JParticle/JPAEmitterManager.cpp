#define JPA_LIST_DEFER_INLINE
#define JPA_EMITTER_DEFER_INLINE
#define JSU_LIST_DEFER_INLINE
#include "JSystem/JParticle/JPAEmitterManager.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/System/NebulaArena.hpp"
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "JSystem/JParticle/JPAParticle.hpp"
#include "JSystem/JParticle/JPAResourceManager.hpp"

void JPAEmitterManager::addEmitters(u32 num) {
    JPABaseEmitter* p_emtr_link = new (mHeap, 0) JPABaseEmitter[num];

    for (u32 i = 0; i < num; i++)
        mFreeEmtrList.prepend(&p_emtr_link[i].mLink);
}

void JPAEmitterManager::addParticles(u32 num) {
    JPANode< JPABaseParticle >* p_ptcl_node = new (mHeap, 0) JPANode< JPABaseParticle >[num];
    for (u32 i = 0; i < num; i++)
        mPtclPool.push_back(&p_ptcl_node[i]);
}

// Out of free ones: as many again, for as long as the scene lasts.
void JPAEmitterManager::growEmitters() {
    addEmitters(emtrNum);
    emtrNum *= 2;
}

void JPAEmitterManager::growParticles() {
    addParticles(ptclNum);
    ptclNum *= 2;
}

JPAEmitterManager::JPAEmitterManager(u32 i_ptclNum, u32 i_emtrNum, NebulaArena* pHeap, u8 i_gidMax, u8 i_ridMax) {
    emtrNum = i_emtrNum;
    ptclNum = i_ptclNum;
    gidMax = i_gidMax;
    ridMax = i_ridMax;
    mHeap = pHeap;

    addEmitters(i_emtrNum);
    addParticles(i_ptclNum);

    pEmtrUseList = new (pHeap, 0) JSUList< JPABaseEmitter >[gidMax];
    pResMgrAry = new (pHeap, 0) JPAResourceManager*[ridMax];
    for (int i = 0; i < ridMax; i++) {
        pResMgrAry[i] = NULL;
    }

    pWd = new (pHeap, 0) JPAEmitterWorkData();
}

JPABaseEmitter* JPAEmitterManager::createSimpleEmitterID(JGeometry::TVec3< f32 > const& pos, u16 resID, u8 group_id, u8 res_mgr_id,
                                                         JPAEmitterCallBack* emtrCB, JPAParticleCallBack* ptclCB) {
    JPAResource* pRes = pResMgrAry[res_mgr_id]->getResource(resID);

    if (pRes == NULL) {
    } else {
        if (mFreeEmtrList.getNumLinks() == 0) {
            growEmitters();
        }

        JSULink< JPABaseEmitter >* pLink = mFreeEmtrList.getFirst();
        mFreeEmtrList.remove(pLink);
        pEmtrUseList[group_id].append(pLink);
        JPABaseEmitter* emtr = pLink->getObject();
        emtr->init(this, pRes);
        emtr->mpPtclPool = &mPtclPool;
        emtr->mpEmtrCallBack = emtrCB;
        emtr->mpPtclCallBack = ptclCB;
        emtr->mGroupID = group_id;
        emtr->mResMgrID = res_mgr_id;
        emtr->mGlobalTrs.set(pos);
        return emtr;
    }

    return NULL;
}

void JPAEmitterManager::calc(u8 group_id) {
    JSULink< JPABaseEmitter >* pNext = NULL;
    for (JSULink< JPABaseEmitter >* pLink = pEmtrUseList[group_id].getFirst(); pLink != pEmtrUseList[group_id].getEnd(); pLink = pNext) {
        pNext = pLink->getNext();

        JPABaseEmitter* emtr = pLink->getObject();

        if (emtr->mpRes->calc(pWd, emtr) && !emtr->checkStatus(0x200))
            forceDeleteEmitter(emtr);
    }
}

void JPAEmitterManager::draw(JPADrawInfo const* drawInfo, u8 group_id) {
    drawInfo->getCamMtx(pWd->mPosCamMtx);
    drawInfo->getPrjMtx(pWd->mPrjMtx);
    calcYBBCam();
    J3DPass::cull(NEBULA_CULL_NONE);

    for (JSULink< JPABaseEmitter >* pLink = pEmtrUseList[group_id].getFirst(); pLink != pEmtrUseList[group_id].getEnd(); pLink = pLink->getNext()) {
        JPABaseEmitter* emtr = pLink->getObject();
        if (!emtr->checkStatus(0x04)) {
            pWd->mpResMgr = pResMgrAry[emtr->mResMgrID];
            emtr->mpRes->draw(pWd, emtr);
        }
    }
}

void JPAEmitterManager::forceDeleteEmitter(JPABaseEmitter* emtr) {
    emtr->deleteAllParticle();
    emtr->setStatus(0x300);
    pEmtrUseList[emtr->getGroupID()].remove(&emtr->mLink);
    mFreeEmtrList.prepend(&emtr->mLink);
}

void JPAEmitterManager::entryResourceManager(JPAResourceManager* resMgr, u8 resMgrID) {
    pResMgrAry[resMgrID] = resMgr;
}

#include "JSystem/JParticle/JPAListInline.hpp"

template JPAList< JPABaseParticle >::JPAList();
template JPANode< JPABaseParticle >::JPANode();
template JPANode< JPABaseParticle >::~JPANode();

#include "JSystem/JParticle/JPAEmitterInline.hpp"
#include "JSystem/JSupport/JSUListInline.hpp"
