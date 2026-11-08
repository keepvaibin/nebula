#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class SpotMarkLight : public PartsModel {
public:
    SpotMarkLight(LiveActor* pHost, f32 radius, f32 height, MtxPtr pMtx);

    virtual void control();
    virtual void calcAndSetBaseMtx();

    bool mIsDisappeared;  // faded out with the player near
};