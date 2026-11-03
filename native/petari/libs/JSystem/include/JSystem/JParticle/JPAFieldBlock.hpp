#pragma once

#include "JSystem/JGeometry/TVec.hpp"

class NebulaArena;
struct JPAEmitterWorkData;
class JPABaseParticle;
class JPAFieldBlock;

class JPAFieldBase {
public:
    inline JPAFieldBase() {
    }

    void calcAffect(JPAFieldBlock*, JPABaseParticle*);
    f32 calcFadeAffect(JPAFieldBlock*, f32) const;

    virtual ~JPAFieldBase() {
    }
    virtual void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    virtual void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*) = 0;

    JGeometry::TVec3< f32 > mAccel;
};

class JPAFieldSpin : public JPAFieldBase {
public:
    inline JPAFieldSpin() {
    }

    void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);

    JGeometry::TVec3< f32 > field_0x10;
    JGeometry::TVec3< f32 > field_0x1c;
    JGeometry::TVec3< f32 > field_0x28;
};

class JPAFieldRandom : public JPAFieldBase {
public:
    inline JPAFieldRandom() {
    }

    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);
};

class JPAFieldNewton : public JPAFieldBase {
public:
    inline JPAFieldNewton() {
    }

    void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);

    JGeometry::TVec3< f32 > mDir;
    f32 mCutoff;
};

class JPAFieldMagnet : public JPAFieldBase {
public:
    inline JPAFieldMagnet() {
    }

    void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);

    JGeometry::TVec3< f32 > mDir;
};

class JPAFieldGravity : public JPAFieldBase {
public:
    inline JPAFieldGravity() {
    }

    void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);
};

class JPAFieldDrag : public JPAFieldBase {
public:
    inline JPAFieldDrag() {
    }

    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);
};

class JPAFieldConvection : public JPAFieldBase {
public:
    inline JPAFieldConvection() {
    }

    void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);

    JGeometry::TVec3< f32 > field_0x10;
    JGeometry::TVec3< f32 > field_0x1c;
    JGeometry::TVec3< f32 > field_0x28;
};

class JPAFieldVortex : public JPAFieldBase {
public:
    inline JPAFieldVortex() {
    }

    void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);

    JGeometry::TVec3< f32 > field_0x10;
    f32 field_0x1c;
    f32 field_0x20;
};

class JPAFieldAir : public JPAFieldBase {
public:
    inline JPAFieldAir() {
    }
    void prepare(JPAEmitterWorkData*, JPAFieldBlock*);
    void calc(JPAEmitterWorkData*, JPAFieldBlock*, JPABaseParticle*);
};

// unknown name
class JPAFieldBlockData {
public:
    u8 mMagic[4];
    u32 mSize;
    u32 mFlags;
    JGeometry::TVec3< f32 > mPos;
    JGeometry::TVec3< f32 > mDir;
    f32 mMag;
    f32 mMagRndm;
    f32 mVal1;
    f32 mFadeInTime;
    f32 mFadeOutTime;
    f32 mEnTime;
    f32 mDisTime;
    u8 mCycle;
};

class JPAFieldBlock {
public:
    JPAFieldBlock(u8 const*, NebulaArena*);
    void init(NebulaArena*);

    u32 getType() {
        return mpData->mFlags & 0xF;
    }
    u32 getAddType() {
        return (mpData->mFlags >> 8) & 3;
    }
    u32 getSttFlag() {
        return mpData->mFlags >> 16;
    }
    bool checkStatus(u16 flag) {
        return flag & getSttFlag();
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
    u8 getCycle() {
        return mpData->mCycle;
    }
    f32 getFadeInRate() {
        return mFadeInRate;
    }
    f32 getFadeOutRate() {
        return mFadeOutRate;
    }
    JGeometry::TVec3< f32 >& getPos() {
        return mPos;
    }
    JGeometry::TVec3< f32 >& getDir() {
        return mDir;
    }
    f32 getMag() const {
        return mMag;
    }
    void getPosOrig(JGeometry::TVec3< f32 >* pos) {
        pos->set(mpData->mPos);
    }
    void getDirOrig(JGeometry::TVec3< f32 >* dir) {
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
    void prepare(JPAEmitterWorkData* work) {
        mpField->prepare(work, this);
    }
    void calc(JPAEmitterWorkData* work, JPABaseParticle* ptcl) {
        mpField->calc(work, this, ptcl);
    }

private:
    const JPAFieldBlockData* mpData;
    JPAFieldBase* mpField;
    f32 mFadeInRate;
    f32 mFadeOutRate;
    JGeometry::TVec3< f32 > mPos;
    JGeometry::TVec3< f32 > mDir;
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

#ifndef JPA_FIELD_BLOCK_DEFER_INLINE
#include "JSystem/JParticle/JPAFieldBlockInline.hpp"
#endif
