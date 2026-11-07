#pragma once

#include "Game/Particle/ParticleData.hpp"

struct EmitterWork;

class ExTexShape {
public:
    explicit ExTexShape(const ExTexShapeData* pData) : mpData(pData) {
    }

    // The resource's matrix.
    void getIndTexMtx(f32 (*pMtx)[3]) const {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 3; j++) {
                pMtx[i][j] = mpData->mIndTexMtx[i][j];
            }
        }
    }
    s8 getExpScale() const {
        return mpData->mExpScale;
    }
    u8 getIndTexIdx() const {
        return mpData->mIndTexIdx;
    }
    u8 getSecTexIdx() const {
        return mpData->mSecTexIdx;
    }
    bool isUseIndirect() const {
        return mpData->mUseIndirect != 0;
    }
    bool isUseSecTex() const {
        return mpData->mUseSecTex != 0;
    }

public:
    const ExTexShapeData* mpData;
};

void ParticleLoadExTex(EmitterWork*);
