#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/ParticleShape.hpp"
#include "Game/Particle/ChildShape.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include "Game/Particle/ExtraShape.hpp"

ParticleCallBack::~ParticleCallBack() {
}

void ParticleCore::init_p(EmitterWork* pWork) {
    mDrawnFrame = cNeverDrawn;
    EmitterCore* emtr = pWork->mpEmtr;
    ExtraShape* esp = pWork->mpRes->mpExtraShape;
    ParticleShape* bsp = pWork->mpRes->mpBaseShape;
    EmitterDynamics* dyn = pWork->mpRes->mpDynamicsBlock;

    mAge = -1;
    mLifeTime = (s32)((1.0f - dyn->getLifetimeRndm() * emtr->get_r_f()) * emtr->mLifeTime);
    mTime = 0.0f;

    initStatus(0);
    NebulaMtxMultVecSR(pWork->mGlobalSR, &pWork->mVolumeCalcData.mVolumePos, &mLocalPosition);

    if (emtr->checkFlag(8)) {
        setStatus(0x20);
    }

    mOffsetPosition.set(pWork->mGlobalPos);
    mPosition.set< f32 >(mOffsetPosition.x + mLocalPosition.x * pWork->mPublicScale.x, mOffsetPosition.y + mLocalPosition.y * pWork->mPublicScale.y,
                         mOffsetPosition.z + mLocalPosition.z * pWork->mPublicScale.z);

    Vec3f velOmni;

    if (emtr->mAwayFromCenterSpeed) {
        velOmni.setLength(pWork->mVolumeCalcData.mVelOmni, emtr->mAwayFromCenterSpeed);
    } else {
        velOmni.zero();
    }

    Vec3f velAxis;

    if (emtr->mAwayFromAxisSpeed) {
        velAxis.setLength(pWork->mVolumeCalcData.mVelAxis, emtr->mAwayFromAxisSpeed);
    } else {
        velAxis.zero();
    }

    Vec3f velDir;

    if (emtr->mDirSpeed) {
        Mtx mtx;
        // The second angle drawn first: the console's right-to-left order of the arguments, on every platform.
        const s16 angleZ = emtr->mRndm.get_rndm_ss();
        ParticleMath::yzRotateMtx((s32)(emtr->get_r_zp() * 0x8000 * emtr->mSpread), angleZ, mtx);
        NebulaMtxConcat(pWork->mDirectionMtx, mtx, mtx);
        velDir.set< f32 >(emtr->mDirSpeed * mtx[0][2], emtr->mDirSpeed * mtx[1][2], emtr->mDirSpeed * mtx[2][2]);
    } else {
        velDir.zero();
    }

    Vec3f velRndm;

    if (emtr->mRndmDirSpeed) {
        f32 z = emtr->mRndm.get_rndm_zh();
        f32 y = emtr->mRndm.get_rndm_zh();
        f32 x = emtr->mRndm.get_rndm_zh();
        velRndm.set< f32 >(emtr->mRndmDirSpeed * x, emtr->mRndmDirSpeed * y, emtr->mRndmDirSpeed * z);
    } else {
        velRndm.zero();
    }

    f32 ratio = emtr->get_r_zp() * dyn->getInitVelRatio() + 1.0f;
    mVelType1.set< f32 >(ratio * (velOmni.x + velAxis.x + velDir.x + velRndm.x), ratio * (velOmni.y + velAxis.y + velDir.y + velRndm.y),
                         ratio * (velOmni.z + velAxis.z + velDir.z + velRndm.z));

    if (emtr->checkFlag(4)) {
        mVelType1.mul(emtr->mLocalScl);
    }

    NebulaMtxMultVecSR(pWork->mGlobalRot, &mVelType1, &mVelType1);

    mVelType0.zero();

    mMoment = 1.0f - dyn->getMomentRndm() * emtr->get_r_f();
    mDrag = 1.0f;
    mUserWork = 0;

    mBaseAxis.set< f32 >(pWork->mGlobalRot[0][1], pWork->mGlobalRot[1][1], pWork->mGlobalRot[2][1]);

    mPrmClr = emtr->mPrmClr;
    mEnvClr = emtr->mEnvClr;

    mAnmRandom = (s32)(emtr->get_r_f() * bsp->getLoopOfstValue());

    if (esp != nullptr && esp->isEnableScaleAnm()) {
        mParticleScaleX = mParticleScaleY = mScaleOut = emtr->mScaleOut * (emtr->get_r_zp() * esp->getScaleRndm() + 1.0f);
    } else {
        mParticleScaleX = mParticleScaleY = mScaleOut = emtr->mScaleOut;
    }

    mPrmColorAlphaAnm = 0xff;

    if (esp != nullptr && esp->isEnableAlphaFlick()) {
        mAlphaWaveRandom = emtr->get_r_zp() * esp->getAlphaFreqRndm() + 1.0f;
    } else {
        mAlphaWaveRandom = 1.0f;
    }

    if (esp != nullptr) {
        if (esp->isEnableRotateAnm()) {
            mRotateAngle = (s32)(esp->getRotateInitAngle() + esp->getRotateRndmAngle() * emtr->mRndm.get_rndm_zh());
            mRotateSpeed = (s32)(esp->getRotateInitSpeed() * (esp->getRotateRndmSpeed() * emtr->get_r_zp() + 1.0f));
            mRotateSpeed = emtr->get_r_zp() < esp->getRotateDirection() ? mRotateSpeed : (s16)-mRotateSpeed;
        } else {
            mRotateAngle = 0;
            mRotateSpeed = 0;
        }
    } else {
        mRotateAngle = 0;
        mRotateSpeed = 0;
    }
}

void ParticleCore::init_c(EmitterWork* pWork, ParticleCore* pParent) {
    mDrawnFrame = cNeverDrawn;
    EmitterCore* emtr = pWork->mpEmtr;
    ChildShape* csp = pWork->mpRes->getCsp();

    mAge = -1;
    mLifeTime = csp->getLife();
    mTime = 0.0f;
    initStatus(4);
    mLocalPosition.set(pParent->mLocalPosition);

    f32 pos_rndm = csp->getPosRndm();

    if (pos_rndm != 0.0f) {
        // Drawn z, y, x: the console's right-to-left order of the arguments, on every platform.
        const f32 rndZ = emtr->mRndm.get_rndm_zh();
        const f32 rndY = emtr->mRndm.get_rndm_zh();
        const f32 rndX = emtr->mRndm.get_rndm_zh();
        Vec3f rnd(rndX, rndY, rndZ);
        rnd.setLength(pos_rndm * emtr->get_r_f());
        mLocalPosition.add(rnd);
    }

    if (emtr->checkFlag(0x10)) {
        setStatus(0x20);
    }

    mOffsetPosition.set(pParent->mOffsetPosition);

    f32 base_speed = csp->getBaseVel() * (csp->getBaseVelRndm() * emtr->get_r_zp() + 1.0f);
    // Drawn z, y, x: the console's right-to-left order of the arguments, on every platform.
    const f32 velZ = emtr->get_r_zp();
    const f32 velY = emtr->get_r_zp();
    const f32 velX = emtr->get_r_zp();
    Vec3f base_vel(velX, velY, velZ);
    base_vel.setLength(base_speed);
    mVelType1.scaleAdd(csp->getVelInhRate(), pParent->mVelType1, base_vel);
    mVelType0.scale(csp->getVelInhRate(), pParent->mVelType2);

    mMoment = pParent->mMoment;

    if (csp->isFieldAffected()) {
        mDrag = pParent->mDrag;
    } else {
        setStatus(0x40);
        mDrag = 1.0f;
    }

    mUserWork = pParent->mUserWork;

    mVelType2.set(mVelType0);
    f32 ratio = mMoment * mDrag;
    mVelocity.set< f32 >(ratio * (mVelType1.x + mVelType2.x), ratio * (mVelType1.y + mVelType2.y), ratio * (mVelType1.z + mVelType2.z));

    mBaseAxis.set(pParent->mBaseAxis);

    if (csp->isScaleInherited()) {
        mParticleScaleX = mScaleOut = pParent->mParticleScaleX * csp->getScaleInhRate();
        mParticleScaleY = mAlphaWaveRandom = pParent->mParticleScaleY * csp->getScaleInhRate();
    } else {
        mParticleScaleX = mScaleOut = mParticleScaleY = mAlphaWaveRandom = 1.0f;
    }

    if (csp->isColorInherited()) {
        mPrmClr.r = (s32)(pParent->mPrmClr.r * csp->getColorInhRate());
        mPrmClr.g = (s32)(pParent->mPrmClr.g * csp->getColorInhRate());
        mPrmClr.b = (s32)(pParent->mPrmClr.b * csp->getColorInhRate());
        mEnvClr.r = (s32)(pParent->mEnvClr.r * csp->getColorInhRate());
        mEnvClr.g = (s32)(pParent->mEnvClr.g * csp->getColorInhRate());
        mEnvClr.b = (s32)(pParent->mEnvClr.b * csp->getColorInhRate());
        // The game left env's alpha as the slot's last particle had it (no shader reads it): the parent's.
        mEnvClr.a = pParent->mEnvClr.a;
    } else {
        csp->getPrmClr(&mPrmClr);
        csp->getEnvClr(&mEnvClr);
    }

    mPrmColorAlphaAnm = 0xff;
    if (csp->isAlphaInherited()) {
        mPrmClr.a = (s32)(COLOR_MULTI(pParent->mPrmClr.a, pParent->mPrmColorAlphaAnm) * csp->getAlphaInhRate());
    } else {
        mPrmClr.a = csp->getPrmAlpha();
    }

    mRotateAngle = pParent->getRotateAngle();
    if (csp->isRotateOn()) {
        mRotateSpeed = csp->getRotInitSpeed();
    } else {
        mRotateSpeed = 0;
    }

    mTexAnmIdx = 0;
}

bool ParticleCore::calc_p(EmitterWork* pWork) {
    if (++mAge >= mLifeTime) {
        return true;
    }

    mTime = (f32)mAge / (f32)mLifeTime;

    if (checkStatus(0x20)) {
        mOffsetPosition.set(pWork->mGlobalPos);
    }

    mVelType2.zero();
    if (!checkStatus(0x40)) {
        pWork->mpRes->calcField(pWork, this);
    }

    mVelType2.add(mVelType0);
    mVelType1.scale(pWork->mpEmtr->mAirResist);
    f32 ratio = mMoment * mDrag;
    mVelocity.set< f32 >(ratio * (mVelType1.x + mVelType2.x), ratio * (mVelType1.y + mVelType2.y), ratio * (mVelType1.z + mVelType2.z));

    if (pWork->mpEmtr->mpPtclCallBack != nullptr) {
        pWork->mpEmtr->mpPtclCallBack->execute(pWork->mpEmtr, this);
    }

    if (checkStatus(2)) {
        return true;
    }

    pWork->mpRes->calc_p(pWork, this);
    u16 angle = getRotateAngle();
    mRotateAngle = angle + mRotateSpeed;

    if (pWork->mpRes->getCsp() != nullptr && canCreateChild(pWork)) {
        for (int i = pWork->mpRes->getCsp()->getRate(); i > 0; i--) {
            pWork->mpEmtr->createChild(this);
        }
    }

    mLocalPosition.add(mVelocity);
    mPosition.set< f32 >(mOffsetPosition.x + mLocalPosition.x * pWork->mPublicScale.x, mOffsetPosition.y + mLocalPosition.y * pWork->mPublicScale.y,
                         mOffsetPosition.z + mLocalPosition.z * pWork->mPublicScale.z);

    return false;
}

bool ParticleCore::calc_c(EmitterWork* pWork) {
    if (++mAge >= mLifeTime) {
        return true;
    }

    mTime = (f32)mAge / (f32)mLifeTime;

    if (mAge != 0) {
        if (checkStatus(0x20)) {
            mOffsetPosition.set(pWork->mGlobalPos);
        }

        mVelType1.y -= pWork->mpRes->getCsp()->getGravity();
        mVelType2.zero();
        if (!checkStatus(0x40)) {
            pWork->mpRes->calcField(pWork, this);
        }

        mVelType2.add(mVelType0);
        mVelType1.scale(pWork->mpEmtr->mAirResist);
        f32 ratio = mMoment * mDrag;
        mVelocity.set< f32 >(ratio * (mVelType1.x + mVelType2.x), ratio * (mVelType1.y + mVelType2.y), ratio * (mVelType1.z + mVelType2.z));
    }

    if (pWork->mpEmtr->mpPtclCallBack != nullptr) {
        pWork->mpEmtr->mpPtclCallBack->execute(pWork->mpEmtr, this);
    }

    if (checkStatus(2)) {
        return true;
    }

    pWork->mpRes->calc_c(pWork, this);
    u16 angle = getRotateAngle();
    mRotateAngle = angle + mRotateSpeed;
    mLocalPosition.add(mVelocity);
    mPosition.set< f32 >(mOffsetPosition.x + mLocalPosition.x * pWork->mPublicScale.x, mOffsetPosition.y + mLocalPosition.y * pWork->mPublicScale.y,
                         mOffsetPosition.z + mLocalPosition.z * pWork->mPublicScale.z);

    return false;
}

bool ParticleCore::canCreateChild(EmitterWork* pWork) {
    ChildShape* csp = pWork->mpRes->getCsp();
    int sinceStart = mAge - (int)((mLifeTime - 1) * csp->getTiming());
    return sinceStart >= 0 && sinceStart % (csp->getStep() + 1) == 0;
}

/* NEW IN SMG1 */
f32 ParticleCore::getCalcCurrentPositionX(const EmitterCore* pEmitter) const {
    return (mOffsetPosition.x + (pEmitter->mpEmtrMgr->pWd->mPublicScale.x) * (mLocalPosition.x + mVelocity.x));
}

f32 ParticleCore::getCalcCurrentPositionY(const EmitterCore* pEmitter) const {
    return (mOffsetPosition.y + (pEmitter->mpEmtrMgr->pWd->mPublicScale.y) * (mLocalPosition.y + mVelocity.y));
}

f32 ParticleCore::getCalcCurrentPositionZ(const EmitterCore* pEmitter) const {
    return (mOffsetPosition.z + (pEmitter->mpEmtrMgr->pWd->mPublicScale.z) * (mLocalPosition.z + mVelocity.z));
}
