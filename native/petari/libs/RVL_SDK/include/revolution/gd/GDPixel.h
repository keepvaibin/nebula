#ifndef GDPIXEL_H
#define GDPIXEL_H

#define TEV_FOG_PARAM_0_ID 0x000000ee
#define TEV_FOG_PARAM_1_ID 0x000000ef
#define TEV_FOG_PARAM_2_ID 0x000000f0
#define TEV_FOG_PARAM_3_ID 0x000000f1
#define TEV_FOG_COLOR_ID 0x000000f2

#define PE_ZMODE_ID 0x00000040
#define PE_CMODE0_ID 0x00000041
#define PE_CMODE1_ID 0x00000042

#define PE_ZMODE_ENABLE_SHIFT 0
#define PE_ZMODE_FUNC_SHIFT 1
#define PE_ZMODE_MASK_SHIFT 4
#define PE_ZMODE_RID_SHIFT 24
#define PE_ZMODE(enable, func, mask, rid)                                                                                                            \
    ((((u32)(enable)) << PE_ZMODE_ENABLE_SHIFT) | (((u32)(func)) << PE_ZMODE_FUNC_SHIFT) |                                       \
     (((u32)(mask)) << PE_ZMODE_MASK_SHIFT) | (((u32)(rid)) << PE_ZMODE_RID_SHIFT))

#define TEV_FOG_PARAM_0_A_MANT_SHIFT 0
#define TEV_FOG_PARAM_0_A_EXPN_SHIFT 11
#define TEV_FOG_PARAM_0_A_SIGN_SHIFT 19
#define TEV_FOG_PARAM_0_RID_SHIFT 24
#define TEV_FOG_PARAM_0(a_mant, a_expn, a_sign, rid)                                                                                                 \
    ((((u32)(a_mant)) << TEV_FOG_PARAM_0_A_MANT_SHIFT) | (((u32)(a_expn)) << TEV_FOG_PARAM_0_A_EXPN_SHIFT) |                     \
     (((u32)(a_sign)) << TEV_FOG_PARAM_0_A_SIGN_SHIFT) | (((u32)(rid)) << TEV_FOG_PARAM_0_RID_SHIFT))

#define TEV_FOG_PARAM_0_PS(a_preshifted, rid) (((u32)(a_preshifted)) | (((u32)(rid)) << TEV_FOG_PARAM_0_RID_SHIFT))

#define PE_CMODE1_CONSTANT_ALPHA_SHIFT 0
#define PE_CMODE1_CONSTANT_ALPHA_ENABLE_SHIFT 8
#define PE_CMODE1_RID_SHIFT 24
#define PE_CMODE1(constant_alpha, constant_alpha_enable, rid)                                                                                        \
    ((((u32)(constant_alpha)) << PE_CMODE1_CONSTANT_ALPHA_SHIFT) |                                                                         \
     (((u32)(constant_alpha_enable)) << PE_CMODE1_CONSTANT_ALPHA_ENABLE_SHIFT) | (((u32)(rid)) << PE_CMODE1_RID_SHIFT))

#define TEV_FOG_PARAM_1_B_MAG_SHIFT 0
#define TEV_FOG_PARAM_1_RID_SHIFT 24
#define TEV_FOG_PARAM_1(b_mag, rid)                                                                                                                  \
    ((((u32)(b_mag)) << TEV_FOG_PARAM_1_B_MAG_SHIFT) | (((u32)(rid)) << TEV_FOG_PARAM_1_RID_SHIFT))

#define TEV_FOG_PARAM_2_B_SHF_SHIFT 0
#define TEV_FOG_PARAM_2_RID_SHIFT 24
#define TEV_FOG_PARAM_2(b_shf, rid)                                                                                                                  \
    ((((u32)(b_shf)) << TEV_FOG_PARAM_2_B_SHF_SHIFT) | (((u32)(rid)) << TEV_FOG_PARAM_2_RID_SHIFT))

#define TEV_FOG_PARAM_3_C_MANT_SHIFT 0
#define TEV_FOG_PARAM_3_C_EXPN_SHIFT 11
#define TEV_FOG_PARAM_3_C_SIGN_SHIFT 19
#define TEV_FOG_PARAM_3_PROJ_SHIFT 20
#define TEV_FOG_PARAM_3_FSEL_SHIFT 21
#define TEV_FOG_PARAM_3_RID_SHIFT 24
#define TEV_FOG_PARAM_3(c_mant, c_expn, c_sign, proj, fsel, rid)                                                                                     \
    ((((u32)(c_mant)) << TEV_FOG_PARAM_3_C_MANT_SHIFT) | (((u32)(c_expn)) << TEV_FOG_PARAM_3_C_EXPN_SHIFT) |                     \
     (((u32)(c_sign)) << TEV_FOG_PARAM_3_C_SIGN_SHIFT) | (((u32)(proj)) << TEV_FOG_PARAM_3_PROJ_SHIFT) |                         \
     (((u32)(fsel)) << TEV_FOG_PARAM_3_FSEL_SHIFT) | (((u32)(rid)) << TEV_FOG_PARAM_3_RID_SHIFT))

#define TEV_FOG_PARAM_3_PS(c_preshifted, proj, fsel, rid)                                                                                            \
    (((u32)(c_preshifted)) | (((u32)(proj)) << TEV_FOG_PARAM_3_PROJ_SHIFT) |                                                     \
     (((u32)(fsel)) << TEV_FOG_PARAM_3_FSEL_SHIFT) | (((u32)(rid)) << TEV_FOG_PARAM_3_RID_SHIFT))

#define TEV_FOG_COLOR_B_SHIFT 0
#define TEV_FOG_COLOR_G_SHIFT 8
#define TEV_FOG_COLOR_R_SHIFT 16
#define TEV_FOG_COLOR_RID_SHIFT 24
#define TEV_FOG_COLOR(b, g, r, rid)                                                                                                                  \
    ((((u32)(b)) << TEV_FOG_COLOR_B_SHIFT) | (((u32)(g)) << TEV_FOG_COLOR_G_SHIFT) |                                             \
     (((u32)(r)) << TEV_FOG_COLOR_R_SHIFT) | (((u32)(rid)) << TEV_FOG_COLOR_RID_SHIFT))

#define PE_CMODE0_BLEND_ENABLE_SHIFT 0
#define PE_CMODE0_LOGICOP_ENABLE_SHIFT 1
#define PE_CMODE0_DITHER_ENABLE_SHIFT 2
#define PE_CMODE0_COLOR_MASK_SHIFT 3
#define PE_CMODE0_ALPHA_MASK_SHIFT 4
#define PE_CMODE0_DFACTOR_SHIFT 5
#define PE_CMODE0_SFACTOR_SHIFT 8
#define PE_CMODE0_BLENDOP_SHIFT 11
#define PE_CMODE0_LOGICOP_SHIFT 12
#define PE_CMODE0_RID_SHIFT 24
#define PE_CMODE0(blend_enable, logicop_enable, dither_enable, color_mask, alpha_mask, dfactor, sfactor, blendop, logicop, rid)                      \
    ((((u32)(blend_enable)) << PE_CMODE0_BLEND_ENABLE_SHIFT) | (((u32)(logicop_enable)) << PE_CMODE0_LOGICOP_ENABLE_SHIFT) |     \
     (((u32)(dither_enable)) << PE_CMODE0_DITHER_ENABLE_SHIFT) | (((u32)(color_mask)) << PE_CMODE0_COLOR_MASK_SHIFT) |           \
     (((u32)(alpha_mask)) << PE_CMODE0_ALPHA_MASK_SHIFT) | (((u32)(dfactor)) << PE_CMODE0_DFACTOR_SHIFT) |                       \
     (((u32)(sfactor)) << PE_CMODE0_SFACTOR_SHIFT) | (((u32)(blendop)) << PE_CMODE0_BLENDOP_SHIFT) |                             \
     (((u32)(logicop)) << PE_CMODE0_LOGICOP_SHIFT) | (((u32)(rid)) << PE_CMODE0_RID_SHIFT))

#define PE_CMODE1_MASK_SETBLENDMODE                                                                                                                  \
    ((0x000001 << PE_CMODE0_BLEND_ENABLE_SHIFT) | (0x000001 << PE_CMODE0_LOGICOP_ENABLE_SHIFT) | (0x000007 << PE_CMODE0_DFACTOR_SHIFT) |             \
     (0x000007 << PE_CMODE0_SFACTOR_SHIFT) | (0x000001 << PE_CMODE0_BLENDOP_SHIFT) | (0x00000F << PE_CMODE0_LOGICOP_SHIFT))

#define BP_FOG_UNK0(a, id) ((u32)(a) << 0 | (u32)(id) << 24)

#define BP_FOG_UNK1(b_m, id) ((u32)(b_m) << 0 | (u32)(id) << 24)

#define BP_FOG_UNK2(b_expn, id) ((u32)(b_expn) << 0 | (u32)(id) << 24)

#define BP_FOG_UNK3(c, proj, fsel, id) ((u32)(c) << 0 | (u32)(proj) << 20 | (u32)(fsel) << 21 | (u32)(id) << 24)

#define BP_FOG_COLOR(r, g, b, id) ((u32)(b) << 0 | (u32)(g) << 8 | (u32)(r) << 16 | (u32)(id) << 24)

#define BP_BLEND_MODE(enable, enable_logic, enable_dither, enable_color_update, enable_alpha_update, dst_factor, src_factor, blend_sub, logic_op,    \
                      id)                                                                                                                            \
    ((u32)(enable) << 0 | (u32)(enable_logic) << 1 | (u32)(enable_dither) << 2 | (u32)(enable_color_update) << 3 | (u32)(enable_alpha_update) << 4 | \
     (u32)(dst_factor) << 5 | (u32)(src_factor) << 8 | (u32)(blend_sub) << 11 | (u32)(logic_op) << 12 | (u32)(id) << 24)

#define BP_Z_MODE(enable_compare, compare_fn, enable_update, id)                                                                                     \
    ((u32)(enable_compare) << 0 | (u32)(compare_fn) << 1 | (u32)(enable_update) << 4 | (u32)(id) << 24)

#define BP_DST_ALPHA(alpha, enable, id) ((u32)(alpha) << 0 | (u32)(enable) << 8 | (u32)(id) << 24)

#define BP_TOKEN(token, id) ((u32)(token) << 0 | (u32)(id) << 24)

#endif  // GDPIXEL_H
