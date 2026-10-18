/* The RVL SDK's larger matrix and quaternion functions natively (the inline ones are in nebula_mtx.h). The PS
   functions follow the console's paired-single code (Petari's src/RVL_SDK/mtx); the C ones are Petari's C source.
   Outputs may be inputs: every result is computed before it is stored. */
#include <math.h>
#include <string.h>

#include <revolution/mtx.h>

/* 0 when the 3x3 part is singular (inv untouched), else 1 with inv = src's inverse. The console's reciprocal was
   fres refined once; natively it is exact. */
u32 PSMTXInverse(const Mtx src, Mtx inv) {
    const f32 m00 = src[0][0], m01 = src[0][1], m02 = src[0][2], m03 = src[0][3];
    const f32 m10 = src[1][0], m11 = src[1][1], m12 = src[1][2], m13 = src[1][3];
    const f32 m20 = src[2][0], m21 = src[2][1], m22 = src[2][2], m23 = src[2][3];
    const f32 c00 = m11 * m22 - m12 * m21, c01 = m12 * m20 - m10 * m22, c02 = m10 * m21 - m11 * m20;
    const f32 det = m00 * c00 + m01 * c01 + m02 * c02;
    if (det == 0.0f) {
        return 0;
    }
    const f32 r = 1.0f / det;
    Mtx out;
    out[0][0] = c00 * r;
    out[0][1] = (m02 * m21 - m01 * m22) * r;
    out[0][2] = (m01 * m12 - m02 * m11) * r;
    out[1][0] = c01 * r;
    out[1][1] = (m00 * m22 - m02 * m20) * r;
    out[1][2] = (m02 * m10 - m00 * m12) * r;
    out[2][0] = c02 * r;
    out[2][1] = (m01 * m20 - m00 * m21) * r;
    out[2][2] = (m00 * m11 - m01 * m10) * r;
    out[0][3] = -(out[0][0] * m03 + out[0][1] * m13 + out[0][2] * m23);
    out[1][3] = -(out[1][0] * m03 + out[1][1] * m13 + out[1][2] * m23);
    out[2][3] = -(out[2][0] * m03 + out[2][1] * m13 + out[2][2] * m23);
    memcpy(inv, out, sizeof(Mtx));
    return 1;
}

/* The transpose of the inverse of src's 3x3 part, translation 0 (for transforming normals); 0 when singular. */
u32 PSMTXInvXpose(const Mtx src, Mtx invX) {
    const f32 m00 = src[0][0], m01 = src[0][1], m02 = src[0][2];
    const f32 m10 = src[1][0], m11 = src[1][1], m12 = src[1][2];
    const f32 m20 = src[2][0], m21 = src[2][1], m22 = src[2][2];
    const f32 c00 = m11 * m22 - m12 * m21, c01 = m12 * m20 - m10 * m22, c02 = m10 * m21 - m11 * m20;
    const f32 det = m00 * c00 + m01 * c01 + m02 * c02;
    if (det == 0.0f) {
        return 0;
    }
    const f32 r = 1.0f / det;
    Mtx out;
    out[0][0] = c00 * r;
    out[0][1] = c01 * r;
    out[0][2] = c02 * r;
    out[1][0] = (m02 * m21 - m01 * m22) * r;
    out[1][1] = (m00 * m22 - m02 * m20) * r;
    out[1][2] = (m01 * m20 - m00 * m21) * r;
    out[2][0] = (m01 * m12 - m02 * m11) * r;
    out[2][1] = (m02 * m10 - m00 * m12) * r;
    out[2][2] = (m00 * m11 - m01 * m10) * r;
    out[0][3] = 0.0f;
    out[1][3] = 0.0f;
    out[2][3] = 0.0f;
    memcpy(invX, out, sizeof(Mtx));
    return 1;
}

/* A rotation about the x, y or z axis (either case) from its sine and cosine; any other axis leaves m untouched. */
void PSMTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA) {
    switch (axis | 0x20) {
    case 'x':
        m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f;  m[0][3] = 0.0f;
        m[1][0] = 0.0f; m[1][1] = cosA; m[1][2] = -sinA; m[1][3] = 0.0f;
        m[2][0] = 0.0f; m[2][1] = sinA; m[2][2] = cosA;  m[2][3] = 0.0f;
        break;
    case 'y':
        m[0][0] = cosA;  m[0][1] = 0.0f; m[0][2] = sinA; m[0][3] = 0.0f;
        m[1][0] = 0.0f;  m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
        m[2][0] = -sinA; m[2][1] = 0.0f; m[2][2] = cosA; m[2][3] = 0.0f;
        break;
    case 'z':
        m[0][0] = cosA; m[0][1] = -sinA; m[0][2] = 0.0f; m[0][3] = 0.0f;
        m[1][0] = sinA; m[1][1] = cosA;  m[1][2] = 0.0f; m[1][3] = 0.0f;
        m[2][0] = 0.0f; m[2][1] = 0.0f;  m[2][2] = 1.0f; m[2][3] = 0.0f;
        break;
    default:
        break;
    }
}

/* A rotation of rad radians about axis, which is normalised first (the console's frsqrte refined once). */
void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) {
    const f32 s = sinf(rad), c = cosf(rad), t = 1.0f - c;
    const f32 ax = axis->x, ay = axis->y, az = axis->z;
    const f32 n = __NebulaRsqrtRefined((ax * ax + az * az) + ay * ay);
    const f32 x = ax * n, y = ay * n, z = az * n;
    const f32 tx = t * x, ty = t * y, tz = t * z;
    m[0][0] = tx * x + c;     m[0][1] = tx * y - s * z; m[0][2] = tx * z + s * y; m[0][3] = 0.0f;
    m[1][0] = tx * y + s * z; m[1][1] = ty * y + c;     m[1][2] = ty * z - s * x; m[1][3] = 0.0f;
    m[2][0] = tx * z - s * y; m[2][1] = ty * z + s * x; m[2][2] = tz * z + c;     m[2][3] = 0.0f;
}

/* The rotation of a quaternion, normalised by 2/|q|^2 (the console's fres refined once). */
void PSMTXQuat(Mtx m, const Quaternion* q) {
    const f32 x = q->x, y = q->y, z = q->z, w = q->w;
    const f32 s = 2.0f / ((x * x + z * z) + (y * y + w * w));
    const f32 xx = x * x, yy = y * y, zz = z * z;
    const f32 xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    m[0][0] = 1.0f - s * (yy + zz); m[0][1] = s * (xy - wz);        m[0][2] = s * (xz + wy);        m[0][3] = 0.0f;
    m[1][0] = s * (xy + wz);        m[1][1] = 1.0f - s * (xx + zz); m[1][2] = s * (yz - wx);        m[1][3] = 0.0f;
    m[2][0] = s * (xz - wy);        m[2][1] = s * (yz + wx);        m[2][2] = 1.0f - s * (xx + yy); m[2][3] = 0.0f;
}

/* dst[i] = m * (src[i], 0) for count vectors (count >= 1 on the console's loop). */
void PSMTXMultVecArraySR(const Mtx m, const Vec* src, Vec* dst, u32 count) {
    for (u32 i = 0; i < count; i++) {
        PSMTXMultVecSR(m, &src[i], &dst[i]);
    }
}

void C_MTXLookAt(Mtx m, const Point3d* camPos, const Vec* camUp, const Point3d* target) {
    Vec vLook, vRight, vUp;

    vLook.x = camPos->x - target->x;
    vLook.y = camPos->y - target->y;
    vLook.z = camPos->z - target->z;
    PSVECNormalize(&vLook, &vLook);
    PSVECCrossProduct(camUp, &vLook, &vRight);
    PSVECNormalize(&vRight, &vRight);
    PSVECCrossProduct(&vLook, &vRight, &vUp);

    m[0][0] = vRight.x;
    m[0][1] = vRight.y;
    m[0][2] = vRight.z;
    m[0][3] = -(camPos->x * vRight.x + camPos->y * vRight.y + camPos->z * vRight.z);
    m[1][0] = vUp.x;
    m[1][1] = vUp.y;
    m[1][2] = vUp.z;
    m[1][3] = -(camPos->x * vUp.x + camPos->y * vUp.y + camPos->z * vUp.z);
    m[2][0] = vLook.x;
    m[2][1] = vLook.y;
    m[2][2] = vLook.z;
    m[2][3] = -(camPos->x * vLook.x + camPos->y * vLook.y + camPos->z * vLook.z);
}

void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    const f32 angle = MTXDegToRad(fovY * 0.5f);
    const f32 cot = 1.0f / (f32)tan(angle);
    m[0][0] = (cot / aspect) * scaleS;
    m[0][1] = 0.0f;
    m[0][2] = -transS;
    m[0][3] = 0.0f;
    m[1][0] = 0.0f;
    m[1][1] = cot * scaleT;
    m[1][2] = -transT;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -1.0f;
    m[2][3] = 0.0f;
}

void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = (2.0f * tmp * scaleS);
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = ((-(r + l) * tmp) * scaleS) + transS;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = (2.0f * tmp) * scaleT;
    m[1][2] = 0.0f;
    m[1][3] = ((-(t + b) * tmp) * scaleT) + transT;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = 0.0f;
    m[2][3] = 1.0f;
}

void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
    const f32 angle = MTXDegToRad(fovY * 0.5f);
    const f32 cot = 1.0f / (f32)tan(angle);
    const f32 tmp = 1.0f / (f - n);
    m[0][0] = cot / aspect;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = 0.0f;
    m[1][0] = 0.0f;
    m[1][1] = cot;
    m[1][2] = 0.0f;
    m[1][3] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -(n)*tmp;
    m[2][3] = -(f * n) * tmp;
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = -1.0f;
    m[3][3] = 0.0f;
}

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    f32 tmp = 1.0f / (r - l);
    m[0][0] = 2.0f * tmp;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = -(r + l) * tmp;
    tmp = 1.0f / (t - b);
    m[1][0] = 0.0f;
    m[1][1] = 2.0f * tmp;
    m[1][2] = 0.0f;
    m[1][3] = -(t + b) * tmp;
    tmp = 1.0f / (f - n);
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = -(1.0f) * tmp;
    m[2][3] = -(f)*tmp;
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
}

void C_QUATMtx(Quaternion* r, const Mtx m) {
    f32 tr, s;
    s32 i, j, k;
    const s32 nxt[3] = {1, 2, 0};
    f32 q[3];

    tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        tr = sqrtf(1.0f + tr);
        r->w = tr * 0.5f;
        s = 0.5f / tr;
        r->x = (m[2][1] - m[1][2]) * s;
        r->y = (m[0][2] - m[2][0]) * s;
        r->z = (m[1][0] - m[0][1]) * s;
    } else {
        i = 0;
        if (m[1][1] > m[0][0]) {
            i = 1;
        }
        if (m[2][2] > m[i][i]) {
            i = 2;
        }
        j = nxt[i];
        k = nxt[j];
        s = sqrtf((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
        q[i] = s * 0.5f;

        if (s != 0.0f) {
            s = 0.5f / s;
        }

        r->w = (m[k][j] - m[j][k]) * s;
        q[j] = (m[i][j] + m[j][i]) * s;
        q[k] = (m[i][k] + m[k][i]) * s;

        r->x = q[0];
        r->y = q[1];
        r->z = q[2];
    }
}

/* The console computes the angle terms in double (acos, sin) and rounds; so does this. */
void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
    f32 theta, sin_th, cos_th, tp, tq, sin_1mtth, sin_tth;

    cos_th = p->x * q->x + p->y * q->y + p->z * q->z + p->w * q->w;
    tq = 1.0f;

    if (cos_th < 0.0f) {
        cos_th = -cos_th;
        tq = -tq;
    }

    if (cos_th <= 1.0f - 0.00001f) {
        theta = (f32)acos(cos_th);
        sin_th = (f32)sin(theta);
        tp = 1.0f - t;
        sin_1mtth = (f32)sin(tp * theta);
        tp = sin_1mtth / sin_th;
        sin_tth = (f32)sin(t * theta);
        tq *= sin_tth / sin_th;
    } else {
        tp = 1.0f - t;
        tq = tq * t;
    }

    const f32 x = tp * p->x + tq * q->x;
    const f32 y = tp * p->y + tq * q->y;
    const f32 z = tp * p->z + tq * q->z;
    const f32 w = tp * p->w + tq * q->w;
    r->x = x;
    r->y = y;
    r->z = z;
    r->w = w;
}
