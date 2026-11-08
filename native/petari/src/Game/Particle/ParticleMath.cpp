#include "Game/Particle/ParticleMath.hpp"
#include <nebula_geometry.hpp>

// The rotation taking +z onto rDir: about the axis z x rDir, cosine rDir.z, sine |z x rDir|.
void ParticleMath::dirMtx(const Vec3f& rDir, Mtx dst) {
    Vec3f axis = Vec3f(rDir.y, -rDir.x, 0.0f);
    f32 sine = axis.length();

    if (sine <= FloatUtil::epsilon()) {
        axis.zero();
    } else {
        axis.scale(1.0f / sine);
    }

    f32 xx = axis.x * axis.x;
    f32 yy = axis.y * axis.y;
    f32 xSine = axis.x * sine;
    f32 ySine = axis.y * sine;
    f32 xy = (1.0f - rDir.z) * (axis.x * axis.y);
    dst[0][0] = xx + rDir.z * (1.0f - xx);
    dst[0][1] = xy;
    dst[0][2] = -ySine;
    dst[0][3] = 0.0f;
    dst[1][0] = xy;
    dst[1][1] = yy + rDir.z * (1.0f - yy);
    dst[1][2] = xSine;
    dst[1][3] = 0.0f;
    dst[2][0] = ySine;
    dst[2][1] = -xSine;
    dst[2][2] = rDir.z;
    dst[2][3] = 0.0f;
}

void ParticleMath::yzRotateMtx(s16 angleY, s16 angleZ, Mtx dst) {
    f32 cosy = Angle::cosShort(angleY);
    f32 cosz = Angle::cosShort(angleZ);
    f32 siny = Angle::sinShort(angleY);
    f32 sinz = Angle::sinShort(angleZ);
    dst[0][0] = (cosy * cosz);
    dst[0][1] = -sinz;
    dst[0][2] = (siny * cosz);
    dst[1][0] = (cosy * sinz);
    dst[1][1] = cosz;
    dst[1][2] = (siny * sinz);
    dst[2][0] = -siny;
    dst[2][2] = cosy;
    dst[2][3] = 0.0f;
    dst[2][1] = 0.0f;
    dst[1][3] = 0.0f;
    dst[0][3] = 0.0f;
}

void ParticleMath::xyzRotateMtx(s16 x, s16 y, s16 z, Mtx mtx) {
    f32 cosx = Angle::cosShort(x);
    f32 siny = Angle::sinShort(y);
    f32 cosy = Angle::cosShort(y);
    f32 cosz = Angle::cosShort(z);
    f32 sinx = Angle::sinShort(x);
    f32 sinz = Angle::sinShort(z);
    mtx[0][0] = cosy * cosz;
    mtx[1][0] = cosy * sinz;
    mtx[2][0] = -siny;
    mtx[2][1] = sinx * cosy;
    mtx[2][2] = cosx * cosy;
    f32 product1 = cosx * sinz;
    f32 product2 = sinx * cosz;
    mtx[0][1] = product2 * siny - product1;
    mtx[1][2] = product1 * siny - product2;
    product1 = sinx * sinz;
    product2 = cosx * cosz;
    mtx[0][2] = product1 + product2 * siny;
    mtx[1][1] = product2 + product1 * siny;
    mtx[2][3] = 0.0f;
    mtx[1][3] = 0.0f;
    mtx[0][3] = 0.0f;
}

void ParticleMath::splitR(const Mtx src, Mtx rotation) {
    Vec3f scale;
    Vec3f translation;
    ParticleMath::splitSRT(src, rotation, &scale, &translation);
}

void ParticleMath::splitRT(const Mtx src, Mtx rotation, Vec3f* pTranslation) {
    Vec3f scale;
    ParticleMath::splitSRT(src, rotation, &scale, pTranslation);
}

// Scale is each column's length; rotation is the columns divided by it (identity where a scale is zero).
void ParticleMath::splitSRT(const Mtx src, Mtx rotation, Vec3f* pScale, Vec3f* pTranslation) {
    Vec3f column = Vec3f(src[0][0], src[1][0], src[2][0]);
    pScale->x = column.length();
    column.set< f32 >(src[0][1], src[1][1], src[2][1]);
    pScale->y = column.length();
    column.set< f32 >(src[0][2], src[1][2], src[2][2]);
    pScale->z = column.length();
    NebulaMtxIdentity(rotation);
    if (pScale->x != 0.0f) {
        f32 inverse = 1.0f / pScale->x;
        rotation[0][0] = src[0][0] * inverse;
        rotation[1][0] = src[1][0] * inverse;
        rotation[2][0] = src[2][0] * inverse;
    }

    if (pScale->y != 0.0f) {
        f32 inverse = 1.0f / pScale->y;
        rotation[0][1] = src[0][1] * inverse;
        rotation[1][1] = src[1][1] * inverse;
        rotation[2][1] = src[2][1] * inverse;
    }

    if (pScale->z != 0.0f) {
        f32 inverse = 1.0f / pScale->z;
        rotation[0][2] = src[0][2] * inverse;
        rotation[1][2] = src[1][2] * inverse;
        rotation[2][2] = src[2][2] * inverse;
    }

    pTranslation->set< f32 >(src[0][3], src[1][3], src[2][3]);
}

// pKeys: count keys of four floats (time, value, in tangent, out tangent), in time order. Before the first key its value,
// from the last key on its value, else the Hermite curve between the two keys around time.
f32 ParticleMath::keyValue(f32 time, u32 count, const f32* pKeys) {
    if (time < pKeys[0]) {
        return pKeys[1];
    }

    int last = count - 1;
    if (pKeys[last * 4] <= time) {
        return (pKeys + last * 4)[1];
    }

    int remaining = count;
    while (remaining > 1) {
        u32 half = remaining / 2;
        if (time >= pKeys[half * 4]) {
            pKeys += half * 4;
            remaining -= half;
        } else {
            remaining = half;
        }
    }

    return hermiteInterpolation(time, pKeys[0], pKeys[1], pKeys[3], pKeys[4], pKeys[5], pKeys[6]);
}
