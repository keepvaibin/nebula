#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleShape.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleChunk.hpp"
#include "Game/Particle/ParticleResources.hpp"
#include "Game/Particle/EmitterDynamics.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MotionSignal.hpp"

EmitterCallBack::~EmitterCallBack() {
}

EmitterCore::EmitterCore() : mpPrevEmtr(NULL), mpNextEmtr(NULL), mpChunks(NULL), mpLastChunk(NULL), mRndm(0) {
    mDrawSlot = 0;
    mMotionEpoch = 0;
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
    mPtclPool = ParticleList();
}

ParticleCore* EmitterCore::createParticle() {
    if (mPtclPool.getNum() == 0) {
        addChunk(this);
    }

    ParticleSlot* node = mPtclPool.pop_front();
    mAlivePtclBase.push_front(node);
    mpRes->getDyn()->calc(mpEmtrMgr->pWd);
    node->mParticle.init_p(mpEmtrMgr->pWd);
    return &node->mParticle;
}

ParticleCore* EmitterCore::createChild(ParticleCore* parent) {
    if (mPtclPool.getNum() == 0) {
        addChunk(this);
    }

    ParticleSlot* node = mPtclPool.pop_front();
    mAlivePtclChld.push_front(node);
    node->mParticle.init_c(mpEmtrMgr->pWd, parent);
    return &node->mParticle;
}

void EmitterCore::init(EmitterManager* pManager, ParticleResource* pResource, u32 seed) {
    MotionSignal::emitterInit(this);
    mpEmtrMgr = pManager;
    mpRes = pResource;
    const EmitterDynamicsData* pData = mpRes->getDyn()->mpData;
    mLocalScl.set< f32 >(pData->mEmitterScl.x, pData->mEmitterScl.y, pData->mEmitterScl.z);
    pData = mpRes->getDyn()->mpData;
    mLocalTrs.set< f32 >(pData->mEmitterTrs.x, pData->mEmitterTrs.y, pData->mEmitterTrs.z);
    pData = mpRes->getDyn()->mpData;
    mLocalDir.set< f32 >(pData->mEmitterDir.x, pData->mEmitterDir.y, pData->mEmitterDir.z);
    if (!MR::isNearZero(mLocalDir)) {
        MR::normalize(&mLocalDir);
    }

    const Vec3< s16 >& rRotation = mpRes->getDyn()->mpData->mEmitterRot;
    const s16 x = rRotation.x;
    const s16 z = rRotation.z;
    const s16 y = rRotation.y;
    mLocalRot.set(x, y, z);
    mMaxFrame = mpRes->getDyn()->getMaxFrame();
    mLifeTime = mpRes->getDyn()->getLifetime();
    mVolumeSize = mpRes->getDyn()->getVolumeSize();
    mRate = mpRes->getDyn()->getRate();
    mRateStep = mpRes->getDyn()->getRateStep();
    mVolumeSweep = mpRes->getDyn()->getVolumeSweep();
    mVolumeMinRad = mpRes->getDyn()->getVolumeMinRad();
    mAwayFromCenterSpeed = mpRes->getDyn()->getInitVelOmni();
    mAwayFromAxisSpeed = mpRes->getDyn()->getInitVelAxis();
    mDirSpeed = mpRes->getDyn()->getInitVelDir();
    mSpread = mpRes->getDyn()->getInitVelDirSp();
    mRndmDirSpeed = mpRes->getDyn()->getInitVelRndm();
    mAirResist = mpRes->getDyn()->getAirRes();
    mRndm.set_seed(seed);
    NebulaMtxIdentity(mGlobalRot);
    mGlobalScl.set(1.0f, 1.0f, 1.0f);
    mGlobalTrs.zero();
    mGlobalPScl.set(1.0f, 1.0f);
    mGlobalPrmClr.r = mGlobalPrmClr.g = mGlobalPrmClr.b = mGlobalPrmClr.a = mGlobalEnvClr.r = mGlobalEnvClr.g = mGlobalEnvClr.b = mGlobalEnvClr.a =
        255;
    pResource->getBsp()->getPrmClr(&mPrmClr);
    pResource->getBsp()->getEnvClr(&mEnvClr);
    mpUserWork = 0;
    mScaleOut = 1.0f;
    mEmitCount = 0.0f;
    mStatus = 0x30;
    mDrawTimes = 1;
    mTick = 0;
    mWaitTime = 0;
    mRateStepTimer = 0;
    mTexAnmIdx = 0;
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
