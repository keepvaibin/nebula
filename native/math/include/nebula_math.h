/* 3x4 and 4x4 matrices, vectors and quaternions. The small functions are inline here, so the game's ~1,000 call sites
   pay no call; the rest are in nebula_math.c. Operations keep their grouping, since positions depend on the bits, and
   read every input before writing any output (an output may be an input). Square roots and their reciprocals are
   exact: the same bits on every CPU. */
#ifndef NEBULA_MATH_H
#define NEBULA_MATH_H

#include <math.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(_M_ARM64)
#define NEBULA_MATH_NEON 1
#include <arm_neon.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    f32 x, y, z;
} Vec, *VecPtr, Point3d;

typedef struct {
    s16 x;
    s16 y;
    s16 z;
} S16Vec, *S16VecPtr;

typedef struct {
    f32 x, y, z, w;
} Quaternion, *QuaternionPtr;

typedef f32 Mtx23[2][3];

typedef f32 Mtx[3][4];
typedef f32 (*MtxPtr)[4];

typedef f32 Mtx44[4][4];
typedef f32 (*Mtx44Ptr)[4];

typedef f32 Mtx33[3][3];
typedef f32 (*Mtx3P)[3];

/* The larger ones (nebula_math.c). */
u32 NebulaMtxInverse(const Mtx src, Mtx inv);
u32 NebulaMtxInvXpose(const Mtx src, Mtx invX);
void NebulaMtxRotTrig(Mtx m, char axis, f32 sinA, f32 cosA);
void NebulaMtxRotAxisRad(Mtx m, const Vec* axis, f32 rad);
void NebulaMtxQuat(Mtx m, const Quaternion* q);
void NebulaMtxMultVecArraySR(const Mtx m, const Vec* src, Vec* dst, u32 count);
void NebulaMtxLookAt(Mtx m, const Point3d* camPos, const Vec* camUp, const Point3d* target);
void NebulaMtxLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT);
void NebulaMtxLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT);
void NebulaMtxPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f);
void NebulaMtxOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
void NebulaQuatFromMtx(Quaternion* r, const Mtx m);
void NebulaQuatSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t);
/* A rotation about the x, y or z axis by NebulaSin and NebulaCos. */
void NebulaMtxRotRad(Mtx m, char axis, f32 rad);

static inline void NebulaMtxIdentity(Mtx m) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = 0.0f;
}

static inline void NebulaMtxCopy(const Mtx src, Mtx dst) {
    __builtin_memmove(dst, src, sizeof(Mtx));
}

/* ab = a * b, both 3x4 with the implied (0, 0, 0, 1) row. */
static inline void NebulaMtxConcat(const Mtx a, const Mtx b, Mtx ab) {
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
#elif defined(NEBULA_MATH_NEON)
    const float32x4_t b0 = vld1q_f32(b[0]), b1 = vld1q_f32(b[1]), b2 = vld1q_f32(b[2]);
    const uint32x4_t lane3 = vsetq_lane_u32(0xFFFFFFFFu, vdupq_n_u32(0u), 3);
    float32x4_t r[3];
    for (int i = 0; i < 3; ++i) {
        const float32x4_t ai = vld1q_f32(a[i]);
        float32x4_t t = vmulq_laneq_f32(b0, ai, 0);
        t = vaddq_f32(t, vmulq_laneq_f32(b1, ai, 1));
        t = vaddq_f32(t, vmulq_laneq_f32(b2, ai, 2));
        r[i] = vaddq_f32(t, vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(ai), lane3))); /* + a[i][3] in the translation column only */
    }
    vst1q_f32(ab[0], r[0]);
    vst1q_f32(ab[1], r[1]);
    vst1q_f32(ab[2], r[2]);
#else
    f32 r[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            r[i][j] = (a[i][0] * b[0][j] + a[i][1] * b[1][j]) + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0.0f);
        }
    }
    __builtin_memcpy(ab, r, sizeof(r));
#endif
}

/* ab[i] = a * b[i] for count consecutive 3x4 matrices. Each product is complete before it is stored, so ab may be b.
   Only the translation column adds a's translation: a sum of -0 stays -0 in the other three. */
static inline void NebulaMtxArrayConcat(const Mtx a, const f32 (*b)[4], f32 (*ab)[4], u32 count) {
#if defined(__SSE2__)
    __m128 ax[3], ay[3], az[3], at[3];
    for (int i = 0; i < 3; i++) {
        const __m128 row = _mm_loadu_ps(a[i]);
        ax[i] = _mm_shuffle_ps(row, row, 0x00);
        ay[i] = _mm_shuffle_ps(row, row, 0x55);
        az[i] = _mm_shuffle_ps(row, row, 0xAA);
        at[i] = _mm_shuffle_ps(row, row, 0xFF);
    }
    const __m128 lane3 = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
    for (u32 n = 0; n < count; n++, b += 3, ab += 3) {
        const __m128 b0 = _mm_loadu_ps(b[0]), b1 = _mm_loadu_ps(b[1]), b2 = _mm_loadu_ps(b[2]);
        __m128 r[3];
        for (int i = 0; i < 3; i++) {
            __m128 t = _mm_mul_ps(ax[i], b0);
            t = _mm_add_ps(t, _mm_mul_ps(ay[i], b1));
            t = _mm_add_ps(t, _mm_mul_ps(az[i], b2));
            r[i] = _mm_or_ps(_mm_andnot_ps(lane3, t), _mm_and_ps(lane3, _mm_add_ps(t, at[i])));
        }
        _mm_storeu_ps(ab[0], r[0]);
        _mm_storeu_ps(ab[1], r[1]);
        _mm_storeu_ps(ab[2], r[2]);
    }
#elif defined(NEBULA_MATH_NEON)
    float32x4_t row[3];
    for (int i = 0; i < 3; i++) {
        row[i] = vld1q_f32(a[i]);
    }
    const uint32x4_t lane3 = vsetq_lane_u32(0xFFFFFFFFu, vdupq_n_u32(0u), 3);
    for (u32 n = 0; n < count; n++, b += 3, ab += 3) {
        const float32x4_t b0 = vld1q_f32(b[0]), b1 = vld1q_f32(b[1]), b2 = vld1q_f32(b[2]);
        float32x4_t r[3];
        for (int i = 0; i < 3; i++) {
            float32x4_t t = vmulq_laneq_f32(b0, row[i], 0);
            t = vaddq_f32(t, vmulq_laneq_f32(b1, row[i], 1));
            t = vaddq_f32(t, vmulq_laneq_f32(b2, row[i], 2));
            r[i] = vbslq_f32(lane3, vaddq_f32(t, vdupq_laneq_f32(row[i], 3)), t);
        }
        vst1q_f32(ab[0], r[0]);
        vst1q_f32(ab[1], r[1]);
        vst1q_f32(ab[2], r[2]);
    }
#else
    for (u32 n = 0; n < count; n++, b += 3, ab += 3) {
        f32 r[3][4];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                r[i][j] = (a[i][0] * b[0][j] + a[i][1] * b[1][j]) + a[i][2] * b[2][j];
            }
            r[i][3] += a[i][3];
        }
        __builtin_memcpy(ab, r, sizeof(r));
    }
#endif
}

/* dst = src * a scale about the origin: the columns scaled, translation kept. */
static inline void NebulaMtxApplyScale(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) {
#if defined(__SSE2__)
    const __m128 s = _mm_set_ps(1.0f, z, y, x);
    const __m128 r0 = _mm_mul_ps(_mm_loadu_ps(src[0]), s), r1 = _mm_mul_ps(_mm_loadu_ps(src[1]), s), r2 = _mm_mul_ps(_mm_loadu_ps(src[2]), s);
    _mm_storeu_ps(dst[0], r0);
    _mm_storeu_ps(dst[1], r1);
    _mm_storeu_ps(dst[2], r2);
#elif defined(NEBULA_MATH_NEON)
    const float scale[4] = {x, y, z, 1.0f};
    const float32x4_t v = vld1q_f32(scale);
    const float32x4_t r0 = vmulq_f32(vld1q_f32(src[0]), v), r1 = vmulq_f32(vld1q_f32(src[1]), v), r2 = vmulq_f32(vld1q_f32(src[2]), v);
    vst1q_f32(dst[0], r0);
    vst1q_f32(dst[1], r1);
    vst1q_f32(dst[2], r2);
#else
    for (int i = 0; i < 3; i++) {
        const f32 r0 = src[i][0] * x, r1 = src[i][1] * y, r2 = src[i][2] * z, r3 = src[i][3];
        dst[i][0] = r0;
        dst[i][1] = r1;
        dst[i][2] = r2;
        dst[i][3] = r3;
    }
#endif
}

/* dst = m * (src, 1). */
static inline void NebulaMtxMultVec(const Mtx m, const Vec* src, Vec* dst) {
    const f32 x = src->x, y = src->y, z = src->z;
    const f32 rx = (m[0][0] * x + m[0][2] * z) + (m[0][1] * y + m[0][3]);
    const f32 ry = (m[1][0] * x + m[1][2] * z) + (m[1][1] * y + m[1][3]);
    const f32 rz = (m[2][0] * x + m[2][2] * z) + (m[2][1] * y + m[2][3]);
    dst->x = rx;
    dst->y = ry;
    dst->z = rz;
}

/* dst = m * (src, 0): rotation and scale only. */
static inline void NebulaMtxMultVecSR(const Mtx m, const Vec* src, Vec* dst) {
    const f32 x = src->x, y = src->y, z = src->z;
    const f32 rx = m[0][2] * z + (m[0][0] * x + m[0][1] * y);
    const f32 ry = m[1][2] * z + (m[1][0] * x + m[1][1] * y);
    const f32 rz = m[2][2] * z + (m[2][0] * x + m[2][1] * y);
    dst->x = rx;
    dst->y = ry;
    dst->z = rz;
}

static inline void NebulaMtxTrans(Mtx m, f32 xT, f32 yT, f32 zT) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = xT;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = yT;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = zT;
}

/* dst = translation(xT, yT, zT) * src. */
static inline void NebulaMtxTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT) {
    const f32 t0 = src[0][3] + xT, t1 = src[1][3] + yT, t2 = src[2][3] + zT;
    if (dst != src) {
        __builtin_memcpy(dst, src, sizeof(Mtx));
    }
    dst[0][3] = t0;
    dst[1][3] = t1;
    dst[2][3] = t2;
}

static inline void NebulaMtxScale(Mtx m, f32 xS, f32 yS, f32 zS) {
    m[0][0] = xS;   m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = yS;   m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = zS;   m[2][3] = 0.0f;
}

/* dst = scale(xS, yS, zS) * src: each row scaled, translation included. */
static inline void NebulaMtxScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS) {
    const f32 s[3] = {xS, yS, zS};
    for (int i = 0; i < 3; i++) {
        const f32 r0 = src[i][0] * s[i], r1 = src[i][1] * s[i], r2 = src[i][2] * s[i], r3 = src[i][3] * s[i];
        dst[i][0] = r0;
        dst[i][1] = r1;
        dst[i][2] = r2;
        dst[i][3] = r3;
    }
}

static inline void NebulaMtx44Identity(Mtx44 m) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            m[i][j] = i == j ? 1.0f : 0.0f;
        }
    }
}

static inline void NebulaMtx44Copy(const Mtx44 src, Mtx44 dst) {
    __builtin_memmove(dst, src, sizeof(Mtx44));
}

static inline void NebulaVecAdd(const Vec* a, const Vec* b, Vec* dst) {
    const f32 x = a->x + b->x, y = a->y + b->y, z = a->z + b->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline void NebulaVecSubtract(const Vec* a, const Vec* b, Vec* dst) {
    const f32 x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline void NebulaVecScale(const Vec* src, Vec* dst, f32 mult) {
    const f32 x = src->x * mult, y = src->y * mult, z = src->z * mult;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline void NebulaVecCopy(const Vec* src, Vec* dst) {
    const f32 x = src->x, y = src->y, z = src->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

/* Each component multiplied. */
static inline void NebulaVecMultiply(const Vec* a, const Vec* b, Vec* dst) {
    const f32 x = a->x * b->x, y = a->y * b->y, z = a->z * b->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline void NebulaVecNegate(const Vec* src, Vec* dst) {
    dst->x = -src->x;
    dst->y = -src->y;
    dst->z = -src->z;
}

static inline f32 NebulaVecSquareMag(const Vec* v) {
    return (v->z * v->z + v->x * v->x) + v->y * v->y;
}

static inline f32 NebulaVecSquareDistance(const Vec* a, const Vec* b) {
    const f32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return (dx * dx + dy * dy) + dz * dz;
}

/* A zero, NaN or infinite vector gives a zero vector (it would otherwise give NaN, which then sticks). */
static inline void NebulaVecNormalize(const Vec* src, Vec* dst) {
    const f32 x = src->x, y = src->y, z = src->z;
    const f32 m = (x * x + z * z) + y * y;
    if (!(m > 0.0f && m <= 3.4028234e38f)) {
        dst->x = 0.0f;
        dst->y = 0.0f;
        dst->z = 0.0f;
        return;
    }
    const f32 r = 1.0f / sqrtf(m);
    dst->x = x * r;
    dst->y = y * r;
    dst->z = z * r;
}

static inline f32 NebulaVecMag(const Vec* v) {
    return __builtin_sqrtf((v->z * v->z + v->x * v->x) + v->y * v->y);
}

static inline f32 NebulaVecMagXYZ(const Vec* v) {
    return __builtin_sqrtf((v->x * v->x) + (v->y * v->y) + (v->z * v->z));
}

static inline f32 NebulaVecDotProduct(const Vec* a, const Vec* b) {
    return (a->x * b->x + a->y * b->y) + a->z * b->z;
}

static inline void NebulaVecCrossProduct(const Vec* a, const Vec* b, Vec* dst) {
    const f32 x = a->y * b->z - b->y * a->z;
    const f32 y = b->x * a->z - a->x * b->z;
    const f32 z = b->y * a->x - a->y * b->x;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static inline f32 NebulaVecDistance(const Vec* a, const Vec* b) {
    const f32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return __builtin_sqrtf((dx * dx + dy * dy) + dz * dz);
}

/* pq = p * q (Hamilton product). */
/* dst = a + (b - a) * t. */
static inline void NebulaVecLerp(const Vec* a, const Vec* b, Vec* dst, f32 t) {
    const f32 x = (b->x - a->x) * t + a->x;
    const f32 y = (b->y - a->y) * t + a->y;
    const f32 z = (b->z - a->z) * t + a->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

/* dst = a * scale + b. */
static inline void NebulaVecScaleAdd(const Vec* a, const Vec* b, Vec* dst, f32 scale) {
    const f32 x = a->x * scale + b->x;
    const f32 y = a->y * scale + b->y;
    const f32 z = a->z * scale + b->z;
    dst->x = x;
    dst->y = y;
    dst->z = z;
}

/* The root of a positive value; zero, negatives and NaN come back as they are. */
static inline f32 NebulaSqrt(f32 value) {
    return value > 0.0f ? __builtin_sqrtf(value) : value;
}

/* Reduces every finite binary32 angle without a C runtime call. The 192-bit
   reciprocal leaves more than 40 guard bits even at the largest exponent. */
static inline f32 NebulaReduceLargeAngle(f32 radians, s32* quadrant) {
    union { f32 f; u32 u; } bits = {radians};
    const u32 exponent = (bits.u >> 23) & 255u;
    if (exponent == 255u) { *quadrant = 0; return 0.0f; }
    const u32 mantissa = (bits.u & 0x7FFFFFu) | 0x800000u;
    static const u32 reciprocal[6] = {0x3C439041u, 0xDB629599u, 0xF534DDC0u,
                                      0xFC2757D1u, 0x4E441529u, 0xA2F9836Eu};
    u32 product[7];
    u64 carry = 0;
    for (u32 i = 0; i < 6; ++i) {
        const u64 value = (u64)reciprocal[i] * mantissa + carry;
        product[i] = (u32)value;
        carry = value >> 32;
    }
    product[6] = (u32)carry;
    const u32 shift = 192u + 23u + 127u - exponent;
    const u32 q0 = (product[shift / 32u] >> (shift % 32u)) & 1u;
    const u32 q1 = (product[(shift + 1u) / 32u] >> ((shift + 1u) % 32u)) & 1u;
    double fraction = 0.0, weight = 0.5;
    for (u32 i = 1; i <= 53; ++i) {
        const u32 at = shift - i;
        fraction += ((product[at / 32u] >> (at % 32u)) & 1u) * weight;
        weight *= 0.5;
    }
    s32 q = (s32)(q0 | q1 << 1);
    if (fraction >= 0.5) { fraction -= 1.0; ++q; }
    if (bits.u >> 31) { fraction = -fraction; q = -q; }
    *quadrant = q;
    return (f32)(fraction * 1.57079632679489661923);
}

/* Sine and cosine in radians. Ordinary rotations use split quarter-turn
   reduction; large rotations use the same polynomial after full range reduction. */
static inline void NebulaSinCos(f32 radians, f32* sine, f32* cosine) {
    s32 n;
    f32 r;
    if (radians > -128.0f && radians < 128.0f) {
        const f32 quarters = radians * 0.63661977f;
        n = (s32)(quarters + (quarters >= 0.0f ? 0.5f : -0.5f));
        r = ((radians - (f32)n * 1.5703125f) - (f32)n * 0.00048375130f) - (f32)n * 7.5497901e-8f;
    } else {
        r = NebulaReduceLargeAngle(radians, &n);
    }
    const f32 r2 = r * r;
    const f32 s = r + r * r2 * (-0.16666654f + r2 * (0.0083321608f + r2 * -0.00019515296f));
    const f32 c = 1.0f + r2 * (-0.5f + r2 * (0.041666418f + r2 * (-0.0013888397f + r2 * 0.000024760495f)));
    switch (n & 3) {
    case 0:
        *sine = s;
        *cosine = c;
        break;
    case 1:
        *sine = c;
        *cosine = -s;
        break;
    case 2:
        *sine = -s;
        *cosine = -c;
        break;
    default:
        *sine = -c;
        *cosine = s;
        break;
    }
}

/* Three independent radian rotations in SIMD lanes; large angles retain the
   scalar full-range reducer. Outputs contain exactly three components. */
static inline void NebulaSinCos3(f32 x, f32 y, f32 z, f32* sine, f32* cosine) {
#if defined(__SSE2__)
    const __m128 a = _mm_setr_ps(x, y, z, 0.0f);
    if (_mm_movemask_ps(_mm_cmplt_ps(_mm_andnot_ps(_mm_set1_ps(-0.0f), a), _mm_set1_ps(128.0f))) == 15) {
        const __m128 q = _mm_mul_ps(a, _mm_set1_ps(0.63661977f));
        const __m128 half = _mm_or_ps(_mm_and_ps(q, _mm_set1_ps(-0.0f)), _mm_set1_ps(0.5f));
        const __m128i n = _mm_cvttps_epi32(_mm_add_ps(q, half));
        const __m128 nf = _mm_cvtepi32_ps(n);
        __m128 r = _mm_sub_ps(a, _mm_mul_ps(nf, _mm_set1_ps(1.5703125f)));
        r = _mm_sub_ps(r, _mm_mul_ps(nf, _mm_set1_ps(0.00048375130f)));
        r = _mm_sub_ps(r, _mm_mul_ps(nf, _mm_set1_ps(7.5497901e-8f)));
        const __m128 r2 = _mm_mul_ps(r, r);
        __m128 sp = _mm_add_ps(_mm_set1_ps(0.0083321608f), _mm_mul_ps(r2, _mm_set1_ps(-0.00019515296f)));
        sp = _mm_add_ps(_mm_set1_ps(-0.16666654f), _mm_mul_ps(r2, sp));
        const __m128 s = _mm_add_ps(r, _mm_mul_ps(_mm_mul_ps(r, r2), sp));
        __m128 cp = _mm_add_ps(_mm_set1_ps(-0.0013888397f), _mm_mul_ps(r2, _mm_set1_ps(0.000024760495f)));
        cp = _mm_add_ps(_mm_set1_ps(0.041666418f), _mm_mul_ps(r2, cp));
        cp = _mm_add_ps(_mm_set1_ps(-0.5f), _mm_mul_ps(r2, cp));
        const __m128 c = _mm_add_ps(_mm_set1_ps(1.0f), _mm_mul_ps(r2, cp));
        const __m128 odd = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(n, _mm_set1_epi32(1)), _mm_set1_epi32(1)));
        const __m128 sv = _mm_or_ps(_mm_and_ps(odd, c), _mm_andnot_ps(odd, s));
        const __m128 cv = _mm_or_ps(_mm_and_ps(odd, s), _mm_andnot_ps(odd, c));
        f32 ss[4], cc[4];
        _mm_storeu_ps(ss, _mm_xor_ps(sv, _mm_castsi128_ps(_mm_slli_epi32(_mm_and_si128(n, _mm_set1_epi32(2)), 30))));
        _mm_storeu_ps(cc, _mm_xor_ps(cv, _mm_castsi128_ps(_mm_slli_epi32(_mm_and_si128(_mm_add_epi32(n, _mm_set1_epi32(1)), _mm_set1_epi32(2)), 30))));
        for (unsigned i = 0; i < 3; ++i) { sine[i] = ss[i]; cosine[i] = cc[i]; }
        return;
    }
#elif defined(NEBULA_MATH_NEON)
    const f32 lanes[4] = {x, y, z, 0.0f};
    const float32x4_t a = vld1q_f32(lanes);
    const uint32x4_t bounded = vcltq_f32(vabsq_f32(a), vdupq_n_f32(128.0f));
    if (vgetq_lane_u32(bounded, 0) && vgetq_lane_u32(bounded, 1) && vgetq_lane_u32(bounded, 2)) {
        const float32x4_t q = vmulq_n_f32(a, 0.63661977f);
        const float32x4_t half = vreinterpretq_f32_u32(vorrq_u32(vandq_u32(vreinterpretq_u32_f32(q), vdupq_n_u32(0x80000000u)), vreinterpretq_u32_f32(vdupq_n_f32(0.5f))));
        const int32x4_t n = vcvtq_s32_f32(vaddq_f32(q, half));
        const float32x4_t nf = vcvtq_f32_s32(n);
        float32x4_t r = vsubq_f32(a, vmulq_n_f32(nf, 1.5703125f));
        r = vsubq_f32(r, vmulq_n_f32(nf, 0.00048375130f));
        r = vsubq_f32(r, vmulq_n_f32(nf, 7.5497901e-8f));
        const float32x4_t r2 = vmulq_f32(r, r);
        float32x4_t sp = vaddq_f32(vdupq_n_f32(0.0083321608f), vmulq_n_f32(r2, -0.00019515296f));
        sp = vaddq_f32(vdupq_n_f32(-0.16666654f), vmulq_f32(r2, sp));
        const float32x4_t s = vaddq_f32(r, vmulq_f32(vmulq_f32(r, r2), sp));
        float32x4_t cp = vaddq_f32(vdupq_n_f32(-0.0013888397f), vmulq_n_f32(r2, 0.000024760495f));
        cp = vaddq_f32(vdupq_n_f32(0.041666418f), vmulq_f32(r2, cp));
        cp = vaddq_f32(vdupq_n_f32(-0.5f), vmulq_f32(r2, cp));
        const float32x4_t c = vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(r2, cp));
        const uint32x4_t un = vreinterpretq_u32_s32(n);
        const uint32x4_t odd = vceqq_u32(vandq_u32(un, vdupq_n_u32(1)), vdupq_n_u32(1));
        const uint32x4_t sv = vreinterpretq_u32_f32(vbslq_f32(odd, c, s));
        const uint32x4_t cv = vreinterpretq_u32_f32(vbslq_f32(odd, s, c));
        f32 ss[4], cc[4];
        vst1q_f32(ss, vreinterpretq_f32_u32(veorq_u32(sv, vshlq_n_u32(vandq_u32(un, vdupq_n_u32(2)), 30))));
        vst1q_f32(cc, vreinterpretq_f32_u32(veorq_u32(cv, vshlq_n_u32(vandq_u32(vaddq_u32(un, vdupq_n_u32(1)), vdupq_n_u32(2)), 30))));
        for (unsigned i = 0; i < 3; ++i) { sine[i] = ss[i]; cosine[i] = cc[i]; }
        return;
    }
#endif
    NebulaSinCos(x, sine, cosine);
    NebulaSinCos(y, sine + 1, cosine + 1);
    NebulaSinCos(z, sine + 2, cosine + 2);
}

static inline f32 NebulaSin(f32 radians) {
    f32 s, c;
    NebulaSinCos(radians, &s, &c);
    return s;
}

static inline f32 NebulaCos(f32 radians) {
    f32 s, c;
    NebulaSinCos(radians, &s, &c);
    return c;
}

/* The angle of (x, y) from the x axis, -pi to pi; 0 for (0, 0). */
static inline f32 NebulaAtan2(f32 y, f32 x) {
    const f32 ax = x < 0.0f ? -x : x;
    const f32 ay = y < 0.0f ? -y : y;
    if (!(ax > 0.0f || ay > 0.0f)) {
        return 0.0f;
    }
    const f32 z = ax >= ay ? ay / ax : ax / ay;
    const f32 z2 = z * z;
    f32 a = z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f + z2 * -0.01172120f)))));
    if (ax < ay) {
        a = 1.5707964f - a;
    }
    if (x < 0.0f) {
        a = 3.1415927f - a;
    }
    return y < 0.0f ? -a : a;
}

static inline f32 NebulaAsin(f32 value) {
    if (!(value > -1.0f)) {
        return value <= -1.0f ? -1.5707964f : 0.0f;
    }
    if (value >= 1.0f) {
        return 1.5707964f;
    }
    return NebulaAtan2(value, __builtin_sqrtf(1.0f - value * value));
}

static inline f32 NebulaAcos(f32 value) {
    if (!(value > -1.0f)) {
        return value <= -1.0f ? 3.1415927f : 0.0f;
    }
    if (value >= 1.0f) {
        return 0.0f;
    }
    return NebulaAtan2(__builtin_sqrtf(1.0f - value * value), value);
}

/* Along the shorter way round: q is flipped when the two lie on opposite sides. */
static inline void NebulaQuatLerp(const Quaternion* p, const Quaternion* q, f32 t, Quaternion* dst) {
    const f32 dot = (p->z * q->z + p->x * q->x) + (p->w * q->w + p->y * q->y);
    const f32 sign = dot < 0.0f ? -1.0f : 1.0f;
    const f32 x = -t * (p->x - sign * q->x) + p->x;
    const f32 y = -t * (p->y - sign * q->y) + p->y;
    const f32 z = -t * (p->z - sign * q->z) + p->z;
    const f32 w = -t * (p->w - sign * q->w) + p->w;
    dst->x = x;
    dst->y = y;
    dst->z = z;
    dst->w = w;
}

static inline void NebulaQuatMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) {
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

static inline f32 NebulaQuatDotProduct(const Quaternion* p, const Quaternion* q) {
    return (p->z * q->z + p->x * q->x) + (p->w * q->w + p->y * q->y);
}


#ifdef __cplusplus
}
#endif

#endif
