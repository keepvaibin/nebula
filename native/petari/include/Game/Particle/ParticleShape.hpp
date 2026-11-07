#pragma once

#include "Game/Particle/ParticleData.hpp"

struct EmitterWork;
class ParticleCore;

struct ParticleShape {
public:
    ParticleShape(const void* pFile, const ParticleShapeData* pData)
        : mpData(pData), mpTexIdxAnimTbl(particleRecord< u8 >(pFile, pData->mTexIdxTable)),
          mpPrmClrAnmTbl(particleRecord< NebulaColor8 >(pFile, pData->mPrmTable)),
          mpEnvClrAnmTbl(particleRecord< NebulaColor8 >(pFile, pData->mEnvTable)) {
    }
    void setPass(EmitterWork*) const;

    u8 getBlendMode() const {
        return mpData->mBlendOp;
    }
    u8 getBlendSrc() const {
        return mpData->mBlendSrc;
    }
    u8 getBlendDst() const {
        return mpData->mBlendDst;
    }
    bool getZEnable() const {
        return mpData->mZEnable != 0;
    }
    u8 getZCmp() const {
        return mpData->mZCompare;
    }
    bool getZUpd() const {
        return mpData->mZUpdate != 0;
    }
    u8 getAlphaCmp0() const {
        return mpData->mAlphaCompare0;
    }
    u8 getAlphaRef0() const {
        return mpData->mAlphaRef0;
    }
    // 0 and, 1 or, 2 exclusive or, 3 equal.
    u8 getAlphaOp() const {
        return mpData->mAlphaOp;
    }
    u8 getAlphaCmp1() const {
        return mpData->mAlphaCompare1;
    }
    u8 getAlphaRef1() const {
        return mpData->mAlphaRef1;
    }

    // The colour and alpha modes (runtime/src/render/materials/particle.hlsl).
    u32 getStageColorMode() const {
        return mpData->mColorMode;
    }
    u32 getStageAlphaMode() const {
        return mpData->mAlphaMode;
    }

    u32 getType() const {
        return mpData->mType;
    }
    u32 getDirType() const {
        return mpData->mDirType;
    }
    u32 getRotType() const {
        return mpData->mRotType;
    }
    u32 getBasePlaneType() const {
        return mpData->mPlaneType;
    }
    u32 getTilingS() const {
        return mpData->mTilingS;
    }
    u32 getTilingT() const {
        return mpData->mTilingT;
    }
    bool isGlblClrAnm() const {
        return mpData->mGlobalColorAnm != 0;
    }
    bool isGlblTexAnm() const {
        return mpData->mGlobalTexAnm != 0;
    }
    bool isPrjTex() const {
        return mpData->mPrjTex != 0;
    }
    bool isDrawFwdAhead() const {
        return mpData->mDrawFwdAhead != 0;
    }
    bool isDrawPrntAhead() const {
        return mpData->mDrawPrntAhead != 0;
    }
    bool isClipOn() const {
        return mpData->mClipOn != 0;
    }
    bool isTexCrdAnm() const {
        return mpData->mTexCrdAnmOn != 0;
    }
    bool isNoDrawParent() const {
        return mpData->mNoDrawParent != 0;
    }
    bool isNoDrawChild() const {
        return mpData->mNoDrawChild != 0;
    }

    bool isPrmAnm() const {
        return mpData->mPrmAnm != 0;
    }
    bool isEnvAnm() const {
        return mpData->mEnvAnm != 0;
    }
    u32 getClrAnmType() const {
        return mpData->mColorAnmType;
    }
    s16 getClrAnmMaxFrm() const {
        return mpData->mColorAnmFrameMax;
    }
    void getPrmClr(NebulaColor8* dst) const {
        *dst = mpData->mPrm;
    }
    void getPrmClr(s16 idx, NebulaColor8* dst) const {
        *dst = mpPrmClrAnmTbl[idx];
    }
    void getEnvClr(NebulaColor8* dst) const {
        *dst = mpData->mEnv;
    }
    void getEnvClr(s16 idx, NebulaColor8* dst) const {
        *dst = mpEnvClrAnmTbl[idx];
    }

    bool isTexAnm() const {
        return mpData->mTexAnm != 0;
    }
    u32 getTexAnmType() const {
        return mpData->mTexAnmType;
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
    u32 getClrLoopOfst(u32 random) const {
        return mpData->mColorLoopMask & random;
    }
    u32 getTexLoopOfst(u8 random) const {
        return mpData->mTexLoopMask & random;
    }
    u8 getLoopOfstValue() const {
        return mpData->mLoopValue;
    }

    f32 getInitTransX() const {
        return mpData->mTexCrdAnm[0];
    }
    f32 getInitTransY() const {
        return mpData->mTexCrdAnm[1];
    }
    f32 getInitScaleX() const {
        return mpData->mTexCrdAnm[2];
    }
    f32 getInitScaleY() const {
        return mpData->mTexCrdAnm[3];
    }
    f32 getInitRot() const {
        return mpData->mTexCrdAnm[4];
    }
    f32 getIncTransX() const {
        return mpData->mTexCrdAnm[5];
    }
    f32 getIncTransY() const {
        return mpData->mTexCrdAnm[6];
    }
    f32 getIncScaleX() const {
        return mpData->mTexCrdAnm[7];
    }
    f32 getIncScaleY() const {
        return mpData->mTexCrdAnm[8];
    }
    f32 getIncRot() const {
        return mpData->mTexCrdAnm[9];
    }
    u8 getTexAnmKeyNum() const {
        return mpData->mTexAnmKeyNum;
    }

public:
    const ParticleShapeData* mpData;
    const u8* mpTexIdxAnimTbl;
    const NebulaColor8* mpPrmClrAnmTbl;
    const NebulaColor8* mpEnvClrAnmTbl;
};

// The quad arrays the particles drawn next read (ParticleResource's), and a particle's quad from them (type 1: crossed).
void JPASetQuadArrays(const u8* pPositions, const u8* pTexCoords);
void JPADrawQuad(u32 type);
void JPACalcTexIdxNormal(EmitterWork*);
void JPACalcTexIdxRepeat(EmitterWork*);
void JPACalcTexIdxReverse(EmitterWork*);
void JPACalcTexIdxMerge(EmitterWork*);
void JPACalcTexIdxRandom(EmitterWork*);
void JPACalcPrm(EmitterWork*);
void JPACalcEnv(EmitterWork*);
void JPACalcClrIdxNormal(EmitterWork*);
void JPACalcClrIdxRepeat(EmitterWork*);
void JPACalcClrIdxReverse(EmitterWork*);
void JPACalcClrIdxMerge(EmitterWork*);
void JPACalcClrIdxRandom(EmitterWork*);

void JPACalcTexIdxNormal(EmitterWork*, ParticleCore*);
void JPACalcTexIdxRepeat(EmitterWork*, ParticleCore*);
void JPACalcTexIdxReverse(EmitterWork*, ParticleCore*);
void JPACalcTexIdxMerge(EmitterWork*, ParticleCore*);
void JPACalcTexIdxRandom(EmitterWork*, ParticleCore*);
void JPACalcPrm(EmitterWork*, ParticleCore*);
void JPACalcEnv(EmitterWork*, ParticleCore*);
void JPACalcClrIdxNormal(EmitterWork*, ParticleCore*);
void JPACalcClrIdxRepeat(EmitterWork*, ParticleCore*);
void JPACalcClrIdxReverse(EmitterWork*, ParticleCore*);
void JPACalcClrIdxMerge(EmitterWork*, ParticleCore*);
void JPACalcClrIdxRandom(EmitterWork*, ParticleCore*);
void JPACalcColorCopy(EmitterWork*, ParticleCore*);

void JPADrawStripe(EmitterWork*);
void JPADrawStripeX(EmitterWork*);
void JPADrawEmitterCallBackB(EmitterWork*);
void JPALoadTex(EmitterWork*);
void JPALoadTexAnm(EmitterWork*);
void JPAGenTexCrdMtxIdt(EmitterWork*);
void JPAGenCalcTexCrdMtxAnm(EmitterWork*);
void JPAGenTexCrdMtxAnm(EmitterWork*);
void JPAGenTexCrdMtxPrj(EmitterWork*);
void JPALoadPosMtxCam(EmitterWork*);
void JPASetLineWidth(EmitterWork*);
void JPASetPointSize(EmitterWork*);
void JPARegistPrm(EmitterWork*);
void JPARegistEnv(EmitterWork*);
void JPARegistPrmEnv(EmitterWork*);

void JPADrawPoint(EmitterWork*, ParticleCore*);
void JPADrawLine(EmitterWork*, ParticleCore*);
void JPADrawRotBillboard(EmitterWork*, ParticleCore*);
void JPADrawBillboard(EmitterWork*, ParticleCore*);
void JPADrawRotDirection(EmitterWork*, ParticleCore*);
void JPADrawDirection(EmitterWork*, ParticleCore*);
void JPADrawRotation(EmitterWork*, ParticleCore*);
void JPADrawDBillboard(EmitterWork*, ParticleCore*);
void JPADrawRotYBillboard(EmitterWork*, ParticleCore*);
void JPADrawYBillboard(EmitterWork*, ParticleCore*);
void JPADrawParticleCallBack(EmitterWork*, ParticleCore*);
void JPALoadTexAnm(EmitterWork*, ParticleCore*);
void JPASetPointSize(EmitterWork*, ParticleCore*);
void JPASetLineWidth(EmitterWork*, ParticleCore*);
void JPALoadCalcTexCrdMtxAnm(EmitterWork*, ParticleCore*);
void JPARegistAlpha(EmitterWork*, ParticleCore*);
void JPARegistEnv(EmitterWork*, ParticleCore*);
void JPARegistAlphaEnv(EmitterWork*, ParticleCore*);
void JPARegistPrmAlpha(EmitterWork*, ParticleCore*);
void JPARegistPrmAlphaEnv(EmitterWork*, ParticleCore*);
