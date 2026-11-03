#pragma once

#include <NebulaGpu.h>

struct JPAEmitterWorkData;
class JPABaseParticle;
class NebulaArena;

struct JPABaseShapeData {
    // Common header.
    u8 mMagic[4];
    u32 mSize;

    u32 mFlags;
    s16 mClrPrmAnmOffset;
    s16 mClrEnvAnmOffset;
    f32 mBaseSizeX;
    f32 mBaseSizeY;
    u16 mBlendModeCfg;
    u8 mAlphaCompareCfg;
    u8 mAlphaRef0;
    u8 mAlphaRef1;
    u8 mZModeCfg;
    u8 mTexFlg;
    u8 mTexAnmNum;
    u8 mTexIdx;
    u8 mClrFlg;
    u8 mClrPrmKeyNum;
    u8 mClrEnvKeyNum;
    s16 mClrAnmFrmMax;
    NebulaColor8 mClrPrm;
    NebulaColor8 mClrEnv;
    u8 mAnmRndm;
    u8 mClrAnmRndmMask;
    u8 mTexAnmRndmMask;
};

struct JPABaseShape {
public:
    JPABaseShape(u8 const*, NebulaArena*);
    void setPass(JPAEmitterWorkData*) const;

    static const u8 st_bm[4];
    static const u8 st_lo[16];
    static const u8 st_c[8];
    static u8 blendFactor(u32 code, bool source);

    u8 getBlendMode() const {
        return st_bm[mpData->mBlendModeCfg & 0x03];
    }
    u8 getBlendSrc() const {
        return blendFactor((mpData->mBlendModeCfg >> 2) & 0x0F, true);
    }
    u8 getBlendDst() const {
        return blendFactor((mpData->mBlendModeCfg >> 6) & 0x0F, false);
    }
    u8 getLogicOp() const {
        return st_lo[(mpData->mBlendModeCfg >> 10) & 0x0F];
    }
    bool getZCompLoc() const {
        return ((mpData->mZModeCfg >> 5) & 0x01) != 0;
    }

    bool getZEnable() const {
        return (mpData->mZModeCfg & 0x01) != 0;
    }
    u8 getZCmp() const {
        return st_c[(mpData->mZModeCfg >> 1) & 0x07];
    }
    bool getZUpd() const {
        return ((mpData->mZModeCfg >> 4) & 0x01) != 0;
    }

    u8 getAlphaCmp0() const {
        return st_c[mpData->mAlphaCompareCfg & 0x07];
    }
    u8 getAlphaRef0() const {
        return mpData->mAlphaRef0;
    }
    // 0 and, 1 or, 2 exclusive or, 3 equal.
    u8 getAlphaOp() const {
        return (mpData->mAlphaCompareCfg >> 3) & 0x03;
    }
    u8 getAlphaCmp1() const {
        return st_c[(mpData->mAlphaCompareCfg >> 5) & 0x07];
    }
    u8 getAlphaRef1() const {
        return mpData->mAlphaRef1;
    }

    // The colour and alpha modes (runtime/src/render/materials/particle.hlsl).
    u32 getTevColorMode() const {
        return (mpData->mFlags >> 0x0F) & 0x07;
    }
    u32 getTevAlphaMode() const {
        return (mpData->mFlags >> 0x12) & 0x01;
    }

    u32 getType() const {
        return (mpData->mFlags >> 0) & 0x0F;
    }
    u32 getDirType() const {
        return (mpData->mFlags >> 4) & 0x07;
    }
    u32 getRotType() const {
        return (mpData->mFlags >> 7) & 0x07;
    }
    u32 getBasePlaneType() const {
        return (mpData->mFlags >> 10) & 0x01;
    }
    u32 getTilingS() const {
        return (mpData->mFlags >> 25) & 0x01;
    }
    u32 getTilingT() const {
        return (mpData->mFlags >> 26) & 0x01;
    }
    BOOL isGlblClrAnm() const {
        return mpData->mFlags & 0x00001000;
    }
    BOOL isGlblTexAnm() const {
        return mpData->mFlags & 0x00004000;
    }
    BOOL isPrjTex() const {
        return mpData->mFlags & 0x00100000;
    }
    BOOL isDrawFwdAhead() const {
        return mpData->mFlags & 0x00200000;
    }
    bool isDrawPrntAhead() const {
        return !!(mpData->mFlags & 0x00400000);
    }
    BOOL isClipOn() const {
        return (mpData->mFlags & 0x00800000);
    }
    BOOL isTexCrdAnm() const {
        return mpData->mFlags & 0x01000000;
    }
    u32 isNoDrawParent() const {
        return (mpData->mFlags >> 27) & 1;
    }
    u32 isNoDrawChild() const {
        return (mpData->mFlags >> 28) & 1;
    }

    BOOL isPrmAnm() const {
        return mpData->mClrFlg & 0x02;
    }
    BOOL isEnvAnm() const {
        return mpData->mClrFlg & 0x08;
    }
    inline u32 getClrAnmType() const {
        return (mpData->mClrFlg >> 4) & 0x07;
    }
    s16 getClrAnmMaxFrm() const {
        return mpData->mClrAnmFrmMax;
    }
    void getPrmClr(NebulaColor8* dst) {
        *dst = mpData->mClrPrm;
    }
    void getPrmClr(s16 idx, NebulaColor8* dst) {
        *dst = mpPrmClrAnmTbl[idx];
    }
    void getEnvClr(NebulaColor8* dst) {
        *dst = mpData->mClrEnv;
    }
    void getEnvClr(s16 idx, NebulaColor8* dst) {
        *dst = mpEnvClrAnmTbl[idx];
    }

    BOOL isTexAnm() const {
        return mpData->mTexFlg & 0x01;
    }
    u32 getTexAnmType() const {
        return (mpData->mTexFlg >> 2) & 0x07;
    }
    u32 getTexIdx() const {
        return mpData->mTexIdx;
    }
    u8 getTexIdx(u8 idx) const {
        return mpTexIdxAnimTbl[idx];
    }

    f32 getBaseSizeX() const {
        return mpData->mBaseSizeX;
    }
    f32 getBaseSizeY() const {
        return mpData->mBaseSizeY;
    }
    u8 getClrLoopOfstMask() const {
        return mpData->mClrAnmRndmMask;
    }
    u32 getClrLoopOfst(u32 param_1) const {
        return getClrLoopOfstMask() & param_1;
    }
    u8 getTexLoopOfstMask() const {
        return mpData->mTexAnmRndmMask;
    }
    u32 getTexLoopOfst(u8 param_1) const {
        return getTexLoopOfstMask() & param_1;
    }
    u8 getLoopOfstValue() const {
        return mpData->mAnmRndm;
    }

    f32 getIncTransX() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[5];
    }
    f32 getInitTransX() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[0];
    }
    f32 getIncTransY() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[6];
    }
    f32 getInitTransY() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[1];
    }
    f32 getIncScaleX() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[7];
    }
    f32 getInitScaleX() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[2];
    }
    f32 getIncScaleY() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[8];
    }
    f32 getInitScaleY() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[3];
    }
    f32 getIncRot() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[9];
    }
    f32 getInitRot() const {
        return ((const f32*)mpTexCrdMtxAnmTbl)[4];
    }
    u8 getTexAnmKeyNum() const {
        return mpData->mTexAnmNum;
    }

public:
    const JPABaseShapeData* mpData;
    const void* mpTexCrdMtxAnmTbl;
    const u8* mpTexIdxAnimTbl;
    NebulaColor8* mpPrmClrAnmTbl;
    NebulaColor8* mpEnvClrAnmTbl;
};

struct JPAClrAnmKeyData {
    s16 index;
    NebulaColor8 color;
};

// The quad arrays the particles drawn next read (JPAResource's), and a particle's quad from them (type 1: crossed).
void JPASetQuadArrays(const u8* pPositions, const u8* pTexCoords);
void JPADrawQuad(u32 type);
void JPACalcTexIdxNormal(JPAEmitterWorkData*);
void JPACalcTexIdxRepeat(JPAEmitterWorkData*);
void JPACalcTexIdxReverse(JPAEmitterWorkData*);
void JPACalcTexIdxMerge(JPAEmitterWorkData*);
void JPACalcTexIdxRandom(JPAEmitterWorkData*);
void JPACalcPrm(JPAEmitterWorkData*);
void JPACalcEnv(JPAEmitterWorkData*);
void JPACalcClrIdxNormal(JPAEmitterWorkData*);
void JPACalcClrIdxRepeat(JPAEmitterWorkData*);
void JPACalcClrIdxReverse(JPAEmitterWorkData*);
void JPACalcClrIdxMerge(JPAEmitterWorkData*);
void JPACalcClrIdxRandom(JPAEmitterWorkData*);

void JPACalcTexIdxNormal(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcTexIdxRepeat(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcTexIdxReverse(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcTexIdxMerge(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcTexIdxRandom(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcPrm(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcEnv(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcClrIdxNormal(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcClrIdxRepeat(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcClrIdxReverse(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcClrIdxMerge(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcClrIdxRandom(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcColorCopy(JPAEmitterWorkData*, JPABaseParticle*);

void JPADrawStripe(JPAEmitterWorkData*);
void JPADrawStripeX(JPAEmitterWorkData*);
void JPADrawEmitterCallBackB(JPAEmitterWorkData*);
void JPALoadTex(JPAEmitterWorkData*);
void JPALoadTexAnm(JPAEmitterWorkData*);
void JPAGenTexCrdMtxIdt(JPAEmitterWorkData*);
void JPAGenCalcTexCrdMtxAnm(JPAEmitterWorkData*);
void JPAGenTexCrdMtxAnm(JPAEmitterWorkData*);
void JPAGenTexCrdMtxPrj(JPAEmitterWorkData*);
void JPALoadPosMtxCam(JPAEmitterWorkData*);
void JPASetLineWidth(JPAEmitterWorkData*);
void JPASetPointSize(JPAEmitterWorkData*);
void JPARegistPrm(JPAEmitterWorkData*);
void JPARegistEnv(JPAEmitterWorkData*);
void JPARegistPrmEnv(JPAEmitterWorkData*);

void JPADrawPoint(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawLine(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawRotBillboard(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawBillboard(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawRotDirection(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawDirection(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawRotation(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawDBillboard(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawRotYBillboard(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawYBillboard(JPAEmitterWorkData*, JPABaseParticle*);
void JPADrawParticleCallBack(JPAEmitterWorkData*, JPABaseParticle*);
void JPALoadTexAnm(JPAEmitterWorkData*, JPABaseParticle*);
void JPASetPointSize(JPAEmitterWorkData*, JPABaseParticle*);
void JPASetLineWidth(JPAEmitterWorkData*, JPABaseParticle*);
void JPALoadCalcTexCrdMtxAnm(JPAEmitterWorkData*, JPABaseParticle*);
void JPARegistAlpha(JPAEmitterWorkData*, JPABaseParticle*);
void JPARegistEnv(JPAEmitterWorkData*, JPABaseParticle*);
void JPARegistAlphaEnv(JPAEmitterWorkData*, JPABaseParticle*);
void JPARegistPrmAlpha(JPAEmitterWorkData*, JPABaseParticle*);
void JPARegistPrmAlphaEnv(JPAEmitterWorkData*, JPABaseParticle*);
