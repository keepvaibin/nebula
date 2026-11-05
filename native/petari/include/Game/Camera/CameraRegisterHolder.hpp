#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

#define NR_REGISTERS 0x40

class CameraRegisterHolder : public NameObj {
public:
    CameraRegisterHolder(const char*);

    void declareMtxReg(const char*, MtxPtr);
    MtxPtr getMtx(const char*) const;

    void declareVecReg(const char*, const Vec3f*);
    const Vec3f* getVec(const char*) const;
    void declareFloatReg(const char*, const f32*);
    const char* getDummyVecRegName() const;

    void initMtx();
    void initVec();
    void initFloat();
    void initDummyRegister();

    s32 searchName(const char* const*, u32, const char*) const;

    u32 mNrMatrices;
    MtxPtr mMatrices[NR_REGISTERS];
    const char* mMatrixNames[NR_REGISTERS];
    u32 mNrVectors;
    const Vec3f* mVectors[NR_REGISTERS];
    const char* mVectorNames[NR_REGISTERS];
    u32 mNrFloats;
    const f32* mFloats[NR_REGISTERS];
    const char* mFloatNames[NR_REGISTERS];
    Mtx34f mDummyMatrix;
    Vec3f mDummyVector;
    f32 mDummyFloat;
};
