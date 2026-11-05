#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class CoinRotater : public NameObj {
public:
    CoinRotater(const char*);

    virtual void movement();

    f32 _C;
    f32 _10;
    f32 _14;
    Mtx34f mRotateYMtx;
    Mtx34f mHiSpeedRotateYMtx;
    Mtx34f mWaterRotateMtx;
};

namespace MR {
    void createCoinRotater();
    Mtx34f& getCoinRotateYMatrix();
    Mtx34f& getCoinHiSpeedRotateYMatrix();
    Mtx34f& getCoinInWaterRotateYMatrix();
};  // namespace MR
