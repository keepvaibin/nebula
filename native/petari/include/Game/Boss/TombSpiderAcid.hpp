#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

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
    void updateGroundMtx(TPos3f*, MtxPtr);

    TombSpider* mParent;
    bool mIsGlandReady;
    bool mIsEmitOn;
    bool mIsActive;
    s32 mStateChangeTimer;
    TPos3f mMouthEffectMtx;
    TPos3f mGlandFrontLEffectMtx;
    TPos3f mGlandFrontREffectMtx;
    TPos3f mGlandRearLEffectMtx;
    TPos3f mGlandRearREffectMtx;
};
