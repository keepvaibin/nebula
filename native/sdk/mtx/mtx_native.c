/* The RVL SDK's larger matrix and quaternion functions for the SDK's own sources still in the module: the native math
   library's (nebula_math.c) under the console's names. */
#include <revolution/mtx.h>

u32 PSMTXInverse(const Mtx src, Mtx inv) {
    return NebulaMtxInverse(src, inv);
}

u32 PSMTXInvXpose(const Mtx src, Mtx invX) {
    return NebulaMtxInvXpose(src, invX);
}

void PSMTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA) {
    NebulaMtxRotTrig(m, axis, sinA, cosA);
}

void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) {
    NebulaMtxRotAxisRad(m, axis, rad);
}

void PSMTXQuat(Mtx m, const Quaternion* q) {
    NebulaMtxQuat(m, q);
}

void PSMTXMultVecArraySR(const Mtx m, const Vec* src, Vec* dst, u32 count) {
    NebulaMtxMultVecArraySR(m, src, dst, count);
}

void C_MTXLookAt(Mtx m, const Point3d* camPos, const Vec* camUp, const Point3d* target) {
    NebulaMtxLookAt(m, camPos, camUp, target);
}

void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    NebulaMtxLightPerspective(m, fovY, aspect, scaleS, scaleT, transS, transT);
}

void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
    NebulaMtxLightOrtho(m, t, b, l, r, scaleS, scaleT, transS, transT);
}

void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
    NebulaMtxPerspective(m, fovY, aspect, n, f);
}

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
    NebulaMtxOrtho(m, t, b, l, r, n, f);
}

void C_QUATMtx(Quaternion* r, const Mtx m) {
    NebulaQuatFromMtx(r, m);
}

void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
    NebulaQuatSlerp(p, q, r, t);
}
