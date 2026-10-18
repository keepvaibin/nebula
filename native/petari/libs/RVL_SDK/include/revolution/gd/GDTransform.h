#ifndef GDTRANSFORM_H
#define GDTRANSFORM_H

#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP_MATINDEX_A 0x30
#define CP_MATINDEX_B 0x40

#define XF_MATINDEX_A 0x1018

#define MATIDX_REG_A_POSIDX_SHIFT 0
#define MATIDX_REG_A_TEX0IDX_SHIFT 6
#define MATIDX_REG_A_TEX1IDX_SHIFT 12
#define MATIDX_REG_A_TEX2IDX_SHIFT 18
#define MATIDX_REG_A_TEX3IDX_SHIFT 24
#define MATIDX_REG_A(posIdx, tex0Idx, tex1Idx, tex2Idx, tex3Idx)                                                                                     \
    ((((u32)(posIdx)) << MATIDX_REG_A_POSIDX_SHIFT) | (((u32)(tex0Idx)) << MATIDX_REG_A_TEX0IDX_SHIFT) |                         \
     (((u32)(tex1Idx)) << MATIDX_REG_A_TEX1IDX_SHIFT) | (((u32)(tex2Idx)) << MATIDX_REG_A_TEX2IDX_SHIFT) |                       \
     (((u32)(tex3Idx)) << MATIDX_REG_A_TEX3IDX_SHIFT))

#define MATIDX_REG_B_TEX4IDX_SHIFT 0
#define MATIDX_REG_B_TEX5IDX_SHIFT 6
#define MATIDX_REG_B_TEX6IDX_SHIFT 12
#define MATIDX_REG_B_TEX7IDX_SHIFT 18
#define MATIDX_REG_B(tex4Idx, tex5Idx, tex6Idx, tex7Idx)                                                                                             \
    ((((u32)(tex4Idx)) << MATIDX_REG_B_TEX4IDX_SHIFT) | (((u32)(tex5Idx)) << MATIDX_REG_B_TEX5IDX_SHIFT) |                       \
     (((u32)(tex6Idx)) << MATIDX_REG_B_TEX6IDX_SHIFT) | (((u32)(tex7Idx)) << MATIDX_REG_B_TEX7IDX_SHIFT))

void GDSetCurrentMtx(u32 pn, u32 t0, u32 t1, u32 t2, u32 t3, u32 t4, u32 t5, u32 t6, u32 t7);

#ifdef __cplusplus
}
#endif

#endif  // GDTRANSFORM_H
