#include "MiiLibInternal.h"
#include "MiiNative.h"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <stddef.h>
#include <stdio.h>

static u16 readU16(const void* p) {
    u16 value;
    memcpy(&value, p, sizeof(value));
    return value;
}

_Static_assert(sizeof(MiiCharModel) >= sizeof(Mii_CharModel), "MiiCharModel holds an Mii_CharModel");

#define NUM_VTX_POS(size) ((size) / VTX_POS_SIZE)
#define SIZE_VTX_POS(count) ((count) * VTX_POS_SIZE)

#define NUM_VTX_NRM(size) ((size) / VTX_NRM_SIZE)
#define SIZE_VTX_NRM(count) ((count) * VTX_NRM_SIZE)

#define NUM_VTX_TXC(size) ((size) / VTX_TXC_SIZE)
#define SIZE_VTX_TXC(count) ((count) * VTX_TXC_SIZE)

Mii_CoordinateData coordinateData = {1, 2, 0, FALSE, FALSE, FALSE};

const MiiDrawCoreSetting cDefaultDrawCoreSetting = {0, FALSE};


static const NebulaColor8 cFacelineColor[MII_I_MAX_FACELINE_COLOR + 1] = {
    // clang-format off
    {240, 216, 196, 255}, 
    {255, 188, 128, 255}, 
    {216, 136, 80,  255},  
    {255, 176, 144, 255}, 
    {152, 80,  48,  255},   
    {82,  46,  28,  255},
    // clang-format on
};

static const NebulaColor8 cHairColor[MII_I_MAX_HAIR_COLOR + 1] = {
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

static const NebulaColor8 cBeardColor[MII_I_MAX_BEARD_COLOR + 1] = {
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

static const NebulaColor8 cGlassColor[MII_I_MAX_GLASS_COLOR + 1] = {
    // clang-format off
    {16,  16, 16, 255},  
    {96,  56, 16, 255},  
    {152, 24, 16, 255}, 
    {32,  48, 96, 255},  
    {144, 88, 0,  255},  
    {96,  88, 80, 255},
    // clang-format on
};

static const NebulaColor8 cFavoriteColor[MiiFavoriteColor_Max] = {
    // clang-format off
    {184, 64,  48,  255}, //!< MiiFavoriteColor_Red
    {240, 120, 40,  255}, //!< MiiFavoriteColor_Orange
    {248, 216, 32,  255}, //!< MiiFavoriteColor_Yellow
    {128, 200, 40,  255}, //!< MiiFavoriteColor_YellowGreen
    {0,   116, 40,  255}, //!< MiiFavoriteColor_Green
    {32,  72,  152, 255}, //!< MiiFavoriteColor_Blue
    {64,  160, 216, 255}, //!< MiiFavoriteColor_SkyBlue
    {232, 96,  120, 255}, //!< MiiFavoriteColor_Pink
    {112, 44,  168, 255}, //!< MiiFavoriteColor_Purple
    {72,  56,  24,  255}, //!< MiiFavoriteColor_Brown
    {224, 224, 224, 255}, //!< MiiFavoriteColor_White
    {24,  24,  20,  255}, //!< MiiFavoriteColor_Black
    // clang-format on
};

static const NebulaColor8 cWhite = {255, 255, 255, 255};

/* The face data's primitive byte as the host's kind. */
static NebulaPrimitive rflPrimitive(int code) {
    switch (code) {
    case 0x90:
        return NEBULA_PRIMITIVE_TRIANGLES;
    case 0x98:
        return NEBULA_PRIMITIVE_TRIANGLE_STRIP;
    case 0xA0:
        return NEBULA_PRIMITIVE_TRIANGLE_FAN;
    case 0xA8:
        return NEBULA_PRIMITIVE_LINES;
    case 0xB0:
        return NEBULA_PRIMITIVE_LINE_STRIP;
    case 0xB8:
        return NEBULA_PRIMITIVE_POINTS;
    default:
        return NEBULA_PRIMITIVE_QUADS;
    }
}

static u32 Mii_GetExpressionNum(u32 exprFlags) {
    int i;
    u32 num = 0;

    for (i = 0; i < MiiExp_Max; i++) {
        if (exprFlags & (1 << i)) {
            num++;
        }
    }

    return num;
}

u32 MiiGetModelBufferSize(MiiResolution res, u32 exprFlags) {
    const u32 exprNum = Mii_GetExpressionNum(exprFlags);
    const s32 texSize = Mii_GetMaskBufSize(res);

    return ROUND_UP(exprNum * sizeof(J3DNativeTexObj), 32) + ROUND_UP(sizeof(Mii_CharModelRes), 32) + ROUND_UP(texSize * exprNum, 32);
}

MiiErrcode MiiInitCharModel(MiiCharModel* model, MiiDataSource src, MiiMiddleDB* db, u16 id, void* work, MiiResolution res, u32 exprFlags) {
    Mii_CharInfo info;

    MiiErrcode err = Mii_PickupCharInfo(&info, src, db, id);
    if (err == MiiErrcode_Success) {
        Mii_InitCharModel(model, &info, work, res, exprFlags);
    }

    return err;
}

void Mii_InitCharModel(MiiCharModel* model, Mii_CharInfo* info, void* work, MiiResolution res, u32 exprFlags) {
    Mii_CharModel* imodel;
    u32 maskSize;
    u8* maskImages[MiiExp_Max];
    u32 maskRes;
    J3DNativeTexObj* exprTexObj;
    u8* image;
    BOOL setExpr;
    f32 maxLod;
    u8 mipmap;
    int i;

    imodel = (Mii_CharModel*)model;

    imodel->resolution = res;
    maskSize = Mii_GetMaskBufSize(res);
    maskRes = Mii_GetMaxMaskRsl(res);
    imodel->res = work;

    // Expression texobjs
    exprTexObj = (J3DNativeTexObj*)ROUND_UP_PTR((u8*)work + sizeof(Mii_CharModelRes), 32);
    for (i = 0; i < MiiExp_Max; i++) {
        if (exprFlags & (1 << i)) {
            imodel->maskTexObj[i] = exprTexObj;
            exprTexObj++;
        } else {
            imodel->maskTexObj[i] = NULL;
        }
    }

    // Expression images
    image = (u8*)ROUND_UP_PTR(exprTexObj, 32);
    for (i = 0; i < MiiExp_Max; i++) {
        if (exprFlags & (1 << i)) {
            maskImages[i] = image;
            image = (u8*)image + maskSize;
        } else {
            maskImages[i] = NULL;
        }
    }

    Mii_InitCharModelRes(imodel->res, info);

    setExpr = FALSE;
    mipmap = FALSE;
    maxLod = 0.0f;

    switch (res) {
    case MiiResolution_64M:
        maxLod = 1.0f;
        mipmap = TRUE;
        break;
    case MiiResolution_128M:
        maxLod = 2.0f;
        mipmap = TRUE;
        break;
    case MiiResolution_256M:
        maxLod = 3.0f;
        mipmap = TRUE;
        break;
    default:
        break;
    }

    for (i = 0; i < MiiExp_Max; i++) {
        if (imodel->maskTexObj[i] != NULL) {
            J3DNativeTextureInit(imodel->maskTexObj[i], maskImages[i], maskRes, maskRes, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);

            if (mipmap) {
                J3DNativeTextureSetFilter(imodel->maskTexObj[i], NEBULA_FILTER_LINEAR_BETWEEN_LEVELS, NEBULA_FILTER_LINEAR, 0.0f, maxLod, 0.0f, FALSE, 0);
            } else {
                J3DNativeTextureSetFilter(imodel->maskTexObj[i], NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR, 0.0f, 0.0f, 0.0f, FALSE, 0);
            }

            if (!setExpr) {
                imodel->expression = (MiiExpression)i;
                setExpr = TRUE;
            }
        }
    }

    Mii_MakeTexture(info, maskImages, res);
}

void MiiSetMtx(MiiCharModel* model, const Mtx mvMtx) {
    Mii_CharModel* imodel = (Mii_CharModel*)model;
    NebulaMtxCopy(mvMtx, imodel->posMtx);
    NebulaMtxInvXpose(mvMtx, imodel->nrmMtx);
}

void MiiSetExpression(MiiCharModel* model, MiiExpression expr) {
    Mii_CharModel* imodel = (Mii_CharModel*)model;
    imodel->expression = expr;
}

NebulaColor8 MiiGetFavoriteColor(MiiFavoriteColor color) {
    return cFavoriteColor[color];
}

NebulaColor8 Mii_GetFacelineColor(const Mii_CharInfo* info) {
    int color = 0;

    if (info->faceline.color < 6) {
        color = info->faceline.color;
    }

    return cFacelineColor[color];
}

void MiiLoadDrawSetting(const MiiDrawSetting* setting) {
    J3DPassAlphaTest(NEBULA_COMPARE_GREATER, 0);
    J3DPassBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    J3DPassDepth(1, NEBULA_COMPARE_LEQUAL, 1);
    J3DPassWriteColor(1);
    J3DPassWriteAlpha(1);
    J3DPassConstantAlpha(0, 0);

    if (setting->lightEnable) {
        Mii_NativeChain(MII_I_NATIVE_CHAIN_LIGHT, setting->lightMask, setting->diffuse, setting->attn, setting->ambColor,
                        cWhite, 0);
    } else {
        Mii_NativeChain(MII_I_NATIVE_CHAIN_NONE, 0, J3D_DF_NONE, J3D_AF_NONE, cWhite, cWhite, 0);
    }
}

void MiiDrawOpa(const MiiCharModel* model) {
    MiiDrawOpaCore(model, &cDefaultDrawCoreSetting);
}

void MiiDrawXlu(const MiiCharModel* model) {
    MiiDrawXluCore(model, &cDefaultDrawCoreSetting);
}

// A shape's primitives (Mii_InitShapeRes's stream) with its 16-bit fixed-point arrays: positions with 8 fraction bits,
// normals with 14, texture coordinates with 13.
static void Mii_DrawShape(const s16* pos, const s16* nrm, const s16* txc, const void* dl, u32 size) {
    const u8* p = (const u8*)dl;
    const u8* end = p + size;

    while (p + 3 <= end) {
        const BOOL streamTxc = (p[0] & 1) != 0;
        const BOOL hasTxc = streamTxc && txc != NULL;
        const NebulaPrimitive prim = rflPrimitive(p[0] & 0xF8);
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

void MiiDrawOpaCore(const MiiCharModel* model, const MiiDrawCoreSetting* setting) {
    Mii_CharModel* imodel = (Mii_CharModel*)model;
    Mii_CharModelRes* res = imodel->res;
    const u8 front = setting->reverseCulling ? J3D_CULL_FRONT : J3D_CULL_BACK;
    const u8 back = setting->reverseCulling ? J3D_CULL_BACK : J3D_CULL_FRONT;

    J3DNativeShapeSetPosition(setting->matrix, imodel->posMtx);
    J3DNativeShapeSetNormal(setting->matrix, imodel->nrmMtx);
    J3DPassPositionMatrix((u8)setting->matrix);

    if (res->beardDlSize > 0) {
        Mii_NativePart(MII_I_NATIVE_FORM_COLOR, cBeardColor[res->beardColor], front, NULL);
        Mii_DrawShape(res->beardVtxPos, res->beardVtxNrm, NULL, res->beardDl, res->beardDlSize);
    }

    if (res->noseDlSize > 0) {
        Mii_NativePart(MII_I_NATIVE_FORM_COLOR, cFacelineColor[res->facelineColor], front, NULL);
        Mii_DrawShape(res->noseVtxPos, res->noseVtxNrm, NULL, res->noseDl, res->noseDlSize);
    }

    // A flipped hairstyle draws its forehead and hair with the other side culled.
    const u8 hairCull = res->flipHair ? back : front;
    if (res->foreheadDlSize > 0) {
        Mii_NativePart(MII_I_NATIVE_FORM_COLOR, cFacelineColor[res->facelineColor], hairCull, NULL);
        Mii_DrawShape(res->foreheadVtxPos, res->foreheadVtxNrm, NULL, res->foreheadDl, res->foreheadDlSize);
    }

    if (res->hairDlSize > 0) {
        Mii_NativePart(MII_I_NATIVE_FORM_COLOR, cHairColor[res->hairColor], hairCull, NULL);
        Mii_DrawShape(res->hairVtxPos, res->hairVtxNrm, NULL, res->hairDl, res->hairDlSize);
    }

    if (res->capDlSize > 0) {
        Mii_NativePart(MII_I_NATIVE_FORM_CAP, cFavoriteColor[res->favoriteColor], hairCull, &res->capTexObj);
        Mii_DrawShape(res->capVtxPos, res->capVtxNrm, res->capVtxTxc, res->capDl, res->capDlSize);
    }

    Mii_NativePart(MII_I_NATIVE_FORM_FACE, cFacelineColor[res->facelineColor], front, &res->faceTexObj);
    Mii_DrawShape(res->faceVtxPos, res->faceVtxNrm, res->faceVtxTxc, res->faceDl, res->faceDlSize);
}

void MiiDrawXluCore(const MiiCharModel* model, const MiiDrawCoreSetting* setting) {
    Mii_CharModel* imodel = (Mii_CharModel*)model;
    Mii_CharModelRes* res = imodel->res;
    const u8 front = setting->reverseCulling ? J3D_CULL_FRONT : J3D_CULL_BACK;
    static const NebulaColor8 cBlack = {0, 0, 0, 255};

    J3DNativeShapeSetPosition(setting->matrix, imodel->posMtx);
    J3DNativeShapeSetNormal(setting->matrix, imodel->nrmMtx);
    J3DPassPositionMatrix((u8)setting->matrix);

    Mii_NativePart(MII_I_NATIVE_FORM_MASK, cBlack, front, imodel->maskTexObj[imodel->expression]);
    Mii_DrawShape(res->maskVtxPos, res->maskVtxNrm, res->maskVtxTxc, res->maskDl, res->maskDlSize);

    if (res->noselineDlSize > 0) {
        Mii_NativePart(MII_I_NATIVE_FORM_NOSELINE, cBlack, front, &res->noseTexObj);
        Mii_DrawShape(res->noselineVtxPos, res->noselineVtxNrm, res->noselineVtxTxc, res->noselineDl, res->noselineDlSize);
    }

    if (res->glassesDlSize > 0) {
        Mii_NativePart(MII_I_NATIVE_FORM_GLASSES, cGlassColor[res->glassesColor], J3D_CULL_NONE, &res->glassesTexObj);
        Mii_DrawShape(res->glassesVtxPos, res->glassesVtxNrm, res->glassesVtxTxc, res->glassesDl, res->glassesDlSize);
    }
}

void Mii_InitCharModelRes(Mii_CharModelRes* res, const Mii_CharInfo* info) {
    Vec noseTrans;
    Vec beardTrans;
    Vec hairTrans;

    /**
     * Faceline shape
     */
    {
        Mii_ShapeRes arg;
        arg.part = Mii_PartsShp_Faceline;
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
        Mii_InitShapeRes(&arg);

        res->faceDlSize = arg.dlSize;
    }

    /**
     * Cap shape
     */
    {
        Mii_ShapeRes arg;
        arg.part = Mii_PartsShp_Cap;
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
        Mii_InitShapeRes(&arg);

        res->capDlSize = arg.dlSize;

        res->hairVtxPos = (s16*)((u8*)res->capVtxPos + (arg.numVtxPos * 4 - arg.numVtxPos) * 2);
        res->hairVtxNrm = (s16*)((u8*)res->capVtxNrm + (arg.numVtxNrm * 4 - arg.numVtxNrm) * 2);

        res->hairDl = res->noseDl + ROUND_UP(arg.dlSize, 32) + offsetof(Mii_CharModelRes, capDl);
    }

    /**
     * Hair shape
     */
    {
        Mii_ShapeRes arg;
        int temp = 4;

        arg.part = Mii_PartsShp_Hair;
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
        Mii_InitShapeRes(&arg);

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
        Mii_ShapeRes arg;
        arg.part = Mii_PartsShp_Forehead;
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
        Mii_InitShapeRes(&arg);

        res->foreheadDlSize = arg.dlSize;
    }

    /**
     * Beard shape
     */
    {
        Mii_ShapeRes arg;
        arg.part = Mii_PartsShp_Beard;
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
        Mii_InitShapeRes(&arg);

        res->beardDlSize = arg.dlSize;
    }

    {
        f32 scale;
        Vec trans;

        /**
         * Nose shape
         */
        {
            Mii_ShapeRes arg;

            scale = 0.4f + 0.175f * info->nose.scale;

            trans.x = noseTrans.x;
            trans.y = +noseTrans.y + -1.5f * (info->nose.y - 8);
            trans.z = noseTrans.z;

            arg.part = Mii_PartsShp_Nose;
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
            Mii_InitShapeRes(&arg);

            res->noseDlSize = arg.dlSize;
        }

        /**
         * Noseline shape
         */
        {
            Mii_ShapeRes arg;

            arg.part = Mii_PartsShp_Noseline;
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
            Mii_InitShapeRes(&arg);

            res->noselineDlSize = arg.dlSize;
        }
    }

    /**
     * Mask shape
     */
    {
        Mii_ShapeRes arg;
        arg.part = Mii_PartsShp_Mask;
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
        Mii_InitShapeRes(&arg);

        res->maskDlSize = arg.dlSize;
    }

    /**
     * Glasses shape
     */
    {
        Mii_ShapeRes arg;
        f32 scale;
        Vec trans;

        scale = 0.15f * info->glass.scale + 0.4f;

        trans.x = noseTrans.x;
        trans.y = 5.0f + noseTrans.y + -1.5f * (info->glass.y - 11);
        trans.z = 2.0f + noseTrans.z;

        arg.part = Mii_PartsShp_Glass;
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
        Mii_InitShapeRes(&arg);

        res->glassesDlSize = arg.dlSize;
    }

    Mii_InitTexRes(&res->faceTexObj, Mii_PartsShpTex_Face, info->faceline.texture, res->faceTex);

    if (res->capDlSize > 0) {
        Mii_InitTexRes(&res->capTexObj, Mii_PartsShpTex_Cap, info->hair.type, res->capTex);
    }

    if (res->noselineDlSize > 0) {
        Mii_InitTexRes(&res->noseTexObj, Mii_PartsShpTex_Noseline, info->nose.type, res->noseTex);
    }

    Mii_InitTexRes(&res->glassesTexObj, Mii_PartsShpTex_Glass, info->glass.type, res->glassesTex);

    res->facelineColor = info->faceline.color;
    res->hairColor = info->hair.color;
    res->beardColor = info->beard.color;
    res->glassesColor = info->glass.color;
    res->favoriteColor = info->personal.color;

}

void Mii_InitShapeRes(Mii_ShapeRes* shape) {

    void* res;
    u8* ptr8;

    // Not all shapes have texture coordinates
    BOOL skipTxc = shape->part == Mii_PartsShp_Forehead || shape->part == Mii_PartsShp_Hair || shape->part == Mii_PartsShp_Beard ||
                   shape->part == Mii_PartsShp_Nose;

    u32 fileSize = Mii_GetShapeSize(shape->part, shape->file);
    res = Mii_Alloc32(fileSize);
    Mii_LoadShape(shape->part, shape->file, res);

    ptr8 = (u8*)res;
    ptr8 += sizeof(u32);

    // Faceline extra data
    if (shape->part == Mii_PartsShp_Faceline) {
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

    if (readU16(ptr8) == 0) {
        shape->numVtxPos = 0;
        shape->numVtxNrm = 0;
        shape->numVtxTxc = 0;
        shape->dlSize = 0;
        Mii_Free(res);
        return;
    }

    shape->numVtxPos = readU16(ptr8);
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

                Mii_TransformCoordinate(&shape->vtxPosBuf[i * VTX_COORDS_IN_POS], temp);
                ptr16 += VTX_COORDS_IN_POS;
            }

        } else if (shape->flipX != 0) {
            for (i = 0; i < shape->numVtxPos; i++) {
                s16 temp[3];

                temp[0] = -ptr16[0];
                temp[1] = ptr16[1];
                temp[2] = ptr16[2];

                Mii_TransformCoordinate(&shape->vtxPosBuf[i * VTX_COORDS_IN_POS], temp);
                ptr16 += VTX_COORDS_IN_POS;
            }
        } else {
            for (i = 0; i < shape->numVtxPos; i++) {
                Mii_TransformCoordinate(&shape->vtxPosBuf[i * VTX_COORDS_IN_POS], ptr16);
                ptr16 += VTX_COORDS_IN_POS;
            }
        }

        ptr8 += byteSize;
    }

    /**
     * Vertex normals
     */

    shape->numVtxNrm = readU16(ptr8);
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

                Mii_TransformCoordinate(&shape->vtxNrmBuf[i * VTX_COORDS_IN_NRM], temp);
                ptr16 += VTX_COORDS_IN_NRM;
            }
        } else {
            int i;
            for (i = 0; i < shape->numVtxNrm; i++) {
                Mii_TransformCoordinate(&shape->vtxNrmBuf[i * VTX_COORDS_IN_NRM], ptr16);
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
            shape->numVtxTxc = readU16(ptr8);
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

        // The shape's primitives as Mii_DrawShape reads them: the primitive (bit 0 set when its vertices have
        // texture coordinates), the vertex count (high byte first), then each vertex's position, normal and texture
        // coordinate indices.
        u8* out = (u8*)shape->dlBuf;

        for (i = 0; i < primitiveNum; i++) {
            u16 vtxNum = *ptr8++;
            u8 prim = *ptr8++;

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
    Mii_Free(res);
}

void Mii_InitTexRes(J3DNativeTexObj* texObj, Mii_PartsShpTex part, u16 file, void* buffer) {
    u32 texSize = 0;
    Mii_Texture* tex;

    tex = (Mii_Texture*)Mii_Alloc32(Mii_GetShpTexSize(part, file));
    Mii_LoadShpTexture(part, file, tex);

    switch (part) {
    case Mii_PartsShpTex_Face:
        texSize = tex->height * tex->width * 2;
        break;
    case Mii_PartsShpTex_Cap:
    case Mii_PartsShpTex_Noseline:
        texSize = tex->height * tex->width / 2;
        break;
    case Mii_PartsShpTex_Glass:
        texSize = tex->height * tex->width;
        break;
    default:
        break;
    }

    // A cooked part (a record, nebula_cooked_texture.h) is copied whole.
    if (nebula_texture_record(Mii_GetTexImage(tex)) != NULL) {
        texSize = nebula_texture_record(Mii_GetTexImage(tex))->size;
    }
    memcpy(buffer, Mii_GetTexImage(tex), texSize);
    // The model draws this copy: handed over here, where its bytes now live (the file is freed below).
    J3DNativeTextureRegisterRecord(buffer);
    J3DNativeTextureInit(texObj, buffer, tex->width, tex->height, tex->wrapS, tex->wrapT);
    J3DNativeTextureSetFilter(texObj, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR, 0.0f, 0.0f, 0.0f, FALSE, 0);
    Mii_Free(tex);
}

static void Mii_TransformCoordinate(s16* to, const s16* from) {
    // to is one of the shape's arrays Mii_DrawShape reads (host byte order).
    to[coordinateData.rOff] = coordinateData.rRev ? -from[0] : from[0];
    to[coordinateData.uOff] = coordinateData.uRev ? -from[1] : from[1];
    to[coordinateData.fOff] = coordinateData.fRev ? -from[2] : from[2];
}

