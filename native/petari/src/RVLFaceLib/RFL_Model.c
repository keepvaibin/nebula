#include "RVLFaceLibInternal.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <cstddef>
#include <cstdio>
#include <revolution/gx/GXVert.h>

#ifdef TARGET_PC
_Static_assert(sizeof(RFLCharModel) >= sizeof(RFLiCharModel), "RFLCharModel holds an RFLiCharModel");
#endif

#define NUM_VTX_POS(size) ((size) / VTX_POS_SIZE)
#define SIZE_VTX_POS(count) ((count) * VTX_POS_SIZE)

#define NUM_VTX_NRM(size) ((size) / VTX_NRM_SIZE)
#define SIZE_VTX_NRM(count) ((count) * VTX_NRM_SIZE)

#define NUM_VTX_TXC(size) ((size) / VTX_TXC_SIZE)
#define SIZE_VTX_TXC(count) ((count) * VTX_TXC_SIZE)

RFLiCoordinateData coordinateData = {1, 2, 0, FALSE, FALSE, FALSE};

const RFLDrawCoreSetting cDefaultDrawCoreSetting2Tev = {1, GX_TEXCOORD0, GX_TEXMAP0, 2, GX_TEV_SWAP0, GX_KCOLOR0, GX_TEVPREV, GX_PNMTX0, FALSE};

const RFLDrawCoreSetting cDefaultDrawCoreSetting1Tev = {1, GX_TEXCOORD0, GX_TEXMAP0, 1, GX_TEV_SWAP0, GX_KCOLOR0, GX_TEVPREV, GX_PNMTX0, FALSE};

static const GXColor cFacelineColor[RFLi_MAX_FACELINE_COLOR + 1] = {
    // clang-format off
    {240, 216, 196, 255}, 
    {255, 188, 128, 255}, 
    {216, 136, 80,  255},  
    {255, 176, 144, 255}, 
    {152, 80,  48,  255},   
    {82,  46,  28,  255},
    // clang-format on
};

static const GXColor cHairColor[RFLi_MAX_HAIR_COLOR + 1] = {
    // clang-format off
    {30,  26,  24,  255},    
    {56,  32,  21,  255},    
    {85,  38,  23,  255},    
    {112, 64,  36,  255},   
    {114, 114, 120, 255}, 
    {73,  54,  26,  255},    
    {122, 89,  40,  255},   
    {193, 159, 100, 255},
    // clang-format on
};

static const GXColor cBeardColor[RFLi_MAX_BEARD_COLOR + 1] = {
    // clang-format off
    {30,  26,  24,  255},    
    {56,  32,  21,  255},    
    {85,  38,  23,  255},    
    {112, 64,  36,  255},   
    {114, 114, 120, 255}, 
    {73,  54,  26,  255},    
    {122, 89,  40,  255},   
    {193, 159, 100, 255},
    // clang-format on
};

static const GXColor cGlassColor[RFLi_MAX_GLASS_COLOR + 1] = {
    // clang-format off
    {16,  16, 16, 255},  
    {96,  56, 16, 255},  
    {152, 24, 16, 255}, 
    {32,  48, 96, 255},  
    {144, 88, 0,  255},  
    {96,  88, 80, 255},
    // clang-format on
};

static const GXColor cFavoriteColor[RFLFavoriteColor_Max] = {
    // clang-format off
    {184, 64,  48,  255}, //!< RFLFavoriteColor_Red
    {240, 120, 40,  255}, //!< RFLFavoriteColor_Orange
    {248, 216, 32,  255}, //!< RFLFavoriteColor_Yellow
    {128, 200, 40,  255}, //!< RFLFavoriteColor_YellowGreen
    {0,   116, 40,  255}, //!< RFLFavoriteColor_Green
    {32,  72,  152, 255}, //!< RFLFavoriteColor_Blue
    {64,  160, 216, 255}, //!< RFLFavoriteColor_SkyBlue
    {232, 96,  120, 255}, //!< RFLFavoriteColor_Pink
    {112, 44,  168, 255}, //!< RFLFavoriteColor_Purple
    {72,  56,  24,  255}, //!< RFLFavoriteColor_Brown
    {224, 224, 224, 255}, //!< RFLFavoriteColor_White
    {24,  24,  20,  255}, //!< RFLFavoriteColor_Black
    // clang-format on
};

static const GXColor cWhite = {255, 255, 255, 255};

static void RFLSetCoordinate(RFLCoordinateType u, RFLCoordinateType f) {
    RFLCoordinateType r;

    union {
        RFLCoordinateType c;
        u8 b[4];
    } uu;
    union {
        RFLCoordinateType c;
        u8 b[4];
    } uf;
    union {
        RFLCoordinateType c;
        u8 b[4];
    } ur;

    uu.c = u;
    uf.c = f;

    ur.b[0] = (uu.b[1] * uf.b[2]) - (uu.b[2] * uf.b[1]);
    ur.b[1] = (uu.b[2] * uf.b[0]) - (uu.b[0] * uf.b[2]);
    ur.b[2] = (uu.b[0] * uf.b[1]) - (uu.b[1] * uf.b[0]);

    r = ur.c;

    if (u & RFLCoordinateType_X) {
        coordinateData.uOff = 0;  // U -> X
    } else if (u & RFLCoordinateType_Y) {
        coordinateData.uOff = 1;  // U -> Y
    } else {
        coordinateData.uOff = 2;  // U -> Z
    }

    if (f & RFLCoordinateType_X) {
        coordinateData.fOff = 0;  // F -> X
    } else if (f & RFLCoordinateType_Y) {
        coordinateData.fOff = 1;  // F -> Y
    } else {
        coordinateData.fOff = 2;  // F -> Z
    }

    if (r & RFLCoordinateType_X) {
        coordinateData.rOff = 0;  // R -> X
    } else if (r & RFLCoordinateType_Y) {
        coordinateData.rOff = 1;  // R -> Y
    } else {
        coordinateData.rOff = 2;  // R -> Z
    }

    coordinateData.uRev = (u & RFLCoordinateType_RevMask) != 0;
    coordinateData.fRev = (f & RFLCoordinateType_RevMask) != 0;
    coordinateData.rRev = (r & RFLCoordinateType_RevMask) != 0;
}

static u32 RFLiGetExpressionNum(u32 exprFlags) {
    int i;
    u32 num = 0;

    for (i = 0; i < RFLExp_Max; i++) {
        if (exprFlags & (1 << i)) {
            num++;
        }
    }

    return num;
}

u32 RFLGetModelBufferSize(RFLResolution res, u32 exprFlags) {
    const u32 exprNum = RFLiGetExpressionNum(exprFlags);
    const s32 texSize = RFLiGetMaskBufSize(res);

    return ROUND_UP(exprNum * sizeof(GXTexObj), 32) + ROUND_UP(sizeof(RFLiCharModelRes), 32) + ROUND_UP(texSize * exprNum, 32);
}

RFLErrcode RFLInitCharModel(RFLCharModel* model, RFLDataSource src, RFLMiddleDB* db, u16 id, void* work, RFLResolution res, u32 exprFlags) {
    RFLiCharInfo info;

    RFLErrcode err = RFLiPickupCharInfo(&info, src, db, id);
    if (err == RFLErrcode_Success) {
        RFLiInitCharModel(model, &info, work, res, exprFlags);
    }

    return err;
}

void RFLiInitCharModel(RFLCharModel* model, RFLiCharInfo* info, void* work, RFLResolution res, u32 exprFlags) {
    RFLiCharModel* imodel;
    u32 maskSize;
    u8* maskImages[RFLExp_Max];
    u32 maskRes;
    GXTexObj* exprTexObj;
    u8* image;
    BOOL setExpr;
    f32 maxLod;
    u8 mipmap;
    int i;

    imodel = (RFLiCharModel*)model;

    imodel->resolution = res;
    maskSize = RFLiGetMaskBufSize(res);
    maskRes = RFLiGetMaxMaskRsl(res);
    imodel->res = work;

    // Expression texobjs
    exprTexObj = (GXTexObj*)ROUND_UP_PTR((u8*)work + sizeof(RFLiCharModelRes), 32);
    for (i = 0; i < RFLExp_Max; i++) {
        if (exprFlags & (1 << i)) {
            imodel->maskTexObj[i] = exprTexObj;
            exprTexObj++;
        } else {
            imodel->maskTexObj[i] = NULL;
        }
    }

    // Expression images
    image = (u8*)ROUND_UP_PTR(exprTexObj, 32);
    for (i = 0; i < RFLExp_Max; i++) {
        if (exprFlags & (1 << i)) {
            maskImages[i] = image;
            image = (u8*)image + maskSize;
        } else {
            maskImages[i] = NULL;
        }
    }

    RFLiInitCharModelRes(imodel->res, info);

    setExpr = FALSE;
    mipmap = FALSE;
    maxLod = 0.0f;

    switch (res) {
    case RFLResolution_64M:
        maxLod = 1.0f;
        mipmap = TRUE;
        break;
    case RFLResolution_128M:
        maxLod = 2.0f;
        mipmap = TRUE;
        break;
    case RFLResolution_256M:
        maxLod = 3.0f;
        mipmap = TRUE;
        break;
    }

    for (i = 0; i < RFLExp_Max; i++) {
        if (imodel->maskTexObj[i] != NULL) {
            J3DNativeTextureInit(imodel->maskTexObj[i], maskImages[i], maskRes, maskRes, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, mipmap);

            if (mipmap) {
                J3DNativeTextureInitLOD(imodel->maskTexObj[i], GX_LIN_MIP_LIN, GX_LINEAR, 0.0f, maxLod, 0.0f, FALSE, FALSE, GX_ANISO_1);
            } else {
                J3DNativeTextureInitLOD(imodel->maskTexObj[i], GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, FALSE, FALSE, GX_ANISO_1);
            }

            if (!setExpr) {
                imodel->expression = (RFLExpression)i;
                setExpr = TRUE;
            }
        }
    }

    RFLiMakeTexture(info, maskImages, res);
}

void RFLSetMtx(RFLCharModel* model, const Mtx mvMtx) {
    RFLiCharModel* imodel = (RFLiCharModel*)model;
    PSMTXCopy(mvMtx, imodel->posMtx);
    PSMTXInvXpose(mvMtx, imodel->nrmMtx);
}

void RFLSetExpression(RFLCharModel* model, RFLExpression expr) {
    RFLiCharModel* imodel = (RFLiCharModel*)model;
    imodel->expression = expr;
}

static RFLExpression RFLGetExpression(const RFLCharModel* model) {
    RFLiCharModel* imodel = (RFLiCharModel*)model;
    return imodel->expression;
}

GXColor RFLGetFavoriteColor(RFLFavoriteColor color) {
    return cFavoriteColor[color];
}

GXColor RFLiGetFacelineColor(const RFLiCharInfo* info) {
    int color = 0;

    if (info->faceline.color < 6) {
        color = info->faceline.color;
    }

    return cFacelineColor[color];
}

void RFLLoadDrawSetting(const RFLDrawSetting* setting) {
    J3DNativeStateSetAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_NEVER, 0);
    J3DNativeStateSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
    J3DNativeStateSetZMode(TRUE, GX_LEQUAL, TRUE);
    J3DNativeStateSetZCompLoc(setting->compLoc);
    J3DNativeStateSetColorUpdate(TRUE);
    J3DNativeStateSetAlphaUpdate(TRUE);
    J3DNativeStateSetDither(FALSE);
    J3DNativeStateSetDstAlpha(FALSE, 0);

    if (setting->lightEnable) {
        J3DNativeStateSetTevDirect(GX_TEVSTAGE1);
        J3DNativeStateSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
        J3DNativeStateSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        J3DNativeStateSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
        J3DNativeStateSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV);
        J3DNativeStateSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
        J3DNativeStateSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV);

        RFLLoadMaterialSetting(&cDefaultDrawCoreSetting2Tev);
        RFLLoadVertexSetting(&cDefaultDrawCoreSetting2Tev);
        J3DNativeStateSetNumChans(1);

        J3DNativeStateSetChanCtrl(GX_COLOR0, TRUE, GX_SRC_REG, GX_SRC_REG, setting->lightMask, setting->diffuse, setting->attn);
        J3DNativeStateSetChanCtrl(GX_ALPHA0, FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeStateSetChanAmbColor(GX_COLOR0, setting->ambColor);
        J3DNativeStateSetChanMatColor(GX_COLOR0, cWhite);
    } else {
        RFLLoadMaterialSetting(&cDefaultDrawCoreSetting1Tev);
        RFLLoadVertexSetting(&cDefaultDrawCoreSetting1Tev);
        J3DNativeStateSetNumChans(0);
    }
}

void RFLDrawOpa(const RFLCharModel* model) {
    RFLDrawOpaCore(model, &cDefaultDrawCoreSetting2Tev);
}

void RFLDrawXlu(const RFLCharModel* model) {
    RFLDrawXluCore(model, &cDefaultDrawCoreSetting2Tev);
}

// A shape's primitives (RFLiInitShapeRes's stream) with its 16-bit fixed-point arrays: positions with 8 fraction bits,
// normals with 14, texture coordinates with 13.
static void RFLiDrawShape(const s16* pos, const s16* nrm, const s16* txc, const void* dl, u32 size) {
    const u8* p = (const u8*)dl;
    const u8* end = p + size;

    while (p + 3 <= end) {
        const BOOL streamTxc = (p[0] & 1) != 0;
        const BOOL hasTxc = streamTxc && txc != NULL;
        const GXPrimitive prim = (GXPrimitive)(p[0] & 0xF8);
        const u16 count = (u16)(p[1] << 8 | p[2]);
        int i;

        p += 3;
        J3DNativeDrawBegin(prim, count, NEBULA_SHAPE_NORMAL | (hasTxc ? NEBULA_SHAPE_TEXCOORD0 : 0));
        for (i = 0; i < count; i++) {
            const s16* v = pos + 3 * p[0];
            const s16* n = nrm + 3 * p[1];

            J3DNativeDrawPosition(v[0] / 256.0f, v[1] / 256.0f, v[2] / 256.0f);
            J3DNativeDrawNormal(n[0] / 16384.0f, n[1] / 16384.0f, n[2] / 16384.0f);
            if (hasTxc) {
                const s16* c = txc + 2 * p[2];
                J3DNativeDrawTexCoord(c[0] / 8192.0f, c[1] / 8192.0f);
            }
            p += streamTxc ? 3 : 2;
        }
    }
}

void RFLLoadVertexSetting(const RFLDrawCoreSetting* setting) {
    J3DNativeStateSetNumTexGens(setting->txcGenNum);
}

void RFLLoadMaterialSetting(const RFLDrawCoreSetting* setting) {
    J3DNativeStateSetTevSwapModeTable(setting->tevSwapTable, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
    J3DNativeStateSetTevSwapModeTable(setting->tevSwapTable + 1, GX_CH_RED, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_GREEN);
    J3DNativeStateSetNumTevStages(setting->tevStageNum);
    J3DNativeStateSetTevDirect(GX_TEVSTAGE0);
    J3DNativeStateSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, setting->tevOutRegID);
    J3DNativeStateSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, setting->tevOutRegID);
    J3DNativeStateSetTevKColorSel(GX_TEVSTAGE0, (GXTevKColorSel)(setting->tevKColorID + GX_TEV_KCSEL_K0));
    J3DNativeStateSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_8_8);
}

void RFLDrawOpaCore(const RFLCharModel* model, const RFLDrawCoreSetting* setting) {
    RFLiCharModel* imodel = (RFLiCharModel*)model;
    RFLiCharModelRes* res = imodel->res;

    J3DNativeStateSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);

    // @bug Copy-paste error
    J3DNativeStateSetTevSwapMode(GX_TEVSTAGE0, setting->tevSwapTable, setting->tevSwapTable);
    J3DNativeStateSetTevSwapMode(GX_TEVSTAGE0, setting->tevSwapTable, setting->tevSwapTable);

    J3DNativeStateSetCullMode(setting->reverseCulling ? GX_CULL_FRONT : GX_CULL_BACK);

    J3DNativeShapeSendPosition(imodel->posMtx, setting->posNrmMtxID);
    J3DNativeShapeSendNormal(imodel->nrmMtx, setting->posNrmMtxID);
    J3DNativeStateSetCurrentMtx(setting->posNrmMtxID);

    J3DNativeStateSetTexCoordGen2(setting->txcID, GX_TG_MTX2x4, GX_TG_POS, 60, 0, 0x7D);
    J3DNativeStateSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    J3DNativeStateSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);

    if (res->beardDlSize > 0) {
        J3DNativeStateSetTevKColor(setting->tevKColorID, cBeardColor[res->beardColor]);
        RFLiDrawShape(res->beardVtxPos, res->beardVtxNrm, NULL, res->beardDl, res->beardDlSize);
    }

    J3DNativeStateSetTevKColor(setting->tevKColorID, cFacelineColor[res->facelineColor]);

    if (res->noseDlSize > 0) {
        RFLiDrawShape(res->noseVtxPos, res->noseVtxNrm, NULL, res->noseDl, res->noseDlSize);
    }

    if (res->flipHair) {
        J3DNativeStateSetCullMode(setting->reverseCulling ? GX_CULL_BACK : GX_CULL_FRONT);
    }

    if (res->foreheadDlSize > 0) {
        RFLiDrawShape(res->foreheadVtxPos, res->foreheadVtxNrm, NULL, res->foreheadDl, res->foreheadDlSize);
    }

    if (res->hairDlSize > 0) {
        J3DNativeStateSetTevKColor(setting->tevKColorID, cHairColor[res->hairColor]);
        RFLiDrawShape(res->hairVtxPos, res->hairVtxNrm, NULL, res->hairDl, res->hairDlSize);
    }

    J3DNativeStateSetTevOrder(GX_TEVSTAGE0, setting->txcID, setting->texMapID, GX_COLOR_NULL);
    J3DNativeStateSetTexCoordGen2(setting->txcID, GX_TG_MTX2x4, GX_TG_TEX0, 60, 0, 0x7D);

    if (res->capDlSize > 0) {
        J3DNativeStateSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_KONST);
        J3DNativeStateSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, 1, setting->tevOutRegID);
        J3DNativeStateSetTevKColor(setting->tevKColorID, cFavoriteColor[res->favoriteColor]);

        J3DNativeTextureLoad(&res->capTexObj, setting->texMapID);
        RFLiDrawShape(res->capVtxPos, res->capVtxNrm, res->capVtxTxc, res->capDl, res->capDlSize);

        J3DNativeStateSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, setting->tevOutRegID);
    }

    J3DNativeStateSetTevColorIn(GX_TEVSTAGE0, GX_CC_KONST, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
    J3DNativeStateSetTevKColor(setting->tevKColorID, cFacelineColor[res->facelineColor]);
    J3DNativeStateSetTevSwapMode(GX_TEVSTAGE0, setting->tevSwapTable, (GXTevSwapSel)(setting->tevSwapTable + 1));

    if (res->flipHair) {
        J3DNativeStateSetCullMode(setting->reverseCulling ? GX_CULL_FRONT : GX_CULL_BACK);
    }

    J3DNativeTextureLoad(&res->faceTexObj, setting->texMapID);
    RFLiDrawShape(res->faceVtxPos, res->faceVtxNrm, res->faceVtxTxc, res->faceDl, res->faceDlSize);
}

void RFLDrawXluCore(const RFLCharModel* model, const RFLDrawCoreSetting* setting) {
    RFLiCharModel* imodel = (RFLiCharModel*)model;
    RFLiCharModelRes* res = imodel->res;

    J3DNativeStateSetTevOrder(GX_TEVSTAGE0, setting->txcID, setting->texMapID, GX_COLOR_NULL);
    J3DNativeStateSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
    J3DNativeStateSetTevSwapMode(GX_TEVSTAGE0, setting->tevSwapTable, setting->tevSwapTable);

    J3DNativeShapeSendPosition(imodel->posMtx, setting->posNrmMtxID);
    J3DNativeShapeSendNormal(imodel->nrmMtx, setting->posNrmMtxID);
    J3DNativeStateSetCurrentMtx(setting->posNrmMtxID);

    J3DNativeStateSetTexCoordGen2(setting->txcID, GX_TG_MTX2x4, GX_TG_TEX0, 60, 0, 0x7D);
    J3DNativeStateSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    J3DNativeStateSetCullMode(setting->reverseCulling ? GX_CULL_FRONT : GX_CULL_BACK);

    J3DNativeTextureLoad(imodel->maskTexObj[imodel->expression], setting->texMapID);
    RFLiDrawShape(res->maskVtxPos, res->maskVtxNrm, res->maskVtxTxc, res->maskDl, res->maskDlSize);

    if (res->noselineDlSize > 0) {
        J3DNativeStateSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
        J3DNativeTextureLoad(&res->noseTexObj, setting->texMapID);
        RFLiDrawShape(res->noselineVtxPos, res->noselineVtxNrm, res->noselineVtxTxc, res->noselineDl, res->noselineDlSize);
    }

    if (res->glassesDlSize > 0) {
        J3DNativeStateSetTevKColor(setting->tevKColorID, cGlassColor[res->glassesColor]);
        J3DNativeStateSetCullMode(GX_CULL_NONE);
        J3DNativeStateSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
        J3DNativeTextureLoad(&res->glassesTexObj, setting->texMapID);
        RFLiDrawShape(res->glassesVtxPos, res->glassesVtxNrm, res->glassesVtxTxc, res->glassesDl, res->glassesDlSize);
    }
}

void RFLiInitCharModelRes(RFLiCharModelRes* res, const RFLiCharInfo* info) {
    Vec noseTrans;
    Vec beardTrans;
    Vec hairTrans;

    /**
     * Faceline shape
     */
    {
        RFLiShapeRes arg;
        arg.part = RFLiPartsShp_Faceline;
        arg.file = info->faceline.type;
        arg.vtxPosBuf = res->faceVtxPos;
        arg.vtxNrmBuf = res->faceVtxNrm;
        arg.vtxTxcBuf = res->faceVtxTxc;
        arg.dlBuf = res->faceDl;
        arg.vtxPosBufSize = NUM_VTX_POS(sizeof(res->faceVtxPos));
        arg.vtxNrmBufSize = NUM_VTX_NRM(sizeof(res->faceVtxNrm));
        arg.vtxTxcBufSize = NUM_VTX_TXC(sizeof(res->faceVtxTxc));
        arg.dlBufSize = sizeof(res->faceDl);
        arg.noseTrans = &noseTrans;
        arg.beardTrans = &beardTrans;
        arg.hairTrans = &hairTrans;
        arg.flipX = FALSE;
        arg.transform = FALSE;
        RFLiInitShapeRes(&arg);

        res->faceDlSize = arg.dlSize;
    }

    /**
     * Cap shape
     */
    {
        RFLiShapeRes arg;
        arg.part = RFLiPartsShp_Cap;
        arg.file = info->hair.type;
        arg.vtxPosBuf = res->capVtxPos;
        arg.vtxNrmBuf = res->capVtxNrm;
        arg.vtxTxcBuf = res->capVtxTxc;
        arg.dlBuf = res->capDl;
        arg.vtxPosBufSize = NUM_VTX_POS(sizeof(res->capVtxPos));
        arg.vtxNrmBufSize = NUM_VTX_NRM(sizeof(res->capVtxNrm));
        arg.vtxTxcBufSize = NUM_VTX_TXC(sizeof(res->capVtxTxc));
        arg.dlBufSize = sizeof(res->capDl);
        arg.flipX = info->hair.flip;
        arg.transform = TRUE;
        arg.posScale = 1.0f;
        arg.posTrans = &hairTrans;
        RFLiInitShapeRes(&arg);

        res->capDlSize = arg.dlSize;

        res->hairVtxPos = (s16*)((u8*)res->capVtxPos + (arg.numVtxPos * 4 - arg.numVtxPos) * 2);
        res->hairVtxNrm = (s16*)((u8*)res->capVtxNrm + (arg.numVtxNrm * 4 - arg.numVtxNrm) * 2);

        res->hairDl = res->noseDl + ROUND_UP(arg.dlSize, 32) + offsetof(RFLiCharModelRes, capDl);
    }

    /**
     * Hair shape
     */
    {
        RFLiShapeRes arg;
        int temp = 4;

        arg.part = RFLiPartsShp_Hair;
        arg.file = info->hair.type;
        arg.vtxPosBuf = res->hairVtxPos;
        arg.vtxNrmBuf = res->hairVtxNrm;
        arg.dlBuf = res->hairDl;

        arg.vtxPosBufSize =
            NUM_VTX_POS(sizeof(res->capVtxPos)) - (((uintptr_t)res->hairVtxPos - (uintptr_t)res->capVtxPos) / VTX_COORD_SIZE) / VTX_COORDS_IN_POS;

        arg.vtxNrmBufSize =
            NUM_VTX_POS(sizeof(res->capVtxNrm)) - (((uintptr_t)res->hairVtxNrm - (uintptr_t)res->capVtxNrm) / VTX_COORD_SIZE) / VTX_COORDS_IN_NRM;

        arg.dlBufSize = sizeof(res->capDl) - ((uintptr_t)res->hairDl - (uintptr_t)res->capDl);

        arg.flipX = info->hair.flip;
        arg.transform = TRUE;
        arg.posScale = 1.0f;
        arg.posTrans = &hairTrans;
        RFLiInitShapeRes(&arg);

        res->hairDlSize = arg.dlSize;

        res->foreheadVtxPos = (s16*)((u8*)res->hairVtxPos + (arg.numVtxPos * temp - arg.numVtxPos) * 2);

        res->foreheadVtxNrm = (s16*)((u8*)res->hairVtxNrm + (arg.numVtxNrm * temp - arg.numVtxNrm) * 2);

        res->foreheadDl = res->hairDl + ROUND_UP(arg.dlSize, 32);

        res->flipHair = info->hair.flip;
    }

    /**
     * Forehead shape
     */
    {
        RFLiShapeRes arg;
        arg.part = RFLiPartsShp_Forehead;
        arg.file = info->hair.type;
        arg.vtxPosBuf = res->foreheadVtxPos;
        arg.vtxNrmBuf = res->foreheadVtxNrm;
        arg.dlBuf = res->foreheadDl;

        arg.vtxPosBufSize =
            NUM_VTX_POS(sizeof(res->capVtxPos)) - (((uintptr_t)res->foreheadVtxPos - (uintptr_t)res->capVtxPos) / VTX_COORD_SIZE) / VTX_COORDS_IN_POS;

        arg.vtxNrmBufSize =
            NUM_VTX_POS(sizeof(res->capVtxNrm)) - (((uintptr_t)res->foreheadVtxNrm - (uintptr_t)res->capVtxNrm) / VTX_COORD_SIZE) / VTX_COORDS_IN_NRM;

        arg.dlBufSize = sizeof(res->capDl) - ((uintptr_t)res->foreheadDl - (uintptr_t)res->capDl);

        arg.flipX = info->hair.flip;
        arg.transform = TRUE;
        arg.posScale = 1.0f;
        arg.posTrans = &hairTrans;
        RFLiInitShapeRes(&arg);

        res->foreheadDlSize = arg.dlSize;
    }

    /**
     * Beard shape
     */
    {
        RFLiShapeRes arg;
        arg.part = RFLiPartsShp_Beard;
        arg.file = info->beard.type;
        arg.vtxPosBuf = res->beardVtxPos;
        arg.vtxNrmBuf = res->beardVtxNrm;
        arg.dlBuf = res->beardDl;
        arg.vtxPosBufSize = NUM_VTX_POS(sizeof(res->beardVtxPos));
        arg.vtxNrmBufSize = NUM_VTX_NRM(sizeof(res->beardVtxNrm));
        arg.dlBufSize = sizeof(res->beardDl);
        arg.flipX = FALSE;
        arg.transform = TRUE;
        arg.posScale = 1.0f;
        arg.posTrans = &beardTrans;
        RFLiInitShapeRes(&arg);

        res->beardDlSize = arg.dlSize;
    }

    {
        f32 scale;
        Vec trans;

        /**
         * Nose shape
         */
        {
            RFLiShapeRes arg;

            scale = 0.4f + 0.175f * info->nose.scale;

            trans.x = noseTrans.x;
            trans.y = +noseTrans.y + -1.5f * (info->nose.y - 8);
            trans.z = noseTrans.z;

            arg.part = RFLiPartsShp_Nose;
            arg.file = info->nose.type;
            arg.vtxPosBuf = res->noseVtxPos;
            arg.vtxNrmBuf = res->noseVtxNrm;
            arg.dlBuf = res->noseDl;
            arg.vtxPosBufSize = NUM_VTX_POS(sizeof(res->noseVtxPos));
            arg.vtxNrmBufSize = NUM_VTX_NRM(sizeof(res->noseVtxNrm));
            arg.dlBufSize = sizeof(res->noseDl);
            arg.flipX = FALSE;
            arg.transform = TRUE;
            arg.posScale = scale;
            arg.posTrans = &trans;
            RFLiInitShapeRes(&arg);

            res->noseDlSize = arg.dlSize;
        }

        /**
         * Noseline shape
         */
        {
            RFLiShapeRes arg;

            arg.part = RFLiPartsShp_Noseline;
            arg.file = info->nose.type;
            arg.vtxPosBuf = res->noselineVtxPos;
            arg.vtxNrmBuf = res->noselineVtxNrm;
            arg.vtxTxcBuf = res->noselineVtxTxc;
            arg.dlBuf = res->noselineDl;
            arg.vtxPosBufSize = NUM_VTX_POS(sizeof(res->noselineVtxPos));
            arg.vtxNrmBufSize = NUM_VTX_NRM(sizeof(res->noselineVtxNrm));
            arg.vtxTxcBufSize = NUM_VTX_TXC(sizeof(res->noselineVtxTxc));
            arg.dlBufSize = sizeof(res->noselineDl);
            arg.flipX = FALSE;
            arg.transform = TRUE;
            arg.posScale = scale;
            arg.posTrans = &trans;
            RFLiInitShapeRes(&arg);

            res->noselineDlSize = arg.dlSize;
        }
    }

    /**
     * Mask shape
     */
    {
        RFLiShapeRes arg;
        arg.part = RFLiPartsShp_Mask;
        arg.file = info->faceline.type;
        arg.vtxPosBuf = res->maskVtxPos;
        arg.vtxNrmBuf = res->maskVtxNrm;
        arg.vtxTxcBuf = res->maskVtxTxc;
        arg.dlBuf = res->maskDl;
        arg.vtxPosBufSize = NUM_VTX_POS(sizeof(res->maskVtxPos));
        arg.vtxNrmBufSize = NUM_VTX_NRM(sizeof(res->maskVtxNrm));
        arg.vtxTxcBufSize = NUM_VTX_TXC(sizeof(res->maskVtxTxc));
        arg.dlBufSize = sizeof(res->maskDl);
        arg.flipX = FALSE;
        arg.transform = FALSE;
        RFLiInitShapeRes(&arg);

        res->maskDlSize = arg.dlSize;
    }

    /**
     * Glasses shape
     */
    {
        RFLiShapeRes arg;
        f32 scale;
        Vec trans;

        scale = 0.15f * info->glass.scale + 0.4f;

        trans.x = noseTrans.x;
        trans.y = 5.0f + noseTrans.y + -1.5f * (info->glass.y - 11);
        trans.z = 2.0f + noseTrans.z;

        arg.part = RFLiPartsShp_Glass;
        arg.file = 0;
        arg.vtxPosBuf = res->glassesVtxPos;
        arg.vtxNrmBuf = res->glassesVtxNrm;
        arg.vtxTxcBuf = res->glassesVtxTxc;
        arg.dlBuf = res->glassesDl;
        arg.vtxPosBufSize = NUM_VTX_POS(sizeof(res->glassesVtxPos));
        arg.vtxNrmBufSize = NUM_VTX_NRM(sizeof(res->glassesVtxNrm));
        arg.vtxTxcBufSize = NUM_VTX_TXC(sizeof(res->glassesVtxTxc));
        arg.dlBufSize = sizeof(res->glassesDl);
        arg.flipX = FALSE;
        arg.transform = TRUE;
        arg.posScale = scale;
        arg.posTrans = &trans;
        RFLiInitShapeRes(&arg);

        res->glassesDlSize = arg.dlSize;
    }

    RFLiInitTexRes(&res->faceTexObj, RFLiPartsShpTex_Face, info->faceline.texture, res->faceTex);

    if (res->capDlSize > 0) {
        RFLiInitTexRes(&res->capTexObj, RFLiPartsShpTex_Cap, info->hair.type, res->capTex);
    }

    if (res->noselineDlSize > 0) {
        RFLiInitTexRes(&res->noseTexObj, RFLiPartsShpTex_Noseline, info->nose.type, res->noseTex);
    }

    RFLiInitTexRes(&res->glassesTexObj, RFLiPartsShpTex_Glass, info->glass.type, res->glassesTex);

    res->facelineColor = info->faceline.color;
    res->hairColor = info->hair.color;
    res->beardColor = info->beard.color;
    res->glassesColor = info->glass.color;
    res->favoriteColor = info->personal.color;

}

void RFLiInitShapeRes(RFLiShapeRes* shape) {
    static const u32 csHeader[RFLiPartsShp_Max] = {'nose', 'frhd', 'face', 'hair', 'cap_', 'berd', 'nsln', 'mask', 'glas'};

    void* res;
    u8* ptr8;

    // Not all shapes have texture coordinates
    BOOL skipTxc = shape->part == RFLiPartsShp_Forehead || shape->part == RFLiPartsShp_Hair || shape->part == RFLiPartsShp_Beard ||
                   shape->part == RFLiPartsShp_Nose;

    u32 fileSize = RFLiGetShapeSize(shape->part, shape->file);
    res = RFLiAlloc32(fileSize);
    RFLiLoadShape(shape->part, shape->file, res);

    ptr8 = (u8*)res;
    ptr8 += sizeof(u32);

    // Faceline extra data
    if (shape->part == RFLiPartsShp_Faceline) {
        memcpy(shape->noseTrans, ptr8, sizeof(Vec));
        ptr8 += sizeof(Vec);
        memcpy(shape->beardTrans, ptr8, sizeof(Vec));
        ptr8 += sizeof(Vec);
        memcpy(shape->hairTrans, ptr8, sizeof(Vec));
        ptr8 += sizeof(Vec);
    }

    /**
     * Vertex positions
     */

    if (*(u16*)ptr8 == 0) {
        shape->numVtxPos = 0;
        shape->numVtxNrm = 0;
        shape->numVtxTxc = 0;
        shape->dlSize = 0;
        RFLiFree(res);
        return;
    }

    shape->numVtxPos = *(u16*)ptr8;
    ptr8 += sizeof(u16);

    {
        u32 byteSize = SIZE_VTX_POS(shape->numVtxPos);
        s16* ptr16 = (s16*)ptr8;
        int i;

        if (shape->transform) {
            s32 s = 256.0f * shape->posScale;
            s32 tx = 256.0f * shape->posTrans->x;
            s32 ty = 256.0f * shape->posTrans->y;
            s32 tz = 256.0f * shape->posTrans->z;

            for (i = 0; i < shape->numVtxPos; i++) {
                s16 temp[3];

                if (shape->flipX) {
                    temp[0] = tx + ((-ptr16[0] * s) >> 8);
                } else {
                    temp[0] = tx + ((ptr16[0] * s) >> 8);
                }

                temp[1] = ty + ((ptr16[1] * s) >> 8);
                temp[2] = tz + ((ptr16[2] * s) >> 8);

                RFLiTransformCoordinate(&shape->vtxPosBuf[i * VTX_COORDS_IN_POS], temp);
                ptr16 += VTX_COORDS_IN_POS;
            }

        } else if (shape->flipX != 0) {
            for (i = 0; i < shape->numVtxPos; i++) {
                s16 temp[3];

                temp[0] = -ptr16[0];
                temp[1] = ptr16[1];
                temp[2] = ptr16[2];

                RFLiTransformCoordinate(&shape->vtxPosBuf[i * VTX_COORDS_IN_POS], temp);
                ptr16 += VTX_COORDS_IN_POS;
            }
        } else {
            for (i = 0; i < shape->numVtxPos; i++) {
                RFLiTransformCoordinate(&shape->vtxPosBuf[i * VTX_COORDS_IN_POS], ptr16);
                ptr16 += VTX_COORDS_IN_POS;
            }
        }

        ptr8 += byteSize;
    }

    /**
     * Vertex normals
     */

    shape->numVtxNrm = *(u16*)ptr8;
    ptr8 += sizeof(u16);

    {
        s16* ptr16 = (s16*)ptr8;
        u32 byteSize = SIZE_VTX_NRM(shape->numVtxNrm);

        if (shape->flipX != 0) {
            int i;
            for (i = 0; i < shape->numVtxNrm; i++) {
                s16 temp[3];

                temp[0] = -ptr16[0];
                temp[1] = ptr16[1];
                temp[2] = ptr16[2];

                RFLiTransformCoordinate(&shape->vtxNrmBuf[i * VTX_COORDS_IN_NRM], temp);
                ptr16 += VTX_COORDS_IN_NRM;
            }
        } else {
            int i;
            for (i = 0; i < shape->numVtxNrm; i++) {
                RFLiTransformCoordinate(&shape->vtxNrmBuf[i * VTX_COORDS_IN_NRM], ptr16);
                ptr16 += VTX_COORDS_IN_NRM;
            }
        }

        ptr8 += byteSize;
    }

    /**
     * Vertex texcoords
     */

    {
        u32 byteSize;

        if (skipTxc) {
            shape->numVtxTxc = 0;
        } else {
            shape->numVtxTxc = *(u16*)ptr8;
            ptr8 += sizeof(u16);

            byteSize = SIZE_VTX_TXC(shape->numVtxTxc);
            memcpy(shape->vtxTxcBuf, ptr8, byteSize);
            ptr8 += byteSize;
        }
    }

    /**
     * Display list
     */

    {
        int i;
        int j;
        s32 primitiveNum = *ptr8++;

        // The shape's primitives as RFLiDrawShape reads them: the primitive (bit 0 set when its vertices have
        // texture coordinates), the vertex count (high byte first), then each vertex's position, normal and texture
        // coordinate indices - the size of GX's display list of them.
        u8* out = (u8*)shape->dlBuf;

        for (i = 0; i < primitiveNum; i++) {
            u16 vtxNum = *ptr8++;
            GXPrimitive prim = *ptr8++;

            *out++ = (u8)(prim | (skipTxc ? 0 : 1));
            *out++ = (u8)(vtxNum >> 8);
            *out++ = (u8)vtxNum;
            for (j = 0; j < vtxNum; j++) {
                *out++ = *ptr8++;
                *out++ = *ptr8++;

                if (!skipTxc) {
                    *out++ = *ptr8++;
                }
            }
        }
        shape->dlSize = (u32)(out - (u8*)shape->dlBuf);
    }
    RFLiFree(res);
}

void RFLiInitTexRes(GXTexObj* texObj, RFLiPartsShpTex part, u16 file, void* buffer) {
    u32 texSize;
    RFLiTexture* tex;

    tex = (RFLiTexture*)RFLiAlloc32(RFLiGetShpTexSize(part, file));
    RFLiLoadShpTexture(part, file, tex);

    switch (part) {
    case RFLiPartsShpTex_Face:
        texSize = tex->height * tex->width * 2;
        break;
    case RFLiPartsShpTex_Cap:
    case RFLiPartsShpTex_Noseline:
        texSize = tex->height * tex->width / 2;
        break;
    case RFLiPartsShpTex_Glass:
        texSize = tex->height * tex->width;
        break;
    }

    memcpy(buffer, RFLiGetTexImage(tex), texSize);
    // The model draws this copy: handed over here, where its bytes now live (the file is freed below).
    J3DNativeTextureRegisterImage(buffer, tex->format, tex->width, tex->height, 1);
    J3DNativeTextureInit(texObj, buffer, tex->width, tex->height, tex->format, tex->wrapS, tex->wrapT, FALSE);
    J3DNativeTextureInitLOD(texObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, FALSE, FALSE, GX_ANISO_1);
    RFLiFree(tex);
}

static void RFLiTransformCoordinate(s16* to, const s16* from) {
    // to is one of the shape's arrays RFLiDrawShape reads (host byte order).
    to[coordinateData.rOff] = coordinateData.rRev ? -from[0] : from[0];
    to[coordinateData.uOff] = coordinateData.uRev ? -from[1] : from[1];
    to[coordinateData.fOff] = coordinateData.fRev ? -from[2] : from[2];
}

static void RFLDrawShape(const RFLCharModel* model) {
    GXCullMode cullMode;
    RFLiCharModel* imodel = (RFLiCharModel*)model;
    RFLiCharModelRes* res = imodel->res;


    J3DNativeShapeSendPosition(imodel->posMtx, 0);
    J3DNativeShapeSendNormal(imodel->nrmMtx, 0);
    J3DNativeStateSetCurrentMtx(0);

    if (res->beardDlSize > 0) {
        RFLiDrawShape(res->beardVtxPos, res->beardVtxNrm, NULL, res->beardDl, res->beardDlSize);
    }

    if (res->noseDlSize > 0) {
        RFLiDrawShape(res->noseVtxPos, res->noseVtxNrm, NULL, res->noseDl, res->noseDlSize);
    }

    if (res->flipHair) {
        J3DNativeStateGetCullMode(&cullMode);
        switch (cullMode) {
        case GX_CULL_BACK:
            J3DNativeStateSetCullMode(GX_CULL_FRONT);
            break;
        case GX_CULL_FRONT:
            J3DNativeStateSetCullMode(GX_CULL_BACK);
            break;
        }
    }

    if (res->foreheadDlSize > 0) {
        RFLiDrawShape(res->foreheadVtxPos, res->foreheadVtxNrm, NULL, res->foreheadDl, res->foreheadDlSize);
    }

    if (res->hairDlSize > 0) {
        RFLiDrawShape(res->hairVtxPos, res->hairVtxNrm, NULL, res->hairDl, res->hairDlSize);
    }

    if (res->capDlSize > 0) {
        RFLiDrawShape(res->capVtxPos, res->capVtxNrm, res->capVtxTxc, res->capDl, res->capDlSize);
    }

    if (res->flipHair) {
        switch (cullMode) {
        case GX_CULL_BACK:
            J3DNativeStateSetCullMode(GX_CULL_BACK);
            break;
        case GX_CULL_FRONT:
            J3DNativeStateSetCullMode(GX_CULL_FRONT);
            break;
        }
    }

    RFLiDrawShape(res->faceVtxPos, res->faceVtxNrm, res->faceVtxTxc, res->faceDl, res->faceDlSize);
}

void RFLSetModelDrawDoneCallback(RFLCallback cb) {
    RFLiGetManager()->modelDrawCb = cb;
}
