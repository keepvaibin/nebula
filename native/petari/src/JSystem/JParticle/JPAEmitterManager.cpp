#define JPA_LIST_DEFER_INLINE
#define JPA_EMITTER_DEFER_INLINE
#define JSU_LIST_DEFER_INLINE
#include "JSystem/JParticle/JPAEmitterManager.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "JSystem/JParticle/JPAParticle.hpp"
#include "JSystem/JParticle/JPAResourceManager.hpp"

JPAEmitterManager::JPAEmitterManager(u32 i_ptclNum, u32 i_emtrNum, JKRHeap* pHeap, u8 i_gidMax, u8 i_ridMax) {
    emtrNum = i_emtrNum;
    ptclNum = i_ptclNum;
    gidMax = i_gidMax;
    ridMax = i_ridMax;

    JPABaseEmitter* p_emtr_link = new (pHeap, 0) JPABaseEmitter[emtrNum];

    for (u32 i = 0; i < emtrNum; i++)
        mFreeEmtrList.prepend(&p_emtr_link[i].mLink);

    JPANode< JPABaseParticle >* p_ptcl_node = new (pHeap, 0) JPANode< JPABaseParticle >[ptclNum];
    for (u32 i = 0; i < ptclNum; i++)
        mPtclPool.push_back(&p_ptcl_node[i]);

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
    } else if (mFreeEmtrList.getNumLinks() == 0) {
    } else {
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
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::enableTexOffsets(GX_TEXCOORD0, GX_TRUE, GX_TRUE);
    J3DNativeState::enableTexOffsets(GX_TEXCOORD1, GX_TRUE, GX_TRUE);
    J3DNativeState::enableTexOffsets(GX_TEXCOORD2, GX_TRUE, GX_TRUE);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setCoPlanar(GX_FALSE);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumChans(0);

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
