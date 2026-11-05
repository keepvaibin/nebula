#pragma once

#include <nebula_geometry.hpp>

class TombSpider;

class TombSpiderAcid {
public:
    TombSpiderAcid(TombSpider*);

    void start();
    void startGland();
    void end();
    void update();
    void updateEmitAcid();
    void updateDeleteAcid();
    void updateGroundMtx(Pos3f*, MtxPtr);

    TombSpider* mParent;
    bool mIsGlandReady;
    bool mIsEmitOn;
    bool mIsActive;
    s32 mStateChangeTimer;
    Pos3f mMouthEffectMtx;
    Pos3f mGlandFrontLEffectMtx;
    Pos3f mGlandFrontREffectMtx;
    Pos3f mGlandRearLEffectMtx;
    Pos3f mGlandRearREffectMtx;
};
