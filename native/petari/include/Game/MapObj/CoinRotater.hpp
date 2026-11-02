#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CoinRotater : public NameObj {
public:
    CoinRotater(const char*);

    virtual void movement();

    f32 _C;
    f32 _10;
    f32 _14;
    TMtx34f mRotateYMtx;
    TMtx34f mHiSpeedRotateYMtx;
    TMtx34f mWaterRotateMtx;
};

namespace MR {
    void createCoinRotater();
    TMtx34f& getCoinRotateYMatrix();
    TMtx34f& getCoinHiSpeedRotateYMatrix();
    TMtx34f& getCoinInWaterRotateYMatrix();
};  // namespace MR
