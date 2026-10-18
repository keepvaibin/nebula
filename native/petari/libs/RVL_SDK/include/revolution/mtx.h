#ifndef MTX_H
#define MTX_H

#include "revolution/types.h"

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
typedef f32 (*Mtx23P)[3];

typedef f32 Mtx[3][4];
typedef f32 (*MtxPtr)[4];

typedef f32 Mtx44[4][4];
typedef f32 (*Mtx44Ptr)[4];

typedef f32 Mtx33[3][3];
typedef f32 (*Mtx3P)[3];

#define MTXDegToRad(a) ((a)*0.01745329252f)
#define MTXRadToDeg(a) ((a)*57.29577951f)

#define MTXRowCol(m, r, c) ((m)[(r)][(c)])

/* C Matrix */
void C_MTXIdentity(Mtx);
void C_MTXCopy(const Mtx, Mtx);
void C_MTXOrtho(Mtx44, f32, f32, f32, f32, f32, f32);
void C_MTXPerspective(Mtx44, f32, f32, f32, f32);

/* Paired Single Matrix */
#ifndef TARGET_PC
void PSMTXIdentity(Mtx);
#endif
#ifndef TARGET_PC
void PSMTXCopy(const Mtx, Mtx);
#endif
#ifndef TARGET_PC
void PSMTXConcat(const Mtx, const Mtx, Mtx);
#endif
#ifndef TARGET_PC
void PSMTXMultVecSR(const Mtx, const Vec*, Vec*);
#endif
void PSMTXMultVecArraySR(const Mtx, const Vec*, Vec*, u32);

u32 PSMTXInverse(const Mtx, Mtx);
u32 PSMTXInvXpose(const Mtx, Mtx);
void C_MTXLookAt(Mtx, const Point3d*, const Vec*, const Point3d*);

#ifndef TARGET_PC
void PSMTXMultVec(const Mtx, const Vec*, Vec*);
#endif

#ifndef TARGET_PC
void PSMTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT);
#endif
#ifndef TARGET_PC
void PSMTXScaleApply(const Mtx, Mtx, f32, f32, f32);
#endif
#ifndef TARGET_PC
void PSMTXScale(Mtx, f32, f32, f32);
#endif
void PSMTXRotRad(Mtx m, char axis, f32 rad) ATTRIBUTE_WEAK;
void PSMTXRotAxisRad(Mtx dst, const Vec*, f32);
void PSMTXRotTrig(Mtx, char, f32, f32);

#ifndef TARGET_PC
void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT);
#endif
#ifndef TARGET_PC
void PSMTX44Identity(Mtx44 m);
#endif
#ifndef TARGET_PC
void PSMTX44Copy(const Mtx44 m, Mtx44 dst);
#endif

/* Paired Single Quaternion */
#ifndef TARGET_PC
void PSQUATMultiply(const Quaternion*, const Quaternion*, Quaternion*);
#endif
void PSMTXQuat(Mtx dst, const Quaternion* quat);
#ifndef TARGET_PC
f32 PSQUATDotProduct(const Quaternion*, const Quaternion*);
#endif

/* C Quaternion */
void C_QUATMtx(Quaternion* r, const Mtx m);
void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t);

/* C Vector */

#ifndef TARGET_PC
f32 C_VECMag(const Vec*);
#endif

/* Paired Single Vector */
#ifndef TARGET_PC
void PSVECAdd(const Vec*, const Vec*, Vec*);
#endif
#ifndef TARGET_PC
void PSVECSubtract(const Vec*, const Vec*, Vec*);
#endif
#ifndef TARGET_PC
void PSVECScale(const Vec*, Vec*, f32);
#endif
#ifndef TARGET_PC
void PSVECNormalize(const Vec*, Vec*);
#endif

#ifndef TARGET_PC
f32 PSVECMag(const Vec*);
#endif
#ifndef TARGET_PC
f32 PSVECDotProduct(const Vec*, const Vec*);
#endif
#ifndef TARGET_PC
void PSVECCrossProduct(const Vec*, const Vec*, Vec*);
#endif
#ifndef TARGET_PC
f32 PSVECDistance(const Vec*, const Vec*);
#endif

void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT);
void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT);

#ifdef TARGET_PC
// The small functions inline, the console's paired-single code natively (nebula_mtx.h).
#include <nebula_mtx.h>
#endif

/* SMG1 uses the paired single versions of matrix / vector operations */
#define VECNormalize PSVECNormalize
#define VECMag PSVECMag

#ifdef __cplusplus
}
#endif

#endif  // MTX_H
