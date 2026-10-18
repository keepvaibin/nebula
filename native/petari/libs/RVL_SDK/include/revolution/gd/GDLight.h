#ifndef GDLIGHT_H
#define GDLIGHT_H

#include "revolution/gx/GXEnum.h"
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XF_AMBIENT0_ID 0x100a
#define XF_MATERIAL0_ID 0x100c
#define XF_COLOR0CNTRL_ID 0x100e

#define XF_COLOR0CNTRL_MATERIAL_SRC_SHIFT 0
#define XF_COLOR0CNTRL_LIGHTFUNC_SHIFT 1
#define XF_COLOR0CNTRL_LIGHT0_SHIFT 2
#define XF_COLOR0CNTRL_LIGHT1_SHIFT 3
#define XF_COLOR0CNTRL_LIGHT2_SHIFT 4
#define XF_COLOR0CNTRL_LIGHT3_SHIFT 5
#define XF_COLOR0CNTRL_AMBIENT_SRC_SHIFT 6
#define XF_COLOR0CNTRL_DIFFUSEATTEN_SHIFT 7
#define XF_COLOR0CNTRL_ATTENENABLE_SHIFT 9
#define XF_COLOR0CNTRL_ATTENSELECT_SHIFT 10
#define XF_COLOR0CNTRL_LIGHT4_SHIFT 11
#define XF_COLOR0CNTRL_LIGHT5_SHIFT 12
#define XF_COLOR0CNTRL_LIGHT6_SHIFT 13
#define XF_COLOR0CNTRL_LIGHT7_SHIFT 14
#define XF_COLOR0CNTRL_F(material_src, lightfunc, light0, light1, light2, light3, ambient_src, diffuseatten, attenenable, attenselect, light4,       \
                         light5, light6, light7)                                                                                                     \
    ((((u32)(material_src)) << XF_COLOR0CNTRL_MATERIAL_SRC_SHIFT) | (((u32)(lightfunc)) << XF_COLOR0CNTRL_LIGHTFUNC_SHIFT) |     \
     (((u32)(light0)) << XF_COLOR0CNTRL_LIGHT0_SHIFT) | (((u32)(light1)) << XF_COLOR0CNTRL_LIGHT1_SHIFT) |                       \
     (((u32)(light2)) << XF_COLOR0CNTRL_LIGHT2_SHIFT) | (((u32)(light3)) << XF_COLOR0CNTRL_LIGHT3_SHIFT) |                       \
     (((u32)(ambient_src)) << XF_COLOR0CNTRL_AMBIENT_SRC_SHIFT) | (((u32)(diffuseatten)) << XF_COLOR0CNTRL_DIFFUSEATTEN_SHIFT) | \
     (((u32)(attenenable)) << XF_COLOR0CNTRL_ATTENENABLE_SHIFT) | (((u32)(attenselect)) << XF_COLOR0CNTRL_ATTENSELECT_SHIFT) |   \
     (((u32)(light4)) << XF_COLOR0CNTRL_LIGHT4_SHIFT) | (((u32)(light5)) << XF_COLOR0CNTRL_LIGHT5_SHIFT) |                       \
     (((u32)(light6)) << XF_COLOR0CNTRL_LIGHT6_SHIFT) | (((u32)(light7)) << XF_COLOR0CNTRL_LIGHT7_SHIFT))

#define XF_COLOR0CNTRL_F_PS(material_src, lightfunc, light3210, ambient_src, diffuseatten, attenenable, attenselect, light7654)                      \
    ((((u32)(material_src)) << XF_COLOR0CNTRL_MATERIAL_SRC_SHIFT) | (((u32)(lightfunc)) << XF_COLOR0CNTRL_LIGHTFUNC_SHIFT) |     \
     (((u32)(light3210)) << XF_COLOR0CNTRL_LIGHT0_SHIFT) | (((u32)(ambient_src)) << XF_COLOR0CNTRL_AMBIENT_SRC_SHIFT) |          \
     (((u32)(diffuseatten)) << XF_COLOR0CNTRL_DIFFUSEATTEN_SHIFT) | (((u32)(attenenable)) << XF_COLOR0CNTRL_ATTENENABLE_SHIFT) | \
     (((u32)(attenselect)) << XF_COLOR0CNTRL_ATTENSELECT_SHIFT) | (((u32)(light7654)) << XF_COLOR0CNTRL_LIGHT4_SHIFT))

inline static u16 __GDLightID2Index(GXLightID id) {
    u16 idx;

    idx = 0x1F - __cntlzw(id);
    if (idx > 7) {
        idx = 0;
    }
    return idx;
}

static inline u16 __GDLightID2Offset(GXLightID id) {
    return __GDLightID2Index(id) * 16;
}

#ifdef __cplusplus
}
#endif

#endif  // GDLIGHT_H
