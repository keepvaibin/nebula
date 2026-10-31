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

typedef f32 Mtx[3][4];
typedef f32 (*MtxPtr)[4];

typedef f32 Mtx44[4][4];
typedef f32 (*Mtx44Ptr)[4];

typedef f32 Mtx33[3][3];
typedef f32 (*Mtx3P)[3];

/* C Matrix */

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

u32 PSMTXInverse(const Mtx, Mtx);
u32 PSMTXInvXpose(const Mtx, Mtx);

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

#ifdef TARGET_PC
// The small functions inline, the console's paired-single code natively (nebula_mtx.h).
#include <nebula_mtx.h>
#endif

/* SMG1 uses the paired single versions of matrix / vector operations */

#ifdef __cplusplus
}
#endif

#endif  // MTX_H
