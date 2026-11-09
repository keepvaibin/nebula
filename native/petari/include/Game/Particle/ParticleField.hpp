#pragma once

#include "Game/Particle/ParticleData.hpp"

class NebulaArena;
struct EmitterWork;
class ParticleCore;
class ParticleField;

class FieldBase {
public:
    inline FieldBase() {
    }

    void calcAffect(ParticleField*, ParticleCore*);
    f32 calcFadeAffect(ParticleField*, f32) const;

    virtual ~FieldBase() {
    }
    virtual void prepare(EmitterWork*, ParticleField*) {
    }
    virtual void calc(EmitterWork*, ParticleField*, ParticleCore*) = 0;

    Vec3f mAccel;
};

class FieldSpin : public FieldBase {
public:
    inline FieldSpin() {
    }

    void prepare(EmitterWork*, ParticleField*) override;
    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;

    Mtx mRotation;  // one step about the field axis in world space; translation column 0
};

class FieldRandom : public FieldBase {
public:
    inline FieldRandom() {
    }

    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;
};

class FieldNewton : public FieldBase {
public:
    inline FieldNewton() {
    }

    void prepare(EmitterWork*, ParticleField*) override;
    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;

    Vec3f mDir;
    f32 mCutoff;
};

class FieldMagnet : public FieldBase {
public:
    inline FieldMagnet() {
    }

    void prepare(EmitterWork*, ParticleField*) override;
    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;

    Vec3f mDir;
};

class FieldGravity : public FieldBase {
public:
    inline FieldGravity() {
    }

    void prepare(EmitterWork*, ParticleField*) override;
    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;
};

class FieldDrag : public FieldBase {
public:
    inline FieldDrag() {
    }

    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;
};

class FieldConvection : public FieldBase {
public:
    inline FieldConvection() {
    }

    void prepare(EmitterWork*, ParticleField*) override;
    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;

    // World-space unit frame: toward the ring from the axis, the axis, and around it.
    Vec3f mRadial;
    Vec3f mAxis;
    Vec3f mTangent;
};

class FieldVortex : public FieldBase {
public:
    inline FieldVortex() {
    }

    void prepare(EmitterWork*, ParticleField*) override;
    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;

    Vec3f mAxis;
    f32 mRadiusSq;  // inside it the strength blends from mag at the axis to mag-random at the edge
    f32 mInvRadiusSq;
};

class FieldAir : public FieldBase {
public:
    inline FieldAir() {
    }
    void prepare(EmitterWork*, ParticleField*) override;
    void calc(EmitterWork*, ParticleField*, ParticleCore*) override;
};

class ParticleField {
public:
    ParticleField(const ParticleFieldData* pData, NebulaArena* pHeap);
    void init(NebulaArena*);

    u32 getType() {
        return mpData->mType;
    }
    u32 getAddType() {
        return mpData->mAddType;
    }
    bool checkStatus(u16 flag) {
        return flag & mpData->mStatus;
    }
    f32 getMagRndm() const {
        return mpData->mMagRndm;
    }
    f32 getVal1() const {
        return mpData->mVal1;
    }
    f32 getFadeInTime() {
        return mpData->mFadeInTime;
    }
    f32 getFadeOutTime() {
        return mpData->mFadeOutTime;
    }
    f32 getEnTime() {
        return mpData->mEnTime;
    }
    f32 getDisTime() {
        return mpData->mDisTime;
    }
    u32 getCycle() {
        return mpData->mCycle;
    }
    f32 getFadeInRate() {
        return mpData->mFadeInRate;
    }
    f32 getFadeOutRate() {
        return mpData->mFadeOutRate;
    }
    Vec3f& getPos() {
        return mPos;
    }
    Vec3f& getDir() {
        return mDir;
    }
    f32 getMag() const {
        return mMag;
    }
    void getPosOrig(Vec3f* pos) {
        pos->set(mpData->mPos);
    }
    void getDirOrig(Vec3f* dir) {
        dir->set(mpData->mDir);
    }
    f32 getMagOrig() {
        return mpData->mMag;
    }
    void initOpParam() {
        getPosOrig(&mPos);
        getDirOrig(&mDir);
        mMag = getMagOrig();
    }
    void prepare(EmitterWork* work) {
        mpField->prepare(work, this);
    }
    void calc(EmitterWork* work, ParticleCore* ptcl) {
        mpField->calc(work, this, ptcl);
    }

private:
    const ParticleFieldData* mpData;
    FieldBase* mpField;
    Vec3f mPos;
    Vec3f mDir;
    f32 mMag;

    enum Type {
        FIELD_GRAVITY,
        FIELD_AIR,
        FIELD_MAGNET,
        FIELD_NEWTON,
        FIELD_VORTEX,
        FIELD_RANDOM,
        FIELD_DRAG,
        FIELD_CONVECTION,
        FIELD_SPIN,
    };
};

