#define JPA_LIST_DEFER_INLINE
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleShape.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleChunk.hpp"
#include "Game/Particle/ParticleResources.hpp"

EmitterCallBack::~EmitterCallBack() {
}

// Out of free slots: one more chunk, its slots in order.
static void addChunk(EmitterCore* pEmitter) {
    ParticleChunk* pChunk = pEmitter->mpEmtrMgr->takeChunk();
    if (pEmitter->mpChunks == NULL) {
        pEmitter->mpChunks = pChunk;
    } else {
        pEmitter->mpLastChunk->mpNext = pChunk;
    }
    pEmitter->mpLastChunk = pChunk;
    for (u32 i = 0; i < ParticleChunk::cSlots; i++) {
        pEmitter->mPtclPool.push_back(&pChunk->mSlots[i]);
    }
}

void EmitterCore::releaseChunks() {
    if (mpChunks != NULL) {
        mpEmtrMgr->giveChunks(mpChunks, mpLastChunk);
    }
    mpChunks = mpLastChunk = NULL;
    mPtclPool = JPAList< ParticleCore >();
}

ParticleCore* EmitterCore::createParticle() {
    if (mPtclPool.getNum() == 0) {
        addChunk(this);
    }

    JPANode< ParticleCore >* node = mPtclPool.pop_front();
    mAlivePtclBase.push_front(node);
    mpRes->getDyn()->calc(mpEmtrMgr->pWd);
    node->mData.init_p(mpEmtrMgr->pWd);
    return &node->mData;
}

ParticleCore* EmitterCore::createChild(ParticleCore* parent) {
    if (mPtclPool.getNum() == 0) {
        addChunk(this);
    }

    JPANode< ParticleCore >* node = mPtclPool.pop_front();
    mAlivePtclChld.push_front(node);
    node->mData.init_c(mpEmtrMgr->pWd, parent);
    return &node->mData;
}

void EmitterCore::deleteAllParticle() {
    while (mAlivePtclBase.getNum())
        mPtclPool.push_front(mAlivePtclBase.pop_back());
    while (mAlivePtclChld.getNum())
        mPtclPool.push_front(mAlivePtclChld.pop_back());
}

bool EmitterCore::processTillStartFrame() {
    EmitterDynamics* dyn = mpRes->getDyn();
    s16 startFrame = dyn->getStartFrame();

    if (mWaitTime >= startFrame)
        return true;

    if (!(mStatus & 2))
        mWaitTime++;

    return false;
}

bool EmitterCore::processTermination() {
    if (checkStatus(0x100)) {
        return true;
    }

    if (mMaxFrame == 0) {
        return false;
    }
    if (mMaxFrame < 0) {
        setStatus(8);
        return getParticleNumber() == 0;
    }
    if (mTick >= mMaxFrame) {
        setStatus(8);
        if (checkStatus(0x40)) {
            return 0;
        }
        return getParticleNumber() == 0;
    }
    return false;
}

void EmitterCore::calcEmitterGlobalPosition(Vec3f* dst) const {
    Mtx mtx;
    NebulaMtxScale(mtx, mGlobalScl.x, mGlobalScl.y, mGlobalScl.z);
    NebulaMtxConcat(mGlobalRot, mtx, mtx);
    mtx[0][3] = mGlobalTrs.x;
    mtx[1][3] = mGlobalTrs.y;
    mtx[2][3] = mGlobalTrs.z;
    NebulaMtxMultVec(mtx, mLocalTrs, *dst);
}

#include "JSystem/JParticle/JPAListInline.hpp"
