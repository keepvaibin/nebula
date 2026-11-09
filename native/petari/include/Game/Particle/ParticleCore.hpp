#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>

class NebulaArena;
class EmitterCore;
class ParticleCore;
class EmitterCallBack;
class EmitterManager;
class ParticleCallBack;
class ParticleResources;
struct EmitterWork;

class ParticleCore {
public:
    // Its frame number plus one is no frame.
    static const u32 cNeverDrawn = 0xFFFFFFFEu;

    ~ParticleCore() {
    }

    void init_p(EmitterWork*);
    void init_c(EmitterWork*, ParticleCore*);
    bool calc_p(EmitterWork*);
    bool calc_c(EmitterWork*);
    bool canCreateChild(EmitterWork*);
    f32 getWidth(EmitterCore const*) const;
    f32 getHeight(EmitterCore const*) const;
    int getAge() const {
        return mAge;
    }

    u16 getRotateAngle() const {
        return mRotateAngle;
    }

    void getGlobalPosition(Vec3f& pos) const {
        pos.set(mPosition);
    }

    void getGlobalPosition(Vec3f* pos) const {
        pos->set(mPosition);
    }

    void setStatus(u32 flag) {
        mStatus |= flag;
    }

    u32 checkStatus(u32 flag) {
        return mStatus & flag;
    }

    void initStatus(u32 status) {
        mStatus = status;
    }

    void getVelVec(Vec3f& vec) const {
        vec.set(mVelocity);
    }

    void getLocalPosition(Vec3f& vec) const {
        vec.set(mLocalPosition);
    }

    void getLocalPosition(Vec3f* vec) const {
        vec->set(mLocalPosition);
    }

    u32 getUserWork() const {
        return mUserWork;
    }

    void setUserWork(u32 userWork) {
        mUserWork = userWork;
    }

    f32 getCalcCurrentPositionX(const EmitterCore*) const;
    f32 getCalcCurrentPositionY(const EmitterCore*) const;
    f32 getCalcCurrentPositionZ(const EmitterCore*) const;

public:
    Vec3f mPosition;
    Vec3f mLocalPosition;
    Vec3f mOffsetPosition;
    Vec3f mVelocity;
    Vec3f mVelType1;
    Vec3f mVelType0;
    Vec3f mVelType2;
    Vec3f mBaseAxis;
    f32 mParticleScaleX;
    f32 mParticleScaleY;
    f32 mScaleOut;
    f32 mAlphaWaveRandom;
    f32 mMoment;
    f32 mDrag;
    u32 mUserWork;
    u32 mStatus;
    s16 mAge;
    s16 mLifeTime;
    f32 mTime;
    s16 mRotateAngle;
    s16 mRotateSpeed;
    NebulaColor8 mPrmClr;
    NebulaColor8 mEnvClr;
    u8 mTexAnmIdx;
    u8 mAnmRandom;
    u8 mPrmColorAlphaAnm;
    // Where it was drawn (ParticleDraw): its matrix, or up to four corners; this game frame and the one before.
    f32 mDrawnNow[12];
    f32 mDrawnBefore[12];
    u32 mDrawnFrame;
    bool mDrawnContinues;
};

class ParticleCallBack {
public:
    virtual ~ParticleCallBack() = 0;
    virtual void execute(EmitterCore*, ParticleCore*) {
    }

    virtual void draw(EmitterCore*, ParticleCore*) {
    }
};

static inline u32 COLOR_MULTI(u32 a, u32 b) {
    return ((a * (b + 1)) * 0x10000) >> 24;
}
