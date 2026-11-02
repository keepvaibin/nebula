#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

#include "Game/Boss/TombSpider.hpp"
#include "Game/Boss/TombSpiderGland.hpp"

class TombSpider;
// class TombSpiderGland;
class TombSpiderVitalSpot;
class TombSpiderAcid;
class TombSpiderThreadAttacher;

class TombSpiderParts {
public:
    TombSpiderParts(TombSpider*);

    void appearThreadAttacherAll();
    void killThreadAttacherAll();
    void pauseOffThreadAttacherAll();
    void init();
    void initParts();

    TombSpider* mParent;
    TombSpiderGland* mGlandFrontL;
    TombSpiderGland* mGlandFrontR;
    TombSpiderGland* mGlandRearL;
    TombSpiderGland* mGlandRearR;
    TombSpiderVitalSpot* mVitalSpotC;
    TombSpiderVitalSpot* mVitalSpotL;
    TombSpiderVitalSpot* mVitalSpotR;
    TombSpiderAcid* mAcid;
    TombSpiderThreadAttacher* mThreadAttacherWing;
    TombSpiderThreadAttacher* mThreadAttacherFrontL;
    TombSpiderThreadAttacher* mThreadAttacherCenterL;
    TombSpiderThreadAttacher* mThreadAttacherRearL;
    TombSpiderThreadAttacher* mThreadAttacherFrontR;
    TombSpiderThreadAttacher* mThreadAttacherCenterR;
    TombSpiderThreadAttacher* mThreadAttacherRearR;
    MtxPtr mMtxEye;
    MtxPtr mMtxMouth;
    MtxPtr mMtxHip;
    MtxPtr mMtxGlandFrontL;
    MtxPtr mMtxGlandFrontR;
    MtxPtr mMtxGlandRearL;
    MtxPtr mMtxGlandRearR;
};
