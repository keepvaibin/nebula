#include "JSystem/JMath/JMath.hpp"
#include "JSystem/JMath/JMATrigonometric.hpp"

void JMAEulerToQuat(s16 x, s16 y, s16 z, Quaternion* quat) {
    f32 cosX = JMASCos(x / 2);
    f32 cosY = JMASCos(y / 2);
    f32 cosZ = JMASCos(z / 2);
    f32 sinX = JMASSin(x / 2);
    f32 sinY = JMASSin(y / 2);
    f32 sinZ = JMASSin(z / 2);

    f32 cyz = cosY * cosZ;
    f32 syz = sinY * sinZ;
    quat->w = cosX * (cyz) + sinX * (syz);
    quat->x = sinX * (cyz)-cosX * (syz);
    quat->y = cosZ * (cosX * sinY) + sinZ * (sinX * cosY);
    quat->z = sinZ * (cosX * cosY) - cosZ * (sinX * sinY);
}

void JMAQuatLerp(__REGISTER const Quaternion* p, __REGISTER const Quaternion* q, f32 t, Quaternion* dst) {
    __REGISTER f32 pxy, pzw, qxy, qzw;
    __REGISTER f32 dp;

    dp = (p->z * q->z + p->x * q->x) + (p->w * q->w + p->y * q->y);
    f32 local_78 = dp;

    if (local_78 < 0.0) {
        int unused;
        dst->x = -t * (p->x + q->x) + p->x;
        dst->y = -t * (p->y + q->y) + p->y;
        dst->z = -t * (p->z + q->z) + p->z;
        dst->w = -t * (p->w + q->w) + p->w;
    } else {
        dst->x = -t * (p->x - q->x) + p->x;
        dst->y = -t * (p->y - q->y) + p->y;
        dst->z = -t * (p->z - q->z) + p->z;
        dst->w = -t * (p->w - q->w) + p->w;
    }
}

void JMAVECScaleAdd(__REGISTER const Vec* vec1, __REGISTER const Vec* vec2, __REGISTER Vec* dst, __REGISTER f32 scale) {
    __REGISTER f32 v1xy;
    __REGISTER f32 v2xy;
    __REGISTER f32 rxy, v1z, v2z, rz;
    const f32 x = vec1->x * scale + vec2->x;
    const f32 y = vec1->y * scale + vec2->y;
    const f32 z = vec1->z * scale + vec2->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

void JMAVECLerp(__REGISTER const Vec* a, __REGISTER const Vec* b, __REGISTER Vec* dst, __REGISTER f32 t) {
    __REGISTER f32 axy, bxy, az, bz;
    const f32 x = (b->x - a->x) * t + a->x;
    const f32 y = (b->y - a->y) * t + a->y;
    const f32 z = (b->z - a->z) * t + a->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

void JMAMTXApplyScale(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) {
    Mtx scale;
    NebulaMtxScale(scale, x, y, z);
    NebulaMtxConcat(src, scale, dst);
}
