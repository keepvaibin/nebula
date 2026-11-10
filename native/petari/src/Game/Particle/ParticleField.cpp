#include "Game/Particle/ParticleField.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 10.0f);

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

void FieldGravity::calc(EmitterWork* /*work*/, ParticleField* block, ParticleCore* ptcl) {
    calcAffect(block, ptcl);
}

void FieldAir::prepare(EmitterWork* pWork, ParticleField* pBlock) {
    Vec3f direction(pBlock->getDir());
    MR::normalizeOrZero(&direction);
    if (pBlock->checkStatus(2)) {
        mAccel.scale(pBlock->getMag(), direction);
    } else {
        NebulaMtxMultVecSR(pWork->mRotationMtx, &direction, &mAccel);
        mAccel.scale(pBlock->getMag());
    }
}

void FieldVortex::prepare(EmitterWork* pWork, ParticleField* pBlock) {
    NebulaMtxMultVecSR(pWork->mGlobalRot, &pBlock->getDir(), &mAxis);
    MR::normalizeOrZero(&mAxis);
    mRadiusSq = pBlock->getPos().z * pBlock->getPos().z;
    mInvRadiusSq = 1.0f / mRadiusSq;
}

void FieldVortex::calc(EmitterWork*, ParticleField* pBlock, ParticleCore* pParticle) {
    Vec3f radial;
    radial.scale(mAxis.dot(pParticle->mLocalPosition), mAxis);
    radial.sub(pParticle->mLocalPosition, radial);
    f32 distanceSquared = radial.squared();
    f32 magnitude;
    if (distanceSquared > mRadiusSq) {
        magnitude = pBlock->getMagRndm();
    } else {
        f32 rate = distanceSquared * mInvRadiusSq;
        magnitude = (1.0f - rate) * pBlock->getMag() + rate * pBlock->getMagRndm();
    }

    MR::normalizeOrZero(&radial);
    mAccel.cross(radial, mAxis);
    mAccel.scale(magnitude);
    calcAffect(pBlock, pParticle);
}

void FieldConvection::prepare(EmitterWork* pWork, ParticleField* pBlock) {
    Vec3f radial;
    Vec3f tangent;
    tangent.cross(pBlock->getPos(), pBlock->getDir());
    radial.cross(pBlock->getDir(), tangent);
    NebulaMtxMultVecSR(pWork->mGlobalRot, &radial, &mRadial);
    NebulaMtxMultVecSR(pWork->mGlobalRot, &pBlock->getDir(), &mAxis);
    NebulaMtxMultVecSR(pWork->mGlobalRot, &tangent, &mTangent);
    MR::normalizeOrZero(&mRadial);
    MR::normalizeOrZero(&mAxis);
    MR::normalizeOrZero(&mTangent);
}

void FieldSpin::prepare(EmitterWork* pWork, ParticleField* pBlock) {
    Vec3f axis;
    NebulaMtxMultVecSR(pWork->mGlobalRot, &pBlock->getDir(), &axis);
    MR::normalizeOrZero(&axis);
    NebulaMtxRotAxisRad(mRotation, &axis, pBlock->getMag());
    mRotation[0][3] = mRotation[1][3] = mRotation[2][3] = 0.0f;
}

void FieldAir::calc(EmitterWork* /*work*/, ParticleField* block, ParticleCore* ptcl) {
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

void FieldMagnet::calc(EmitterWork* /*work*/, ParticleField* block, ParticleCore* ptcl) {
    mAccel.sub(mDir, ptcl->mLocalPosition);
    mAccel.setLength(block->getMag());
    calcAffect(block, ptcl);
}

void FieldNewton::prepare(EmitterWork* work, ParticleField* block) {
    mDir.sub(block->getPos(), work->mEmitterPos);
    NebulaMtxMultVecSR(work->mRotationMtx, &mDir, &mDir);
    mCutoff = block->getVal1() * block->getVal1();
}

void FieldNewton::calc(EmitterWork* /*work*/, ParticleField* block, ParticleCore* ptcl) {
    mAccel.sub(mDir, ptcl->mLocalPosition);
    f32 len_sq = mAccel.squared();
    if (len_sq > mCutoff) {
        mAccel.setLength(mCutoff * (block->getMag() * cNum_calc_f1) / len_sq);
    } else {
        mAccel.setLength(block->getMag() * cNum_calc_f2);
    }
    calcAffect(block, ptcl);
}

void FieldConvection::calc(EmitterWork* /*work*/, ParticleField* block, ParticleCore* ptcl) {
    Vec3f vec1, vec2, vec3;
    vec1.scale(mRadial.dot(ptcl->mLocalPosition), mRadial);
    vec3.scale(mTangent.dot(ptcl->mLocalPosition), mTangent);
    vec1.add(vec3);
    vec1.setLength(vec1, block->getVal1());
    vec2.sub(ptcl->mLocalPosition, vec1);
    vec3.cross(mAxis, vec1);
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

void FieldDrag::calc(EmitterWork* /*work*/, ParticleField* block, ParticleCore* ptcl) {
    if (!ptcl->checkStatus(4)) {
        f32 fade = calcFadeAffect(block, ptcl->mTime);
        ptcl->mDrag *= 1.0f - fade * (1.0f - block->getMag());
    } else {
        ptcl->mDrag *= block->getMag();
    }
}

void FieldSpin::calc(EmitterWork* /*work*/, ParticleField* block, ParticleCore* ptcl) {
    Vec vec;
    NebulaMtxMultVecSR(mRotation, &ptcl->mLocalPosition, &vec);
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
