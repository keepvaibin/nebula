#define JGEOMETRY_VEC3_ZERO_NOINLINE
#include "Game/Particle/ParticleField.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"

void FieldBase::calcAffect(ParticleField* block, ParticleCore* ptcl) {
    Vec3f vec = mAccel;
    if (!ptcl->checkStatus(4) && block->checkStatus(0x78)) {
        vec.scale(calcFadeAffect(block, ptcl->mTime));
    }

    switch (block->getAddType()) {
    case 0:
        ptcl->mVelType0.add(vec);
        break;
    case 1:
        ptcl->mVelType1.add(vec);
        break;
    case 2:
        ptcl->mVelType2.add(vec);
        break;
    }
}

f32 FieldBase::calcFadeAffect(ParticleField* block, f32 time) const {
    f32 fade = 1.0f;
    if ((block->checkStatus(8) && time < block->getEnTime()) || (block->checkStatus(0x10) && time >= block->getDisTime())) {
        fade = 0.0f;
    } else if (block->checkStatus(0x40) && time >= block->getFadeOutTime()) {
        fade = (block->getDisTime() - time) * block->getFadeOutRate();
    } else if (block->checkStatus(0x20) && time < block->getFadeInTime()) {
        fade = (time - block->getEnTime()) * block->getFadeInRate();
    }
    return fade;
}

void FieldGravity::prepare(EmitterWork* work, ParticleField* block) {
    if (block->checkStatus(2)) {
        mAccel.scale(block->getMag(), block->getDir());
    } else {
        NebulaMtxMultVecSR(work->mRotationMtx, &block->getDir(), &mAccel);
        mAccel.scale(block->getMag());
    }
}

void FieldGravity::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    calcAffect(block, ptcl);
}

void FieldAir::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    calcAffect(block, ptcl);
    if (block->checkStatus(4)) {
        f32 len = ptcl->mVelType1.length();
        if (len > block->getMagRndm()) {
            ptcl->mVelType1.scale(block->getMagRndm() / len);
        }
    }
}

void FieldMagnet::prepare(EmitterWork* work, ParticleField* block) {
    mDir.sub(block->getPos(), work->mEmitterPos);
    NebulaMtxMultVecSR(work->mRotationMtx, &mDir, &mDir);
}

void FieldMagnet::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    mAccel.sub(mDir, ptcl->mLocalPosition);
    mAccel.setLength(block->getMag());
    calcAffect(block, ptcl);
}

void FieldNewton::prepare(EmitterWork* work, ParticleField* block) {
    mDir.sub(block->getPos(), work->mEmitterPos);
    NebulaMtxMultVecSR(work->mRotationMtx, &mDir, &mDir);
    mCutoff = block->getVal1() * block->getVal1();
}

void FieldNewton::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    mAccel.sub(mDir, ptcl->mLocalPosition);
    f32 len_sq = mAccel.squared();
    if (len_sq > mCutoff) {
        mAccel.setLength(mCutoff * (block->getMag() * 10.0f) / len_sq);
    } else {
        mAccel.setLength(block->getMag() * 10.0f);
    }
    calcAffect(block, ptcl);
}

void FieldConvection::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    Vec3f vec1, vec2, vec3;
    vec1.scale(field_0x10.dot(ptcl->mLocalPosition), field_0x10);
    vec3.scale(field_0x28.dot(ptcl->mLocalPosition), field_0x28);
    vec1.add(vec3);
    vec1.setLength(vec1, block->getVal1());
    vec2.sub(ptcl->mLocalPosition, vec1);
    vec3.cross(field_0x1c, vec1);
    mAccel.cross(vec3, vec2);
    mAccel.setLength(block->getMag());
    calcAffect(block, ptcl);
}

void FieldRandom::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    if (ptcl->mAge == 0 || (block->getCycle() != 0 && ptcl->mAge % block->getCycle() == 0)) {
        EmitterCore* emtr = work->mpEmtr;
        mAccel.set< f32 >(emtr->get_r_zh(), emtr->get_r_zh(), emtr->get_r_zh());
        mAccel.scale(block->getMag());
        calcAffect(block, ptcl);
    }
}

void FieldDrag::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    if (!ptcl->checkStatus(4)) {
        f32 fade = calcFadeAffect(block, ptcl->mTime);
        ptcl->mDrag *= 1.0f - fade * (1.0f - block->getMag());
    } else {
        ptcl->mDrag *= block->getMag();
    }
}

void FieldSpin::calc(EmitterWork* work, ParticleField* block, ParticleCore* ptcl) {
    Mtx mtx;
    mtx[0][0] = field_0x10.x;
    mtx[1][0] = field_0x10.y;
    mtx[2][0] = field_0x10.z;
    mtx[0][1] = field_0x1c.x;
    mtx[1][1] = field_0x1c.y;
    mtx[2][1] = field_0x1c.z;
    mtx[0][2] = field_0x28.x;
    mtx[1][2] = field_0x28.y;
    mtx[2][2] = field_0x28.z;
    mtx[0][3] = mtx[1][3] = mtx[2][3] = 0.0f;
    Vec vec;
    NebulaMtxMultVecSR(mtx, &ptcl->mLocalPosition, &vec);
    mAccel.set< f32 >(vec.x - ptcl->mLocalPosition.x, vec.y - ptcl->mLocalPosition.y, vec.z - ptcl->mLocalPosition.z);
    calcAffect(block, ptcl);
}

ParticleField::ParticleField(const ParticleFieldData* pData, NebulaArena* heap) : mpData(pData) {
    init(heap);
}

void ParticleField::init(NebulaArena* heap) {
    getPosOrig(&mPos);
    getDirOrig(&mDir);
    mMag = getMagOrig();

    switch (getType()) {
    case FIELD_GRAVITY:
        mpField = new (heap, 0) FieldGravity();
        break;
    case FIELD_AIR:
        mpField = new (heap, 0) FieldAir();
        break;
    case FIELD_MAGNET:
        mpField = new (heap, 0) FieldMagnet();
        break;
    case FIELD_NEWTON:
        mpField = new (heap, 0) FieldNewton();
        break;
    case FIELD_VORTEX:
        mpField = new (heap, 0) FieldVortex();
        break;
    case FIELD_RANDOM:
        mpField = new (heap, 0) FieldRandom();
        break;
    case FIELD_DRAG:
        mpField = new (heap, 0) FieldDrag();
        break;
    case FIELD_CONVECTION:
        mpField = new (heap, 0) FieldConvection();
        break;
    case FIELD_SPIN:
        mpField = new (heap, 0) FieldSpin();
        break;
    default:
        mpField = NULL;
        break;
    }
}
