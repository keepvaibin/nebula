#pragma once

#include <nebula_geometry.hpp>
#include "Game/Particle/EmitterDynamics.hpp"
#include "Game/Particle/EmitterCallBack.hpp"
#include "Game/Particle/ParticleList.hpp"
#include "Game/Particle/ParticleMath.hpp"
#include "Game/Particle/ParticleRandom.hpp"
#include "Game/Particle/ParticleResource.hpp"
#include <stdint.h>

class ParticleResources;
class EmitterCore;
struct ParticleChunk;
class EmitterManager;
class ParticleCallBack;

struct EmitterWork {
    struct VolumeCalcData {
        Vec3f mVolumePos;
        Vec3f mVelOmni;
        Vec3f mVelAxis;
    };

    EmitterCore* mpEmtr;
    ParticleResource* mpRes;
    ParticleResources* mpResMgr;
    VolumeCalcData mVolumeCalcData;
    f32 mVolumeSize;
    f32 mVolumeMinRad;
    f32 mVolumeSweep;
    s32 mEmitCount;
    s32 mVolumeEmitIdx;
    Mtx mDirectionMtx;
    Mtx mRotationMtx;
    Mtx mGlobalRot;
    Mtx mGlobalSR;
    Vec3f mEmitterPos;
    Vec3f mGlobalScl;
    Vec3f mGlobalEmtrDir;
    Vec3f mPublicScale;
    Vec3f mGlobalPos;
    Vec2f mGlobalPtclScl;
    Vec2f mPivot;
    Mtx mYBBCamMtx;
    Mtx mPosCamMtx;
    Mtx mPrjMtx;
    ParticleList* mpAlivePtcl;
    ParticleSlot* mpCurNode;
    s32 mVolumeAngleNum;
    s32 mVolumeAngleMax;
    s32 mVolumeX;
    s32 mDivNumber;
    f32 mScaleAnm;
    u32 mDirType;
    u32 mRotType;
    u32 mPlaneType;
    u32 mListType;
    u32 mPrjType;
    s16 mClrKeyFrame;
    u8 mDrawCount;
};

enum {
    EmitterStatus_StopEmit = 0x01,
    EmitterStatus_StopCalc = 0x02,
    EmitterStatus_StopDraw = 0x04,
    EmitterStatus_EnableDeleteEmitter = 0x08,
    EmitterStatus_FirstEmit = 0x10,
    EmitterStatus_RateStepEmit = 0x20,
    EmitterStatus_Immortal = 0x40,
    EmitterStatus_Delete = 0x100,
};

class EmitterCore {
public:
    EmitterCore();
    void init(EmitterManager*, ParticleResource*, u32 seed);
    // Every slot free again (no live particles): the chunks back to the manager.
    void releaseChunks();
    bool processTillStartFrame();
    bool processTermination();
    void calcEmitterGlobalPosition(Vec3f*) const;
    void deleteAllParticle();
    ParticleCore* createChild(ParticleCore*);
    ParticleCore* createParticle();
    u32 getCurrentCreateNumber() const;
    u8 getDrawCount() const;
    bool loadTexture(u8, u8);

    void initStatus(u32 status) {
        mStatus = status;
    }
    void setStatus(u32 status) {
        mStatus |= status;
    }
    void clearStatus(u32 status) {
        mStatus &= ~status;
    }
    u32 checkStatus(u32 status) const {
        return (mStatus & status);
    }
    /* SMG1 change -- returns u32 instead of bool */
    u32 checkFlag(u32 flag) const {
        return (mpRes->getDyn()->getFlag() & flag);
    }
    u8 getResourceManagerID() const {
        return mResMgrID;
    }
    u8 getGroupID() const {
        return mGroupID;
    }
    u8 getDrawTimes() const {
        return mDrawTimes;
    }
    f32 getRate() const {
        return mRate;
    }
    void setRate(f32 rate) {
        mRate = rate;
    }
    void setDirectionalSpeed(f32 i_speed) {
        mDirSpeed = i_speed;
    }
    void setRandomDirectionSpeed(f32 i_speed) {
        mRndmDirSpeed = i_speed;
    }
    void setEmitterCallBackPtr(EmitterCallBack* ptr) {
        mpEmtrCallBack = ptr;
    }
    void setGlobalRTMatrix(const Mtx m) {
        ParticleMath::splitRT(m, mGlobalRot, &mGlobalTrs);
    }
    void setGlobalSRTMatrix(const Mtx m) {
        ParticleMath::splitSRT(m, mGlobalRot, &mGlobalScl, &mGlobalTrs);
        mGlobalPScl.set(mGlobalScl.x, mGlobalScl.y);
    }
    void setGlobalTranslation(f32 x, f32 y, f32 z) {
        mGlobalTrs.set(x, y, z);
    }
    void setGlobalTranslation(const Vec3f& trs) {
        mGlobalTrs.set(trs);
    }
    void getLocalTranslation(Vec3f& vec) {
        vec.set(mLocalTrs);
    }
    void getLocalTranslation(Vec3f* vec) const {
        vec->set(mLocalTrs);
    }
    void setGlobalRotation(const Vec3< s16 >& rot) {
        ParticleMath::xyzRotateMtx(rot.x, rot.y, rot.z, mGlobalRot);
    }
    void getGlobalTranslation(Vec3f* out) const {
        out->set(mGlobalTrs);
    }
    void setGlobalDynamicsScale(const Vec3f& i_scale) {
        mGlobalScl.set(i_scale);
    }
    void getGlobalDynamicsScale(Vec3f* i_scale) const {
        i_scale->set(mGlobalScl);
    }
    void setGlobalAlpha(u8 alpha) {
        mGlobalPrmClr.a = alpha;
    }
    u8 getGlobalAlpha() const {
        return mGlobalPrmClr.a;
    }
    void getGlobalPrmColor(NebulaColor8& color) {
        color = mGlobalPrmClr;
    }
    void getGlobalPrmColor(NebulaColor8* color) const {
        *color = mGlobalPrmClr;
    }
    void setGlobalPrmColor(u8 r, u8 g, u8 b) {
        mGlobalPrmClr.r = r;
        mGlobalPrmClr.g = g;
        mGlobalPrmClr.b = b;
    }
    void setGlobalEnvColor(u8 r, u8 g, u8 b) {
        mGlobalEnvClr.r = r;
        mGlobalEnvClr.g = g;
        mGlobalEnvClr.b = b;
    }
    void setVolumeSize(u16 size) {
        mVolumeSize = size;
    }
    void setLifeTime(s16 lifetime) {
        mLifeTime = lifetime;
    }
    void setAwayFromCenterSpeed(f32 i_speed) {
        mAwayFromCenterSpeed = i_speed;
    }
    void setAwayFromAxisSpeed(f32 i_speed) {
        mAwayFromAxisSpeed = i_speed;
    }
    void setSpread(f32 i_spread) {
        mSpread = i_spread;
    }
    void setLocalTranslation(const Vec3f& i_trans) {
        mLocalTrs.set(i_trans);
    }
    void setLocalRotation(const Vec3< s16 >& i_rot) {
        mLocalRot.set((s32)(i_rot.x * 0.005493248f), (s32)(i_rot.y * 0.005493248f), (s32)(i_rot.z * 0.005493248f));
    }
    void setRateStep(u8 i_step) {
        mRateStep = i_step;
    }

    void setGlobalParticleHeightScale(f32 height) {
        mGlobalPScl.y = height;
    }
    void setGlobalParticleScale(const Vec3f& scale) {
        mGlobalPScl.set(scale.x, scale.y);
    }
    void setGlobalParticleScale(f32 scaleX, f32 scaleY) {
        mGlobalPScl.set(scaleX, scaleY);
    }
    void getGlobalParticleScale(Vec3f& scale) const {
        scale.set(mGlobalPScl.x, mGlobalPScl.y, 1.0f);
    }
    void getGlobalParticleScale(Vec3f* scale) const {
        scale->set(mGlobalPScl.x, mGlobalPScl.y, 1.0f);
    }
    void setGlobalScale(const Vec3f& scale) {
        mGlobalScl.set(scale);
        mGlobalPScl.set(scale.x, scale.y);
    }
    void setDirection(const Vec3f& direction) {
        mLocalDir.set(direction);
    }

    void setLocalScale(const Vec3f& scale) {
        mLocalScl.set(scale);
    }

    f32 get_r_f() {
        return mRndm.get_rndm_f();
    }
    f32 get_r_zp() {
        return mRndm.get_rndm_zp();
    }
    f32 get_r_zh() {
        return mRndm.get_rndm_zh();
    }
    s16 get_r_ss() {
        return mRndm.get_rndm_ss();
    }

    void stopCreateParticle() {
        setStatus(EmitterStatus_StopEmit);
    }
    void playCreateParticle() {
        clearStatus(EmitterStatus_StopEmit);
    }
    void becomeImmortalEmitter() {
        setStatus(EmitterStatus_Immortal);
    }
    void becomeContinuousParticle() {
        mMaxFrame = 0;
    }
    void becomeDeleteEmitter() {
        setStatus(EmitterStatus_Delete);
    }
    void becomeInvalidEmitter() {
        stopCreateParticle();
        mMaxFrame = 1;
    }

    void quitImmortalEmitter() {
        clearStatus(EmitterStatus_Immortal);
    }
    void stopCalcEmitter() {
        setStatus(EmitterStatus_StopCalc);
    }
    void playCalcEmitter() {
        clearStatus(EmitterStatus_StopCalc);
    }
    void stopDrawParticle() {
        setStatus(EmitterStatus_StopDraw);
    }
    void playDrawParticle() {
        clearStatus(EmitterStatus_StopDraw);
    }

    uintptr_t getUserWork() const {
        return mpUserWork;
    }
    void setUserWork(uintptr_t userWork) {
        mpUserWork = userWork;
    }
    u32 getParticleNumber() const {
        return mAlivePtclBase.getNum() + mAlivePtclChld.getNum();
    }
    bool isEnableDeleteEmitter() const {
        return checkStatus(EmitterStatus_EnableDeleteEmitter) && getParticleNumber() == 0;
    }
    void setDrawTimes(u8 drawTimes) {
        mDrawTimes = drawTimes;
    }
    void setParticleCallBackPtr(ParticleCallBack* cb) {
        mpPtclCallBack = cb;
    }
    ParticleCallBack* getParticleCallBackPtr() const {
        return mpPtclCallBack;
    }
    EmitterCallBack* getEmitterCallBackPtr() const {
        return mpEmtrCallBack;
    }
    u32 getAge() const {
        return mTick;
    }

    void setVolumeMiniRadius(f32 radius) {
        mVolumeMinRad = radius;
    }

    void setMaxFrame(s32 maxFrame) {
        mMaxFrame = maxFrame;
    }

public:
    Vec3f mLocalScl;
    Vec3f mLocalTrs;
    Vec3f mLocalDir;
    s32 mMaxFrame;
    f32 mRate;
    f32 mVolumeSweep;
    f32 mVolumeMinRad;
    f32 mAwayFromCenterSpeed;
    f32 mAwayFromAxisSpeed;
    f32 mDirSpeed;
    f32 mSpread;
    f32 mRndmDirSpeed;
    f32 mAirResist;
    Vec3< s16 > mLocalRot;
    s16 mLifeTime;
    u16 mVolumeSize;
    u8 mRateStep;
    // Its neighbours in its group's list (EmitterManager); the next free one while it is free.
    EmitterCore* mpPrevEmtr;
    EmitterCore* mpNextEmtr;
    Mtx mGlobalRot;
    Vec3f mGlobalScl;
    Vec3f mGlobalTrs;
    Vec2f mGlobalPScl;
    NebulaColor8 mGlobalPrmClr;
    NebulaColor8 mGlobalEnvClr;
    uintptr_t mpUserWork;  // a pointer natively too (EffectSystemUtil's SingleEmitter)
    ParticleRandom mRndm;
    ParticleList mAlivePtclBase;
    ParticleList mAlivePtclChld;
    // This emitter's free slots, and the chunks they and its live particles are in (from the manager; back to it when
    // the emitter goes).
    ParticleList mPtclPool;
    ParticleChunk* mpChunks;
    ParticleChunk* mpLastChunk;
    EmitterManager* mpEmtrMgr;
    ParticleResource* mpRes;
    EmitterCallBack* mpEmtrCallBack;
    ParticleCallBack* mpPtclCallBack;
    u32 mStatus;
    f32 mEmitCount;
    f32 mScaleOut;
    u32 mTick;
    s16 mWaitTime;
    s16 mRateStepTimer;
    NebulaColor8 mPrmClr;
    NebulaColor8 mEnvClr;
    u8 mDrawTimes;
    u8 mTexAnmIdx;
    u8 mGroupID;
    u8 mResMgrID;
    u32 mDrawSlot;  // names its draws from frame to frame
    u32 mMotionEpoch;  // counts the pooled emitter's effects (MotionSignal)
};

enum {
    ParticleStatus_Invisible = 0x08,
};
