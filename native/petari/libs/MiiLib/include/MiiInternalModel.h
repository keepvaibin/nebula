#ifndef RVL_FACE_LIBRARY_INTERNAL_MODEL_H
#define RVL_FACE_LIBRARY_INTERNAL_MODEL_H
#include "MiiModel.h"
#include "MiiInternalTypes.h"
#include <NebulaGpu.h>
#include <nebula_math.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VTX_COORDS_IN_POS 3
#define VTX_COORDS_IN_NRM 3
#define VTX_COORDS_IN_TXC 2

#define VTX_COORD_SIZE sizeof(s16)
#define VTX_POS_SIZE (VTX_COORD_SIZE * VTX_COORDS_IN_POS)
#define VTX_NRM_SIZE (VTX_COORD_SIZE * VTX_COORDS_IN_NRM)
#define VTX_TXC_SIZE (VTX_COORD_SIZE * VTX_COORDS_IN_TXC)

typedef struct Mii_CoordinateData {
    u8 uOff;
    u8 fOff;
    u8 rOff;
    BOOL uRev;
    BOOL fRev;
    BOOL rRev;
} Mii_CoordinateData;

typedef struct Mii_CharModelRes {
    u8 noseDl[0xC0];
    u8 capDl[0x560];
    u8 faceDl[0x2E0];
    u8 beardDl[0x160];
    u8 noselineDl[0x60];
    u8 maskDl[0x380];
    u8 glassesDl[0x40];

    // The parts' textures as records (nebula_cooked_texture.h): their texels, at most four bytes each, after a 64-byte
    // header.
    u8 faceTex[0x8000 + 0x40];
    u8 capTex[0x2000 + 0x40];
    u8 noseTex[0x2000 + 0x40];
    u8 glassesTex[0x4000 + 0x40];

    s16 noseVtxPos[23 * VTX_COORDS_IN_POS];
    s16 noseVtxNrm[23 * VTX_COORDS_IN_NRM];

    s16 capVtxPos[173 * VTX_COORDS_IN_POS];
    s16 capVtxNrm[246 * VTX_COORDS_IN_NRM];
    s16 capVtxTxc[95 * VTX_COORDS_IN_TXC];

    s16 faceVtxPos[66 * VTX_COORDS_IN_POS];
    s16 faceVtxNrm[66 * VTX_COORDS_IN_NRM];
    s16 faceVtxTxc[115 * VTX_COORDS_IN_TXC];

    s16 beardVtxPos[40 * VTX_COORDS_IN_POS];
    s16 beardVtxNrm[68 * VTX_COORDS_IN_NRM];

    s16 noselineVtxPos[6 * VTX_COORDS_IN_POS];
    s16 noselineVtxNrm[2 * VTX_COORDS_IN_NRM];
    s16 noselineVtxTxc[7 * VTX_COORDS_IN_TXC];

    s16 maskVtxPos[88 * VTX_COORDS_IN_POS];
    s16 maskVtxNrm[86 * VTX_COORDS_IN_NRM];
    s16 maskVtxTxc[176 * VTX_COORDS_IN_TXC];

    s16 glassesVtxPos[4 * VTX_COORDS_IN_POS];
    s16 glassesVtxNrm[1 * VTX_COORDS_IN_NRM];
    s16 glassesVtxTxc[4 * VTX_COORDS_IN_TXC];

    J3DNativeTexObj faceTexObj;
    J3DNativeTexObj capTexObj;
    J3DNativeTexObj noseTexObj;
    J3DNativeTexObj glassesTexObj;

    s16* hairVtxPos;
    s16* hairVtxNrm;
    u8* hairDl;
    s16* foreheadVtxPos;
    s16* foreheadVtxNrm;
    u8* foreheadDl;

    u16 noseDlSize;
    u16 faceDlSize;
    u16 hairDlSize;
    u16 capDlSize;
    u16 foreheadDlSize;
    u16 beardDlSize;
    u16 noselineDlSize;
    u16 maskDlSize;
    u16 glassesDlSize;

    u8 facelineColor;
    u8 hairColor;
    u8 beardColor;
    u8 glassesColor;
    u8 favoriteColor;

    BOOL flipHair;

    char UNK_0x8250[0x10];
} Mii_CharModelRes;

typedef struct Mii_ShapeRes {
    Mii_PartsShp part;
    u16 file;
    BOOL transform;
    BOOL flipX;
    s16* vtxPosBuf;
    s16* vtxNrmBuf;
    s16* vtxTxcBuf;
    u8* dlBuf;
    u16 vtxPosBufSize;
    u16 vtxNrmBufSize;
    u16 vtxTxcBufSize;
    u16 dlBufSize;
    u16 numVtxPos;
    u16 numVtxNrm;
    u16 numVtxTxc;
    u16 dlSize;
    f32 posScale;
    Vec* posTrans;
    Vec* noseTrans;
    Vec* beardTrans;
    Vec* hairTrans;
} Mii_ShapeRes;

typedef struct Mii_CharModel {
    Mtx posMtx;
    Mtx nrmMtx;
    MiiExpression expression;
    MiiResolution resolution;
    Mii_CharModelRes* res;
    J3DNativeTexObj* maskTexObj[MiiExp_Max];
} Mii_CharModel;

extern Mii_CoordinateData coordinateData;

inline void Mii_SetCoordinateData(const Mii_CoordinateData* data) {
    coordinateData = *data;
}

inline Mii_CoordinateData* Mii_GetCoordinateData(void) {
    return &coordinateData;
}

u32 Mii_GetExpressionNum(u32 exprFlags);
void Mii_InitCharModel(MiiCharModel* model, Mii_CharInfo* info, void* work,
                       MiiResolution res, u32 exprFlags);
NebulaColor8 Mii_GetFacelineColor(const Mii_CharInfo* info);
void Mii_InitCharModelRes(Mii_CharModelRes* res, const Mii_CharInfo* info);
void Mii_InitShapeRes(Mii_ShapeRes* shape);
void Mii_InitTexRes(J3DNativeTexObj* texObj, Mii_PartsShpTex part, u16 file,
                    void* buffer);
void Mii_TransformCoordinate(s16* to, const s16* from);

#ifdef __cplusplus
}
#endif
#endif
