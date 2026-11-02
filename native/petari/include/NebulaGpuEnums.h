#ifndef GpuENUM_H
#define GpuENUM_H

#ifdef __cplusplus
extern "C" {
#endif


typedef u8 GpuBool;

#define GPU_TRUE ((GpuBool)1)
#define GPU_FALSE ((GpuBool)0)
#define GPU_ENABLE ((GpuBool)1)
#define GPU_DISABLE ((GpuBool)0)

typedef enum _GpuAttr {
  GPU_VA_PNMTXIDX = 0,
  GPU_VA_TEX0MTXIDX,
  GPU_VA_TEX1MTXIDX,
  GPU_VA_TEX2MTXIDX,
  GPU_VA_TEX3MTXIDX,
  GPU_VA_TEX4MTXIDX,
  GPU_VA_TEX5MTXIDX,
  GPU_VA_TEX6MTXIDX,
  GPU_VA_TEX7MTXIDX,
  GPU_VA_POS = 9,
  GPU_VA_NRM,
  GPU_VA_CLR0,
  GPU_VA_CLR1,
  GPU_VA_TEX0,
  GPU_VA_TEX1,
  GPU_VA_TEX2,
  GPU_VA_TEX3,
  GPU_VA_TEX4,
  GPU_VA_TEX5,
  GPU_VA_TEX6,
  GPU_VA_TEX7,
  GPU_POS_MTX_ARRAY,
  GPU_NRM_MTX_ARRAY,
  GPU_TEX_MTX_ARRAY,
  GPU_LIGHT_ARRAY,
  GPU_VA_NBT,
  GPU_VA_MAX_ATTR,
  GPU_VA_NULL = 0xff
} GpuAttr;

/*
typedef enum _GpuCopyClamp {
    GPU_CLAMP_NONE,
    GPU_CLAMP_TOP,
    GPU_CLAMP_BOTTOM,
    GPU_CLAMP_ALL,
} GpuCopyClamp;
*/

typedef enum _GpuAttrType {
  GPU_NONE = 0,
  GPU_DIRECT,
  GPU_INDEX8,
  GPU_INDEX16

} GpuAttrType;

typedef enum _GpuTexCoordID {
  GPU_TEXCOORD0 = 0x0,
  GPU_TEXCOORD1,
  GPU_TEXCOORD2,
  GPU_TEXCOORD3,
  GPU_TEXCOORD4,
  GPU_TEXCOORD5,
  GPU_TEXCOORD6,
  GPU_TEXCOORD7,
  GPU_MAX_TEXCOORD = 8,
  GPU_TEXCOORD_NULL = 0xff
} GpuTexCoordID;

typedef enum _GpuTexGenType {
  GPU_TG_MTX3x4 = 0,
  GPU_TG_MTX2x4,
  GPU_TG_BUMP0,
  GPU_TG_BUMP1,
  GPU_TG_BUMP2,
  GPU_TG_BUMP3,
  GPU_TG_BUMP4,
  GPU_TG_BUMP5,
  GPU_TG_BUMP6,
  GPU_TG_BUMP7,
  GPU_TG_SRTG
} GpuTexGenType;

typedef enum _GpuTexGenSrc {
  GPU_TG_POS = 0,
  GPU_TG_NRM,
  GPU_TG_BINRM,
  GPU_TG_TANGENT,
  GPU_TG_TEX0,
  GPU_TG_TEX1,
  GPU_TG_TEX2,
  GPU_TG_TEX3,
  GPU_TG_TEX4,
  GPU_TG_TEX5,
  GPU_TG_TEX6,
  GPU_TG_TEX7,
  GPU_TG_TEXCOORD0,
  GPU_TG_TEXCOORD1,
  GPU_TG_TEXCOORD2,
  GPU_TG_TEXCOORD3,
  GPU_TG_TEXCOORD4,
  GPU_TG_TEXCOORD5,
  GPU_TG_TEXCOORD6,
  GPU_TG_COLOR0,
  GPU_TG_COLOR1
} GpuTexGenSrc;

typedef enum _GpuCompType {
  GPU_U8 = 0,
  GPU_S8 = 1,
  GPU_U16 = 2,
  GPU_S16 = 3,
  GPU_F32 = 4,

  GPU_RGB565 = 0,
  GPU_RGB8 = 1,
  GPU_RGBX8 = 2,
  GPU_RGBA4 = 3,
  GPU_RGBA6 = 4,
  GPU_RGBA8 = 5
} GpuCompType;

typedef enum _GpuChannelID {
  GPU_COLOR0,
  GPU_COLOR1,
  GPU_ALPHA0,
  GPU_ALPHA1,
  GPU_COLOR0A0,
  GPU_COLOR1A1,
  GPU_COLOR_ZERO,
  GPU_ALPHA_BUMP,
  GPU_ALPHA_BUMPN,
  GPU_COLOR_NULL = 0xff
} GpuChannelID;

typedef enum _GpuColorSrc { GPU_SRC_REG = 0, GPU_SRC_VTX } GpuColorSrc;


typedef enum _GpuTexMapID {
  GPU_TEXMAP0,
  GPU_TEXMAP1,
  GPU_TEXMAP2,
  GPU_TEXMAP3,
  GPU_TEXMAP4,
  GPU_TEXMAP5,
  GPU_TEXMAP6,
  GPU_TEXMAP7,
  GPU_MAX_TEXMAP,
  GPU_TEXMAP_NULL = 0xff,
  GPU_TEX_DISABLE = 0x100
} GpuTexMapID;

typedef enum _GpuIndTexMtxID {
  GPU_ITM_OFF,
  GPU_ITM_0,
  GPU_ITM_1,
  GPU_ITM_2,
  GPU_ITM_S0 = 5,
  GPU_ITM_S1,
  GPU_ITM_S2,
  GPU_ITM_T0 = 9,
  GPU_ITM_T1,
  GPU_ITM_T2
} GpuIndTexMtxID;

/********************************/
typedef enum _GpuTexFmt {
#define _GX_TF_CTF 0x20 /* copy-texture-format only */
#define _GX_TF_ZTF 0x10 /* Z-texture-format */

  GPU_TF_I4 = 0x0,
  GPU_TF_I8 = 0x1,
  GPU_TF_IA4 = 0x2,
  GPU_TF_IA8 = 0x3,
  GPU_TF_RGB565 = 0x4,
  GPU_TF_RGB5A3 = 0x5,
  GPU_TF_RGBA8 = 0x6,
  GPU_TF_CMPR = 0xE,

  GPU_CTF_R4 = 0x0 | _GX_TF_CTF,
  GPU_CTF_RA4 = 0x2 | _GX_TF_CTF,
  GPU_CTF_RA8 = 0x3 | _GX_TF_CTF,
  GPU_CTF_YUVA8 = 0x6 | _GX_TF_CTF,
  GPU_CTF_A8 = 0x7 | _GX_TF_CTF,
  GPU_CTF_R8 = 0x8 | _GX_TF_CTF,
  GPU_CTF_G8 = 0x9 | _GX_TF_CTF,
  GPU_CTF_B8 = 0xA | _GX_TF_CTF,
  GPU_CTF_RG8 = 0xB | _GX_TF_CTF,
  GPU_CTF_GB8 = 0xC | _GX_TF_CTF,

  GPU_TF_Z8 = 0x1 | _GX_TF_ZTF,
  GPU_TF_Z16 = 0x3 | _GX_TF_ZTF,
  GPU_TF_Z24X8 = 0x6 | _GX_TF_ZTF,

  GPU_CTF_Z4 = 0x0 | _GX_TF_ZTF | _GX_TF_CTF,
  GPU_CTF_Z8M = 0x9 | _GX_TF_ZTF | _GX_TF_CTF,
  GPU_CTF_Z8L = 0xA | _GX_TF_ZTF | _GX_TF_CTF,
  GPU_CTF_Z16L = 0xC | _GX_TF_ZTF | _GX_TF_CTF,

  GPU_TF_A8 = GPU_CTF_A8
} GpuTexFmt;

typedef enum _GpuCullMode {
  GPU_CULL_NONE,
  GPU_CULL_FRONT,
  GPU_CULL_BACK,
  GPU_CULL_ALL
} GpuCullMode;

typedef enum _GpuTexWrapMode {
  GPU_CLAMP,
  GPU_REPEAT,
  GPU_MIRROR,
  GPU_MAX_TEXWRAPMODE
} GpuTexWrapMode;

typedef enum _GpuTexFilter {
  GPU_NEAR,
  GPU_LINEAR,
  GPU_NEAR_MIP_NEAR,
  GPU_LIN_MIP_NEAR,
  GPU_NEAR_MIP_LIN,
  GPU_LIN_MIP_LIN
} GpuTexFilter;

typedef enum _GpuLightID {
  GPU_LIGHT0 = 0x001,
  GPU_LIGHT1 = 0x002,
  GPU_LIGHT2 = 0x004,
  GPU_LIGHT3 = 0x008,
  GPU_LIGHT4 = 0x010,
  GPU_LIGHT5 = 0x020,
  GPU_LIGHT6 = 0x040,
  GPU_LIGHT7 = 0x080,
  GPU_MAX_LIGHT = 0x100,
  GPU_LIGHT_NULL = 0x000
} GpuLightID;

typedef enum _GpuDiffuseFn {
  GPU_DF_NONE = 0,
  GPU_DF_SIGN,
  GPU_DF_CLAMP
} GpuDiffuseFn;

typedef enum _GpuAttnFn { GPU_AF_SPEC = 0, GPU_AF_SPOT = 1, GPU_AF_NONE } GpuAttnFn;

typedef enum _GpuCompCnt {
  GPU_POS_XY = 0,
  GPU_POS_XYZ = 1,
  GPU_NRM_XYZ = 0,
  GPU_NRM_NBT = 1,  // one index per NBT
  GPU_NRM_NBT3 = 2, // one index per each of N/B/T
  GPU_CLR_RGB = 0,
  GPU_CLR_RGBA = 1,
  GPU_TEX_S = 0,
  GPU_TEX_ST = 1
} GpuCompCnt;

typedef enum _GpuTevRegID {
  GPU_TEVPREV = 0,
  GPU_TEVREG0,
  GPU_TEVREG1,
  GPU_TEVREG2,
  GPU_MAX_TEVREG
} GpuTevRegID;

typedef enum _GpuTevOp {
  GPU_TEV_ADD = 0,
  GPU_TEV_SUB = 1,

  GPU_TEV_COMP_R8_GT = 8,
  GPU_TEV_COMP_R8_EQ = 9,
  GPU_TEV_COMP_GR16_GT = 10,
  GPU_TEV_COMP_GR16_EQ = 11,
  GPU_TEV_COMP_BGR24_GT = 12,
  GPU_TEV_COMP_BGR24_EQ = 13,
  GPU_TEV_COMP_RGB8_GT = 14,
  GPU_TEV_COMP_RGB8_EQ = 15,

  GPU_TEV_COMP_A8_GT = GPU_TEV_COMP_RGB8_GT, // for alpha channel
  GPU_TEV_COMP_A8_EQ = GPU_TEV_COMP_RGB8_EQ  // for alpha channel
} GpuTevOp;

typedef enum _GpuTevColorArg {
  GPU_CC_CPREV,
  GPU_CC_APREV,
  GPU_CC_C0,
  GPU_CC_A0,
  GPU_CC_C1,
  GPU_CC_A1,
  GPU_CC_C2,
  GPU_CC_A2,
  GPU_CC_TEXC,
  GPU_CC_TEXA,
  GPU_CC_RASC,
  GPU_CC_RASA,
  GPU_CC_ONE,
  GPU_CC_HALF,
  GPU_CC_KONST,
  GPU_CC_ZERO
} GpuTevColorArg;

typedef enum _GpuTevAlphaArg {
  GPU_CA_APREV,
  GPU_CA_A0,
  GPU_CA_A1,
  GPU_CA_A2,
  GPU_CA_TEXA,
  GPU_CA_RASA,
  GPU_CA_KONST,
  GPU_CA_ZERO
} GpuTevAlphaArg;

typedef enum _GpuTevKColorID {
  GPU_KCOLOR0 = 0,
  GPU_KCOLOR1,
  GPU_KCOLOR2,
  GPU_KCOLOR3,
  GPU_MAX_KCOLOR
} GpuTevKColorID;

typedef enum _GpuAlphaOp {
  GPU_AOP_AND,
  GPU_AOP_OR,
  GPU_AOP_XOR,
  GPU_AOP_XNOR,
  GPU_MAX_ALPHAOP
} GpuAlphaOp;

typedef enum _GpuFogType {
  GPU_FOG_NONE = 0x00,
  GPU_FOG_PERSP_LIN = 0x02,
  GPU_FOG_PERSP_EXP = 0x04,
  GPU_FOG_PERSP_EXP2 = 0x05,
  GPU_FOG_PERSP_REVEXP = 0x06,
  GPU_FOG_PERSP_REVEXP2 = 0x07,
  GPU_FOG_ORTHO_LIN = 0x0A,
  GPU_FOG_ORTHO_EXP = 0x0C,
  GPU_FOG_ORTHO_EXP2 = 0x0D,
  GPU_FOG_ORTHO_REVEXP = 0x0E,
  GPU_FOG_ORTHO_REVEXP2 = 0x0F,
  GPU_FOG_LIN = GPU_FOG_PERSP_LIN,
  GPU_FOG_EXP = GPU_FOG_PERSP_EXP,
  GPU_FOG_EXP2 = GPU_FOG_PERSP_EXP2,
  GPU_FOG_REVEXP = GPU_FOG_PERSP_REVEXP,
  GPU_FOG_REVEXP2 = GPU_FOG_PERSP_REVEXP2
} GpuFogType;

typedef enum _GpuBlendMode {
  GPU_BM_NONE,
  GPU_BM_BLEND,
  GPU_BM_LOGIC,
  GPU_BM_SUBTRACT,
  GPU_MAX_BLENDMODE
} GpuBlendMode;

typedef enum _GpuBlendFactor {
  GPU_BL_ZERO,
  GPU_BL_ONE,
  GPU_BL_SRCCLR,
  GPU_BL_INVSRCCLR,
  GPU_BL_SRCALPHA,
  GPU_BL_INVSRCALPHA,
  GPU_BL_DSTALPHA,
  GPU_BL_INVDSTALPHA,

  GPU_BL_DSTCLR = GPU_BL_SRCCLR,
  GPU_BL_INVDSTCLR = GPU_BL_INVSRCCLR
} GpuBlendFactor;

typedef enum _GpuCompare {
  GPU_NEVER,
  GPU_LESS,
  GPU_EQUAL,
  GPU_LEQUAL,
  GPU_GREATER,
  GPU_NEQUAL,
  GPU_GEQUAL,
  GPU_ALWAYS
} GpuCompare;

typedef enum _GpuLogicOp {
  GPU_LO_CLEAR,
  GPU_LO_AND,
  GPU_LO_REVAND,
  GPU_LO_COPY,
  GPU_LO_INVAND,
  GPU_LO_NOOP,
  GPU_LO_XOR,
  GPU_LO_OR,
  GPU_LO_NOR,
  GPU_LO_EQUIV,
  GPU_LO_INV,
  GPU_LO_REVOR,
  GPU_LO_INVCOPY,
  GPU_LO_INVOR,
  GPU_LO_NAND,
  GPU_LO_SET
} GpuLogicOp;

typedef enum _GpuProjectionType {
  GPU_PERSPECTIVE,
  GPU_ORTHOGRAPHIC
} GpuProjectionType;

typedef enum _GpuFBClamp {
  GPU_CLAMP_NONE,
  GPU_CLAMP_TOP,
  GPU_CLAMP_BOTTOM
} GpuFBClamp;

typedef enum _GpuAnisotropy {
  GPU_ANISO_1,
  GPU_ANISO_2,
  GPU_ANISO_4,
  GPU_MAX_ANISOTROPY
} GpuAnisotropy;

typedef enum _GpuTexMtxType { GPU_MTX3x4 = 0, GPU_MTX2x4 } GpuTexMtxType;

typedef enum _GpuPrimitive {
  GPU_POINTS = 0xb8,
  GPU_LINES = 0xa8,
  GPU_LINESTRIP = 0xb0,
  GPU_TRIANGLES = 0x90,
  GPU_TRIANGLESTRIP = 0x98,
  GPU_TRIANGLEFAN = 0xa0,
  GPU_QUADS = 0x80
} GpuPrimitive;

typedef enum _GpuTexOffset {
    GPU_TO_ZERO,
    GPU_TO_SIXTEENTH,
    GPU_TO_EIGHTH,
    GPU_TO_FOURTH,
    GPU_TO_HALF,
    GPU_TO_ONE,
    GPU_MAX_TEXOFFSET
} GpuTexOffset;


typedef enum _GpuPosNrmMtx {
  GPU_PNMTX0 = 0,
  GPU_PNMTX1 = 3,
  GPU_PNMTX2 = 6,
  GPU_PNMTX3 = 9,
  GPU_PNMTX4 = 12,
  GPU_PNMTX5 = 15,
  GPU_PNMTX6 = 18,
  GPU_PNMTX7 = 21,
  GPU_PNMTX8 = 24,
  GPU_PNMTX9 = 27

} GpuPosNrmMtx;

typedef enum _GpuTexMtx {
  GPU_TEXMTX0 = 30,
  GPU_TEXMTX1 = 33,
  GPU_TEXMTX2 = 36,
  GPU_TEXMTX3 = 39,
  GPU_TEXMTX4 = 42,
  GPU_TEXMTX5 = 45,
  GPU_TEXMTX6 = 48,
  GPU_TEXMTX7 = 51,
  GPU_TEXMTX8 = 54,
  GPU_TEXMTX9 = 57,
  GPU_IDENTITY = 60
} GpuTexMtx;

typedef enum _GpuPTTexMtx {
    GPU_PTTEXMTX0  = 64,
    GPU_PTTEXMTX1  = 67,
    GPU_PTTEXMTX2  = 70,
    GPU_PTTEXMTX3  = 73,
    GPU_PTTEXMTX4  = 76,
    GPU_PTTEXMTX5  = 79,
    GPU_PTTEXMTX6  = 82,
    GPU_PTTEXMTX7  = 85,
    GPU_PTTEXMTX8  = 88,
    GPU_PTTEXMTX9  = 91,
    GPU_PTTEXMTX10 = 94,
    GPU_PTTEXMTX11 = 97,
    GPU_PTTEXMTX12 = 100,
    GPU_PTTEXMTX13 = 103,
    GPU_PTTEXMTX14 = 106,
    GPU_PTTEXMTX15 = 109,
    GPU_PTTEXMTX16 = 112,
    GPU_PTTEXMTX17 = 115,
    GPU_PTTEXMTX18 = 118,
    GPU_PTTEXMTX19 = 121,
    GPU_PTIDENTITY = 125
} GpuPTTexMtx;


typedef enum _GpuSpotFn {
  GPU_SP_OFF = 0,
  GPU_SP_FLAT,
  GPU_SP_COS,
  GPU_SP_COS2,
	GPU_SP_SHARP,
	GPU_SP_RING1,
	GPU_SP_RING2
} GpuSpotFn;

typedef enum _GpuDistAttnFn {
    GPU_DA_OFF = 0,
    GPU_DA_GENTLE,
    GPU_DA_MEDIUM,
    GPU_DA_STEEP
} GpuDistAttnFn;

#ifdef __cplusplus
}
#endif

#endif // GpuENUM_H
