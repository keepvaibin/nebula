/* The RVL SDK's matrix, vector and quaternion functions natively (included by revolution/mtx.h under TARGET_PC).
   The small ones are inline here, so the game's ~1,000 call sites pay no call (docs/NATIVE_PERFORMANCE.md); the rest
   are in native/sdk/mtx/mtx_native.c. Each follows the console's paired-single code: the same operations, grouped as
   it grouped them, and every input read before any output is written, so the output may be an input. Square roots are
   exact (the console refined frsqrte once); reciprocal square roots are the host estimate plus that refinement. */
#ifndef NEBULA_MTX_H
#define NEBULA_MTX_H

#include <math.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

static inline f32 __NebulaRsqrtRefined(f32 x) {
    const f32 e = __frsqrte(x);
    return (3.0f - (e * e) * x) * (e * 0.5f);
}

static inline void PSMTXIdentity(Mtx m) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = 0.0f;
}

static inline void PSMTXCopy(const Mtx src, Mtx dst) {
    __builtin_memmove(dst, src, sizeof(Mtx));
}

/* ab = a * b, both 3x4 with the implied (0, 0, 0, 1) row. */
static inline void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
#if defined(__SSE2__)
    const __m128 b0 = _mm_loadu_ps(b[0]), b1 = _mm_loadu_ps(b[1]), b2 = _mm_loadu_ps(b[2]);
    const __m128 lane3 = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
    __m128 r[3];
    for (int i = 0; i < 3; i++) {
        const __m128 ai = _mm_loadu_ps(a[i]);
        __m128 t = _mm_mul_ps(_mm_shuffle_ps(ai, ai, 0x00), b0);
        t = _mm_add_ps(t, _mm_mul_ps(_mm_shuffle_ps(ai, ai, 0x55), b1));
        t = _mm_add_ps(t, _mm_mul_ps(_mm_shuffle_ps(ai, ai, 0xAA), b2));
        r[i] = _mm_add_ps(t, _mm_and_ps(ai, lane3)); /* + a[i][3] in the translation column only */
    }
    _mm_storeu_ps(ab[0], r[0]);
    _mm_storeu_ps(ab[1], r[1]);
    _mm_storeu_ps(ab[2], r[2]);
#else
    f32 r[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            r[i][j] = (a[i][0] * b[0][j] + a[i][1] * b[1][j]) + a[i][2] * b[2][j];
        }
        r[i][3] += a[i][3];
    }
    __builtin_memcpy(ab, r, sizeof(r));
#endif
}

/* dst = m * (src, 1). */
static inline void PSMTXMultVec(const Mtx m, const Vec* src, Vec* dst) {
    const f32 x = src->x, y = src->y, z = src->z;
    const f32 rx = (m[0][0] * x + m[0][2] * z) + (m[0][1] * y + m[0][3]);
    const f32 ry = (m[1][0] * x + m[1][2] * z) + (m[1][1] * y + m[1][3]);
    const f32 rz = (m[2][0] * x + m[2][2] * z) + (m[2][1] * y + m[2][3]);
    dst->x = rx;
    dst->y = ry;
    dst->z = rz;
}

/* dst = m * (src, 0): rotation and scale only. */
static inline void PSMTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) {
    const f32 x = src->x, y = src->y, z = src->z;
    const f32 rx = m[0][2] * z + (m[0][0] * x + m[0][1] * y);
    const f32 ry = m[1][2] * z + (m[1][0] * x + m[1][1] * y);
    const f32 rz = m[2][2] * z + (m[2][0] * x + m[2][1] * y);
    dst->x = rx;
    dst->y = ry;
    dst->z = rz;
}

static inline void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = xT;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = yT;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = zT;
}

/* dst = translation(xT, yT, zT) * src. */
static inline void PSMTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT) {
    const f32 t0 = src[0][3] + xT, t1 = src[1][3] + yT, t2 = src[2][3] + zT;
    if (dst != src) {
        __builtin_memcpy(dst, src, sizeof(Mtx));
    }
    dst[0][3] = t0;
    dst[1][3] = t1;
    dst[2][3] = t2;
}

static inline void PSMTXScale(Mtx m, f32 xS, f32 yS, f32 zS) {
    m[0][0] = xS;   m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = yS;   m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = zS;   m[2][3] = 0.0f;
}

/* dst = scale(xS, yS, zS) * src: each row scaled, translation included. */
static inline void PSMTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS) {
    const f32 s[3] = {xS, yS, zS};
    for (int i = 0; i < 3; i++) {
        const f32 r0 = src[i][0] * s[i], r1 = src[i][1] * s[i], r2 = src[i][2] * s[i], r3 = src[i][3] * s[i];
        dst[i][0] = r0;
        dst[i][1] = r1;
        dst[i][2] = r2;
        dst[i][3] = r3;
    }
}

static inline void PSMTX44Identity(Mtx44 m) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            m[i][j] = i == j ? 1.0f : 0.0f;
        }
    }
}

static inline void PSMTX44Copy(const Mtx44 src, Mtx44 dst) {
    __builtin_memmove(dst, src, sizeof(Mtx44));
}

static inline void PSVECAdd(const Vec* a, const Vec* b, Vec* dst) {
    const f32 x = a->x + b->x, y = a->y + b->y, z = a->z + b->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline void PSVECSubtract(const Vec* a, const Vec* b, Vec* dst) {
    const f32 x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline void PSVECScale(const Vec* src, Vec* dst, f32 mult) {
    const f32 x = src->x * mult, y = src->y * mult, z = src->z * mult;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

/* No zero test, as on the console: a zero vector normalises to NaN. */
static inline void PSVECNormalize(const Vec* src, Vec* dst) {
    const f32 x = src->x, y = src->y, z = src->z;
    const f32 r = __NebulaRsqrtRefined((x * x + z * z) + y * y);
    dst->x = x * r;
    dst->y = y * r;
    dst->z = z * r;
}

static inline f32 PSVECMag(const Vec* v) {
    return __builtin_sqrtf((v->z * v->z + v->x * v->x) + v->y * v->y);
}

static inline f32 C_VECMag(const Vec* v) {
    return __builtin_sqrtf((v->x * v->x) + (v->y * v->y) + (v->z * v->z));
}

static inline f32 PSVECDotProduct(const Vec* a, const Vec* b) {
    return (a->x * b->x + a->y * b->y) + a->z * b->z;
}

static inline void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* dst) {
    const f32 x = a->y * b->z - b->y * a->z;
    const f32 y = b->x * a->z - a->x * b->z;
    const f32 z = b->y * a->x - a->y * b->x;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline f32 PSVECDistance(const Vec* a, const Vec* b) {
    const f32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return __builtin_sqrtf((dx * dx + dy * dy) + dz * dz);
}

/* pq = p * q (Hamilton product). */
static inline void PSQUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) {
    const f32 px = p->x, py = p->y, pz = p->z, pw = p->w;
    const f32 qx = q->x, qy = q->y, qz = q->z, qw = q->w;
    const f32 x = (pw * qx + px * qw) + (py * qz - pz * qy);
    const f32 y = (pw * qy + py * qw) + (pz * qx - px * qz);
    const f32 z = (pw * qz + pz * qw) + (px * qy - py * qx);
    const f32 w = (pw * qw - px * qx) - (py * qy + pz * qz);
    pq->x = x;
    pq->y = y;
    pq->z = z;
    pq->w = w;
}

static inline f32 PSQUATDotProduct(const Quaternion* p, const Quaternion* q) {
    return (p->z * q->z + p->x * q->x) + (p->w * q->w + p->y * q->y);
}

#endif
