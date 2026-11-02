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

u32 PSMTXInverse(const Mtx, Mtx);
u32 PSMTXInvXpose(const Mtx, Mtx);


void PSMTXRotAxisRad(Mtx dst, const Vec*, f32);
void PSMTXRotTrig(Mtx, char, f32, f32);


/* Paired Single Quaternion */
void PSMTXQuat(Mtx dst, const Quaternion* quat);

/* C Quaternion */
void C_QUATMtx(Quaternion* r, const Mtx m);

/* C Vector */


/* Paired Single Vector */


// The small functions inline, the console's paired-single code natively (nebula_mtx.h).
#include <nebula_mtx.h>

/* SMG1 uses the paired single versions of matrix / vector operations */

#ifdef __cplusplus
}
#endif

#endif  // MTX_H
