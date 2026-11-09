#pragma once

#include <nebula_geometry.hpp>

class TombSpider;
class TombSpiderGland;

class TombSpiderAcid {
public:
    TombSpiderAcid(TombSpider* pParent);

    void start();
    void startGland();
    void end();
    void update();
    void updateEmitAcid();
    void updateDeleteAcid();
    void updateGroundMtx(Pos3f* pDstMtx, MtxPtr pSrcMtx);
    void updateGlandGroundMtx(const TombSpiderGland* pGland, Pos3f* pDstMtx, MtxPtr pSrcMtx);

    TombSpider* mParent;
    bool mIsEmitOn;
    bool mIsActive;
    s32 mStateChangeTimer;
    Pos3f mMouthEffectMtx;
    Pos3f mGlandFrontLEffectMtx;
    Pos3f mGlandFrontREffectMtx;
    Pos3f mGlandRearLEffectMtx;
    Pos3f mGlandRearREffectMtx;
};
