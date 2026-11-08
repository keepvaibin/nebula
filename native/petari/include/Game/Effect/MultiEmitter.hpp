#pragma once

#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class AutoEffectInfo;
class EffectSystem;
class ParticleCallBack;
class MultiEmitterCallBack;
class MultiEmitterCallBackBase;
class MultiEmitterParticleCallBack;
class ParticleEmitter;
class SingleEmitter;
class SyncBckEffectInfo;
class XanimePlayer;

class MultiEmitter {
public:
    MultiEmitter(const char*, const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale, const Vec3f& rOffset);
    MultiEmitter(const char*, MtxPtr, const Vec3f& rOffset);
    MultiEmitter(const char*, MtxPtr, const Vec3f* pScale, const Vec3f& rOffset);
    MultiEmitter(const char*);

    void createEmitter();
    void createEmitterWithCallBack(MultiEmitterCallBackBase*);

    void deleteEmitter();
    void forceDeleteEmitter();
    void forceDelete(EffectSystem*);
    void deleteForeverEmitter();
    void playCalcAndDeleteForeverEmitter();

    bool isValid() const;
    bool isExistOneTimeEmitter() const;

    void setHostSRT(const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale);
    void setHostMtx(MtxPtr);
    void setOffset(const Vec3f&);
    void setBaseScale(f32);
    void setName(const char*);
    bool isEqualName(u16) const;

    ParticleEmitter* getParticleEmitter(int) const;
    void create(EffectSystem*);
    void scanParticleEmitter(EffectSystem*);

    void forceFollowOn();
    void forceFollowOff();
    void forceScaleOn();

    void initSyncBck(XanimePlayer*, const char*, s32, f32);
    void onDeleteSyncBck(bool, f32);
    void addSyncBck(const XanimePlayer*, const char*);
    void setContinueBckEnd(bool);
    void onCreateSyncClipping();
    void onForceDeleteSyncClipping();
    void stopEmitterOnClipped();
    void playEmitterOffClipped();

    void setDrawOrder(s32);

    void addChildEmitter(MultiEmitter*);

    void setGlobalRotationDegree(const Vec3f&, s32);

    void allocateEmitter(const char*);
    SingleEmitter* getValidEmitter(s32, bool);
    void createOneTimeEmitter();
    void createForeverEmitter();

    // In MultiEmitterAccess
    void setGlobalRotation(const Vec3s&, s32);
    void setGlobalTranslation(const Vec3f&, s32);
    void setGlobalScale(const Vec3f&, s32);
    void setGlobalSRTMatrix(const Mtx, s32);
    void setGlobalPrmColor(u8, u8, u8, s32);
    void setGlobalEnvColor(u8, u8, u8, s32);
    void setLocalScale(const Vec3f&, s32);
    void setDirectionalSpeed(f32, s32);
    void setRate(f32, s32);
    void stopCalcEmitter(s32);
    void playCalcEmitter(s32);
    void stopDrawParticle(s32);
    void playDrawParticle(s32);
    void setParticleCallBackPtr(ParticleCallBack*, s32);
    void pauseOff(s32);

    // In MarioEffect
    void setGlobalScale(f32, s32);

    inline bool isFlagOn(u16 flag) const {
        u16 mask = mFlags & flag;
        return mask == flag;
    }

    inline bool isFlagOff(u16 flag) const {
        u16 mask = mFlags & flag;
        return mask != flag;
    }

    inline void turnFlagOn(u16 flag) {
        mFlags |= flag;
    }

    inline void turnFlagOff(u16 flag) {
        mFlags &= ~flag;
    }

    MR::AssignableArray< SingleEmitter > mEmitters;
    MR::Vector< MR::FixedArray< MultiEmitter*, 4 > > mChildren;
    MultiEmitterCallBack* mCallBack;
    MultiEmitterParticleCallBack* mParticleCallBack;
    SyncBckEffectInfo* mSyncBck;
    const AutoEffectInfo* mAutoInfo;  // the effect's table row; none for effects made by code
    f32 mLightAffect;                 // near 0: the emitters keep their own colours
    u16 mHash;
    u8 mFlags;
};
