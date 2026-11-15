#include "Game/Particle/EmitterDynamics.hpp"
#include <nebula_geometry.hpp>
#include "Game/Particle/EmitterCore.hpp"

void volumePoint(EmitterWork* work) {
    work->mVolumeCalcData.mVolumePos.zero();
    f32 z = work->mpEmtr->mRndm.get_rndm_f() - 0.5f;
    f32 y = work->mpEmtr->mRndm.get_rndm_f() - 0.5f;
    f32 x = work->mpEmtr->mRndm.get_rndm_f() - 0.5f;
    work->mVolumeCalcData.mVelOmni.set< f32 >(x, y, z);
    work->mVolumeCalcData.mVelAxis.set< f32 >(work->mVolumeCalcData.mVelOmni.x, 0.0f, work->mVolumeCalcData.mVelOmni.z);
}

void volumeLine(EmitterWork* work) {
    if (work->mpEmtr->checkFlag(DynamicsFlag_FixedInterval)) {
        work->mVolumeCalcData.mVolumePos.set< f32 >(0.0f, 0.0f, work->mVolumeSize * ((work->mVolumeEmitIdx / (work->mEmitCount - 1.0f) - 0.5f)));
        work->mVolumeEmitIdx++;
    } else {
        work->mVolumeCalcData.mVolumePos.set< f32 >(0.0f, 0.0f, work->mVolumeSize * (work->mpEmtr->mRndm.get_rndm_f() - 0.5f));
    }

    work->mVolumeCalcData.mVelOmni.set< f32 >(0.0f, 0.0f, work->mVolumeCalcData.mVolumePos.z * work->mGlobalScl.z);
    work->mVolumeCalcData.mVelAxis.set< f32 >(0.0f, 0.0f, work->mVolumeCalcData.mVolumePos.z);
}

void volumeCircle(EmitterWork* work) {
    s16 theta;
    f32 distance;

    if (work->mpEmtr->checkFlag(DynamicsFlag_FixedInterval)) {
        theta = (s16)((work->mVolumeEmitIdx << 16) / work->mEmitCount);
        theta *= work->mVolumeSweep;
        work->mVolumeEmitIdx++;
    } else {
        theta = (s32)(work->mVolumeSweep * work->mpEmtr->get_r_ss());
    }

    distance = work->mpEmtr->get_r_f();
    if (work->mpEmtr->checkFlag(DynamicsFlag_FixedDensity)) {
        distance = 1.0f - (distance * distance);
    }

    distance = work->mVolumeSize * (work->mVolumeMinRad + distance * (1.0f - work->mVolumeMinRad));
    work->mVolumeCalcData.mVolumePos.set< f32 >(distance * Angle::sinShort(theta), 0.0f, distance * Angle::cosShort(theta));
    work->mVolumeCalcData.mVelOmni.mul(work->mVolumeCalcData.mVolumePos, work->mGlobalScl);
    work->mVolumeCalcData.mVelAxis.set< f32 >(work->mVolumeCalcData.mVolumePos.x, 0.0f, work->mVolumeCalcData.mVolumePos.z);
}

void volumeCube(EmitterWork* work) {
    f32 z = work->mpEmtr->mRndm.get_rndm_f() - 0.5f;
    f32 y = work->mpEmtr->mRndm.get_rndm_f() - 0.5f;
    f32 x = work->mpEmtr->mRndm.get_rndm_f() - 0.5f;
    work->mVolumeCalcData.mVolumePos.set< f32 >(work->mVolumeSize * x, work->mVolumeSize * y, work->mVolumeSize * z);
    work->mVolumeCalcData.mVelOmni.mul(work->mVolumeCalcData.mVolumePos, work->mGlobalScl);
    work->mVolumeCalcData.mVelAxis.set< f32 >(work->mVolumeCalcData.mVolumePos.x, 0.0f, work->mVolumeCalcData.mVolumePos.z);
}

static void volumeSphere(EmitterWork* work) {
    s16 phi, theta;
    if (work->mpEmtr->checkFlag(DynamicsFlag_FixedInterval)) {
        // A ring of one angle (mVolumeAngleMax 1) or one division puts every particle at the first angle.
        const s32 divisions = work->mDivNumber - 1;
        const s32 angles = work->mVolumeAngleMax - 1;
        phi = (u16)((divisions != 0 ? work->mVolumeX * 0x8000 / divisions : 0) + 0x4000);
        u16 angle = (u16)(angles != 0 ? work->mVolumeAngleNum * 0x10000 / angles : 0);
        theta = (s32)((f32)angle * work->mVolumeSweep + 0x8000);
        work->mVolumeAngleNum++;
        if (work->mVolumeAngleNum == work->mVolumeAngleMax) {
            work->mVolumeAngleNum = 0;
            work->mVolumeX++;
            if (work->mVolumeX * 2 < work->mDivNumber) {
                work->mVolumeAngleMax = work->mVolumeAngleMax != 1 ? work->mVolumeAngleMax + 4 : work->mVolumeAngleMax + 3;
            } else {
                work->mVolumeAngleMax = work->mVolumeAngleMax != 4 ? work->mVolumeAngleMax - 4 : 1;
            }
        }
    } else {
        phi = work->mpEmtr->get_r_ss() >> 1;
        theta = (s32)(work->mVolumeSweep * work->mpEmtr->get_r_ss());
    }

    f32 rnd = work->mpEmtr->get_r_f();
    if (work->mpEmtr->checkFlag(DynamicsFlag_FixedDensity)) {
        rnd = 1.0f - rnd * rnd * rnd;
    }
    rnd = work->mVolumeSize * (work->mVolumeMinRad + rnd * (1.0f - work->mVolumeMinRad));
    work->mVolumeCalcData.mVolumePos.set< f32 >(rnd * Angle::cosShort(phi) * Angle::sinShort(theta), -rnd * Angle::sinShort(phi), rnd * Angle::cosShort(phi) * Angle::cosShort(theta));
    work->mVolumeCalcData.mVelOmni.mul(work->mVolumeCalcData.mVolumePos, work->mGlobalScl);
    work->mVolumeCalcData.mVelAxis.set< f32 >(work->mVolumeCalcData.mVolumePos.x, 0.0f, work->mVolumeCalcData.mVolumePos.z);
}

static void volumeCylinder(EmitterWork* work) {
    s16 theta = (s32)(work->mVolumeSweep * work->mpEmtr->get_r_ss());
    f32 rnd = work->mpEmtr->get_r_f();
    if (work->mpEmtr->checkFlag(DynamicsFlag_FixedDensity)) {
        rnd = 1.0f - rnd * rnd;
    }
    f32 rad = work->mVolumeSize * (work->mVolumeMinRad + rnd * (1.0f - work->mVolumeMinRad));
    work->mVolumeCalcData.mVolumePos.set< f32 >(rad * Angle::sinShort(theta), work->mVolumeSize * work->mpEmtr->get_r_zp(), rad * Angle::cosShort(theta));
    work->mVolumeCalcData.mVelOmni.mul(work->mVolumeCalcData.mVolumePos, work->mGlobalScl);
    work->mVolumeCalcData.mVelAxis.set< f32 >(work->mVolumeCalcData.mVolumePos.x, 0.0f, work->mVolumeCalcData.mVolumePos.z);
}

static void volumeTorus(EmitterWork* work) {
    s16 theta = (s32)(work->mVolumeSweep * work->mpEmtr->get_r_ss());
    s16 phi = work->mpEmtr->get_r_ss();
    f32 rad = work->mVolumeSize * work->mVolumeMinRad;
    work->mVolumeCalcData.mVelAxis.set< f32 >(rad * Angle::sinShort(theta) * Angle::cosShort(phi), rad * Angle::sinShort(phi), rad * Angle::cosShort(theta) * Angle::cosShort(phi));
    work->mVolumeCalcData.mVolumePos.set< f32 >(work->mVolumeCalcData.mVelAxis.x + work->mVolumeSize * Angle::sinShort(theta),
                                                work->mVolumeCalcData.mVelAxis.y,
                                                work->mVolumeCalcData.mVelAxis.z + work->mVolumeSize * Angle::cosShort(theta));
    work->mVolumeCalcData.mVelOmni.mul(work->mVolumeCalcData.mVolumePos, work->mGlobalScl);
}

EmitterDynamics::EmitterDynamics(const EmitterDynamicsData* pData) : mpData(pData) {
}

enum {
    VOL_Cube = 0x00,
    VOL_Sphere = 0x01,
    VOL_Cylinder = 0x02,
    VOL_Torus = 0x03,
    VOL_Point = 0x04,
    VOL_Circle = 0x05,
    VOL_Line = 0x06,
};

void EmitterDynamics::calc(EmitterWork* work) {
    switch (getVolumeType()) {
    case VOL_Cube:
        volumeCube(work);
        break;
    case VOL_Sphere:
        volumeSphere(work);
        break;
    case VOL_Cylinder:
        volumeCylinder(work);
        break;
    case VOL_Torus:
        volumeTorus(work);
        break;
    case VOL_Point:
        volumePoint(work);
        break;
    case VOL_Circle:
        volumeCircle(work);
        break;
    case VOL_Line:
        volumeLine(work);
        break;
    }
}

void EmitterDynamics::create(EmitterWork* work) {
    if (work->mpEmtr->checkStatus(EmitterStatus_RateStepEmit)) {
        s32 emitCount;
        s32 createCount;

        if (work->mpEmtr->checkFlag(DynamicsFlag_FixedInterval)) {
            s32 count;
            if (getVolumeType() == VOL_Sphere) {
                count = 4 * getDivNumber() * getDivNumber() + 2;
            } else {
                count = getDivNumber();
            }
            emitCount = count;

            work->mVolumeEmitIdx = 0;
        } else {
            f32 rateRndm = getRateRndm();
            EmitterCore* emtr = work->mpEmtr;
            f32 newPtclCount = emtr->mRate * (rateRndm * emtr->get_r_zp() + 1.0f);
            f32 newEmitCount = emtr->mEmitCount + newPtclCount;
            emtr->mEmitCount = newEmitCount;
            emitCount = (s32)newEmitCount;
            work->mpEmtr->mEmitCount -= emitCount;

            if (work->mpEmtr->checkStatus(EmitterStatus_FirstEmit) && 0.0f < newPtclCount && newPtclCount < 1.0f)
                emitCount = 1;
        }

        work->mEmitCount = emitCount;
        if (work->mpEmtr->checkStatus(EmitterStatus_StopEmit)) {
            emitCount = 0;
        }

        createCount = emitCount;
        while (createCount > 0) {
            ParticleCore* ptcl = work->mpEmtr->createParticle();
            if (ptcl == NULL)
                break;
            createCount--;
        }
    }

    EmitterCore* emtr = work->mpEmtr;
    if (++emtr->mRateStepTimer >= (emtr->mRateStep + 1)) {
        work->mpEmtr->mRateStepTimer -= (work->mpEmtr->mRateStep + 1);
        work->mpEmtr->setStatus(EmitterStatus_RateStepEmit);
    } else {
        work->mpEmtr->clearStatus(EmitterStatus_RateStepEmit);
    }

    work->mpEmtr->clearStatus(EmitterStatus_FirstEmit);
}
