/* The RVL SDK's matrix, vector and quaternion names for the SDK's own sources still in the module: the native math
   library's functions (nebula_math.h) under the console's names. The game calls the library itself. */
#ifndef NEBULA_MTX_H
#define NEBULA_MTX_H

#include <nebula_math.h>

static inline void PSMTXIdentity(Mtx m) {
    NebulaMtxIdentity(m);
}

static inline void PSMTXCopy(const Mtx src, Mtx dst) {
    NebulaMtxCopy(src, dst);
}

static inline void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
    NebulaMtxConcat(a, b, ab);
}

static inline void PSMTXMultVec(const Mtx m, const Vec* src, Vec* dst) {
    NebulaMtxMultVec(m, src, dst);
}

static inline void PSMTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) {
    NebulaMtxMultVecSR(m, src, dst);
}

static inline void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT) {
    NebulaMtxTrans(m, xT, yT, zT);
}

static inline void PSMTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT) {
    NebulaMtxTransApply(src, dst, xT, yT, zT);
}

static inline void PSMTXScale(Mtx m, f32 xS, f32 yS, f32 zS) {
    NebulaMtxScale(m, xS, yS, zS);
}

static inline void PSMTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS) {
    NebulaMtxScaleApply(src, dst, xS, yS, zS);
}

static inline void PSMTX44Identity(Mtx44 m) {
    NebulaMtx44Identity(m);
}

static inline void PSMTX44Copy(const Mtx44 src, Mtx44 dst) {
    NebulaMtx44Copy(src, dst);
}

static inline void PSVECAdd(const Vec* a, const Vec* b, Vec* dst) {
    NebulaVecAdd(a, b, dst);
}

static inline void PSVECSubtract(const Vec* a, const Vec* b, Vec* dst) {
    NebulaVecSubtract(a, b, dst);
}

static inline void PSVECScale(const Vec* src, Vec* dst, f32 mult) {
    NebulaVecScale(src, dst, mult);
}

static inline void PSVECNormalize(const Vec* src, Vec* dst) {
    NebulaVecNormalize(src, dst);
}

static inline f32 PSVECMag(const Vec* v) {
    return NebulaVecMag(v);
}

static inline f32 C_VECMag(const Vec* v) {
    return NebulaVecMagXYZ(v);
}

static inline f32 PSVECDotProduct(const Vec* a, const Vec* b) {
    return NebulaVecDotProduct(a, b);
}

static inline void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* dst) {
    NebulaVecCrossProduct(a, b, dst);
}

static inline f32 PSVECDistance(const Vec* a, const Vec* b) {
    return NebulaVecDistance(a, b);
}

static inline void PSQUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) {
    NebulaQuatMultiply(p, q, pq);
}

static inline f32 PSQUATDotProduct(const Quaternion* p, const Quaternion* q) {
    return NebulaQuatDotProduct(p, q);
}

#endif
