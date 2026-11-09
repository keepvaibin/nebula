#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class CoinRotater : public NameObj {
public:
    CoinRotater(const char*);

    virtual void movement() override;

    f32 mRotateY;
    f32 mWaterRotateY;
    f32 mHiSpeedRotateY;
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
