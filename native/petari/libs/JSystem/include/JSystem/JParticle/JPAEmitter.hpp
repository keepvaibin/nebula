#pragma once

#include "JSystem/JGeometry/TVec.hpp"
#include "JSystem/JParticle/JPADynamicsBlock.hpp"
#include "JSystem/JParticle/JPAEmitterCallBack.hpp"
#include "JSystem/JParticle/JPAList.hpp"
#include "JSystem/JParticle/JPAMath.hpp"
#include "JSystem/JParticle/JPARandom.hpp"
#include "JSystem/JParticle/JPAResource.hpp"
#include "JSystem/JSupport/JSUList.hpp"
#include <stdint.h>

class JPAResourceManager;
class JPABaseEmitter;
class JPAEmitterManager;
class JPAParticleCallBack;

struct JPAEmitterWorkData {
    struct JPAVolumeCalcData {
        JGeometry::TVec3< f32 > mVolumePos;
        JGeometry::TVec3< f32 > mVelOmni;
        JGeometry::TVec3< f32 > mVelAxis;
    };

    JPAEmitterWorkData() : mRndm(0) {
    }

    JPABaseEmitter* mpEmtr;
    JPAResource* mpRes;
    JPAResourceManager* mpResMgr;
    JPARandom mRndm;
    JPAVolumeCalcData mVolumeCalcData;
    f32 mVolumeSize;
    f32 mVolumeMinRad;
    f32 mVolumeSweep;
    s32 mEmitCount;
    s32 mVolumeEmitIdx;
    Mtx mDirectionMtx;
    Mtx mRotationMtx;
    Mtx mGlobalRot;
    Mtx mGlobalSR;
    JGeometry::TVec3< f32 > mEmitterPos;
    JGeometry::TVec3< f32 > mGlobalScl;
    JGeometry::TVec3< f32 > mGlobalEmtrDir;
    JGeometry::TVec3< f32 > mPublicScale;
    JGeometry::TVec3< f32 > mGlobalPos;
    JGeometry::TVec2< f32 > mGlobalPtclScl;
    JGeometry::TVec2< f32 > mPivot;
    Mtx mYBBCamMtx;
    Mtx mPosCamMtx;
    Mtx mPrjMtx;
    JPAList< JPABaseParticle >* mpAlivePtcl;
    JPANode< JPABaseParticle >* mpCurNode;
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
    JPAEmtrStts_StopEmit = 0x01,
    JPAEmtrStts_StopCalc = 0x02,
    JPAEmtrStts_StopDraw = 0x04,
    JPAEmtrStts_EnableDeleteEmitter = 0x08,
    JPAEmtrStts_FirstEmit = 0x10,
    JPAEmtrStts_RateStepEmit = 0x20,
    JPAEmtrStts_Immortal = 0x40,
    JPAEmtrStts_Delete = 0x100,
};

class JPABaseEmitter {
public:
    ~JPABaseEmitter();
    JPABaseEmitter();
    void init(JPAEmitterManager*, JPAResource*);
    bool processTillStartFrame();
    bool processTermination();
    void calcEmitterGlobalPosition(JGeometry::TVec3< f32 >*) const;
    void deleteAllParticle();
    JPABaseParticle* createChild(JPABaseParticle*);
    JPABaseParticle* createParticle();
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
    void setEmitterCallBackPtr(JPAEmitterCallBack* ptr) {
        mpEmtrCallBack = ptr;
    }
    void setGlobalRTMatrix(const Mtx m) {
        JPASetRMtxTVecfromMtx(m, mGlobalRot, &mGlobalTrs);
    }
    void setGlobalSRTMatrix(const Mtx m) {
        JPASetRMtxSTVecfromMtx(m, mGlobalRot, &mGlobalScl, &mGlobalTrs);
        mGlobalPScl.set(mGlobalScl.x, mGlobalScl.y);
    }
    void setGlobalTranslation(f32 x, f32 y, f32 z) {
        mGlobalTrs.set(x, y, z);
    }
    void setGlobalTranslation(const JGeometry::TVec3< f32 >& trs) {
        mGlobalTrs.set(trs);
    }
    void getLocalTranslation(JGeometry::TVec3< f32 >& vec) {
        vec.set(mLocalTrs);
    }
    void getLocalTranslation(JGeometry::TVec3< f32 >* vec) const {
        vec->set(mLocalTrs);
    }
    void setGlobalRotation(const JGeometry::TVec3< s16 >& rot) {
        JPAGetXYZRotateMtx(rot.x, rot.y, rot.z, mGlobalRot);
    }
    void getGlobalTranslation(JGeometry::TVec3< f32 >* out) const {
        out->set(mGlobalTrs);
    }
    void setGlobalDynamicsScale(const JGeometry::TVec3< f32 >& i_scale) {
        mGlobalScl.set(i_scale);
    }
    void getGlobalDynamicsScale(JGeometry::TVec3< f32 >* i_scale) const {
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
    void setLocalTranslation(const JGeometry::TVec3< f32 >& i_trans) {
        mLocalTrs.set(i_trans);
    }
    void setLocalRotation(const JGeometry::TVec3< s16 >& i_rot) {
        mLocalRot.set((s32)(i_rot.x * 0.005493248f), (s32)(i_rot.y * 0.005493248f), (s32)(i_rot.z * 0.005493248f));
    }
    void setRateStep(u8 i_step) {
        mRateStep = i_step;
    }

    void setGlobalParticleHeightScale(f32 height) {
        mGlobalPScl.y = height;
    }
    void setGlobalParticleScale(const JGeometry::TVec3< f32 >& scale) {
        mGlobalPScl.set(scale.x, scale.y);
    }
    void setGlobalParticleScale(f32 scaleX, f32 scaleY) {
        mGlobalPScl.set(scaleX, scaleY);
    }
    void getGlobalParticleScale(JGeometry::TVec3< f32 >& scale) const {
        //       in dPa_gen_b_light8PcallBack::draw on GCN (where the call to set would normally be
        //       inlined).
#if PLATFORM_GCN
        scale.x = mGlobalPScl.x;
        scale.y = mGlobalPScl.y;
        scale.z = 1.0f;
#else
        scale.set(mGlobalPScl.x, mGlobalPScl.y, 1.0f);
#endif
    }
    void getGlobalParticleScale(JGeometry::TVec3< f32 >* scale) const {
        //       in dPa_gen_b_light8PcallBack::draw on GCN (where the call to set would normally be
        //       inlined).
#if PLATFORM_GCN
        scale->x = mGlobalPScl.x;
        scale->y = mGlobalPScl.y;
        scale->z = 1.0f;
#else
        scale->set(mGlobalPScl.x, mGlobalPScl.y, 1.0f);
#endif
    }
    void setGlobalScale(const JGeometry::TVec3< f32 >& scale) {
        mGlobalScl.set(scale);
        mGlobalPScl.set(scale.x, scale.y);
    }
    void setDirection(const JGeometry::TVec3< f32 >& direction) {
        mLocalDir.set(direction);
    }

    void setLocalScale(const JGeometry::TVec3< f32 >& scale) {
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
        setStatus(JPAEmtrStts_StopEmit);
    }
    void playCreateParticle() {
        clearStatus(JPAEmtrStts_StopEmit);
    }
    void becomeImmortalEmitter() {
        setStatus(JPAEmtrStts_Immortal);
    }
    void becomeContinuousParticle() {
        mMaxFrame = 0;
    }
    void becomeDeleteEmitter() {
        setStatus(JPAEmtrStts_Delete);
    }
    void becomeInvalidEmitter() {
        stopCreateParticle();
        mMaxFrame = 1;
    }

    void quitImmortalEmitter() {
        clearStatus(JPAEmtrStts_Immortal);
    }
    void stopCalcEmitter() {
        setStatus(JPAEmtrStts_StopCalc);
    }
    void playCalcEmitter() {
        clearStatus(JPAEmtrStts_StopCalc);
    }
    void stopDrawParticle() {
        setStatus(JPAEmtrStts_StopDraw);
    }
    void playDrawParticle() {
        clearStatus(JPAEmtrStts_StopDraw);
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
        return checkStatus(JPAEmtrStts_EnableDeleteEmitter) && getParticleNumber() == 0;
    }
    void setDrawTimes(u8 drawTimes) {
        mDrawTimes = drawTimes;
    }
    void setParticleCallBackPtr(JPAParticleCallBack* cb) {
        mpPtclCallBack = cb;
    }
    JPAParticleCallBack* getParticleCallBackPtr() const {
        return mpPtclCallBack;
    }
    JPAEmitterCallBack* getEmitterCallBackPtr() const {
        return mpEmtrCallBack;
    }
    u32 getAge() const {
        return mTick;
    }

    void setVolumeMiniRadius(f32 param_1) {
        mVolumeMinRad = param_1;
    }

    void setMaxFrame(s32 maxFrame) {
        mMaxFrame = maxFrame;
    }

public:
    JGeometry::TVec3< f32 > mLocalScl;
    JGeometry::TVec3< f32 > mLocalTrs;
    JGeometry::TVec3< f32 > mLocalDir;
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
    JGeometry::TVec3< s16 > mLocalRot;
    s16 mLifeTime;
    u16 mVolumeSize;
    u8 mRateStep;
    JSULink< JPABaseEmitter > mLink;
    Mtx mGlobalRot;
    JGeometry::TVec3< f32 > mGlobalScl;
    JGeometry::TVec3< f32 > mGlobalTrs;
    JGeometry::TVec2< f32 > mGlobalPScl;
    NebulaColor8 mGlobalPrmClr;
    NebulaColor8 mGlobalEnvClr;
    uintptr_t mpUserWork;  // a pointer natively too (EffectSystemUtil's SingleEmitter)
    JPARandom mRndm;
    JPAList< JPABaseParticle > mAlivePtclBase;
    JPAList< JPABaseParticle > mAlivePtclChld;
    JPAList< JPABaseParticle >* mpPtclPool;
    JPAEmitterManager* mpEmtrMgr;
    JPAResource* mpRes;
    JPAEmitterCallBack* mpEmtrCallBack;
    JPAParticleCallBack* mpPtclCallBack;
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
    u16 mMotionEpoch;  // counts the pooled emitter's effects (MotionSignal)
};

enum {
    JPAPtclStts_Invisible = 0x08,
};

#ifndef JPA_EMITTER_DEFER_INLINE
#include "JSystem/JParticle/JPAEmitterInline.hpp"
#endif
