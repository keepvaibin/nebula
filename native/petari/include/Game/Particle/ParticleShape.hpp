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

void ParticleCalcTexIdxNormal(EmitterWork*);
void ParticleCalcTexIdxRepeat(EmitterWork*);
void ParticleCalcTexIdxReverse(EmitterWork*);
void ParticleCalcTexIdxMerge(EmitterWork*);
void ParticleCalcTexIdxRandom(EmitterWork*);
void ParticleCalcPrm(EmitterWork*);
void ParticleCalcEnv(EmitterWork*);
void ParticleCalcClrIdxNormal(EmitterWork*);
void ParticleCalcClrIdxRepeat(EmitterWork*);
void ParticleCalcClrIdxReverse(EmitterWork*);
void ParticleCalcClrIdxMerge(EmitterWork*);
void ParticleCalcClrIdxRandom(EmitterWork*);

void ParticleCalcTexIdxNormal(EmitterWork*, ParticleCore*);
void ParticleCalcTexIdxRepeat(EmitterWork*, ParticleCore*);
void ParticleCalcTexIdxReverse(EmitterWork*, ParticleCore*);
void ParticleCalcTexIdxMerge(EmitterWork*, ParticleCore*);
void ParticleCalcTexIdxRandom(EmitterWork*, ParticleCore*);
void ParticleCalcPrm(EmitterWork*, ParticleCore*);
void ParticleCalcEnv(EmitterWork*, ParticleCore*);
void ParticleCalcClrIdxNormal(EmitterWork*, ParticleCore*);
void ParticleCalcClrIdxRepeat(EmitterWork*, ParticleCore*);
void ParticleCalcClrIdxReverse(EmitterWork*, ParticleCore*);
void ParticleCalcClrIdxMerge(EmitterWork*, ParticleCore*);
void ParticleCalcClrIdxRandom(EmitterWork*, ParticleCore*);
void ParticleCalcColorCopy(EmitterWork*, ParticleCore*);

void ParticleDrawStripe(EmitterWork*);
void ParticleDrawStripeX(EmitterWork*);
void ParticleDrawEmitterCallBackB(EmitterWork*);
void ParticleLoadTex(EmitterWork*);
void ParticleLoadTexAnm(EmitterWork*);
void ParticleGenTexCrdMtxIdt(EmitterWork*);
void ParticleGenCalcTexCrdMtxAnm(EmitterWork*);
void ParticleGenTexCrdMtxAnm(EmitterWork*);
void ParticleGenTexCrdMtxPrj(EmitterWork*);
void ParticleLoadPosMtxCam(EmitterWork*);
void ParticleSetLineWidth(EmitterWork*);
void ParticleSetPointSize(EmitterWork*);
void ParticleRegistPrm(EmitterWork*);
void ParticleRegistEnv(EmitterWork*);
void ParticleRegistPrmEnv(EmitterWork*);

void ParticleDrawPoint(EmitterWork*, ParticleCore*);
void ParticleDrawLine(EmitterWork*, ParticleCore*);
void ParticleDrawRotBillboard(EmitterWork*, ParticleCore*);
void ParticleDrawBillboard(EmitterWork*, ParticleCore*);
void ParticleDrawRotDirection(EmitterWork*, ParticleCore*);
void ParticleDrawDirection(EmitterWork*, ParticleCore*);
void ParticleDrawRotation(EmitterWork*, ParticleCore*);
void ParticleDrawDBillboard(EmitterWork*, ParticleCore*);
void ParticleDrawRotYBillboard(EmitterWork*, ParticleCore*);
void ParticleDrawYBillboard(EmitterWork*, ParticleCore*);
void ParticleDrawParticleCallBack(EmitterWork*, ParticleCore*);
void ParticleLoadTexAnm(EmitterWork*, ParticleCore*);
void ParticleSetPointSize(EmitterWork*, ParticleCore*);
void ParticleSetLineWidth(EmitterWork*, ParticleCore*);
void ParticleLoadCalcTexCrdMtxAnm(EmitterWork*, ParticleCore*);
void ParticleRegistAlpha(EmitterWork*, ParticleCore*);
void ParticleRegistEnv(EmitterWork*, ParticleCore*);
void ParticleRegistAlphaEnv(EmitterWork*, ParticleCore*);
void ParticleRegistPrmAlpha(EmitterWork*, ParticleCore*);
void ParticleRegistPrmAlphaEnv(EmitterWork*, ParticleCore*);
