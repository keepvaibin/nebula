#pragma once

#include "Game/Boss/TombSpider.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;

class TombSpiderDemo {
public:
    TombSpiderDemo(TombSpider*);

    bool updateGateOpen();
    bool updateCocoonBreak();
    bool updateBattle1stStart();
    bool updateBattle1stEnd();
    bool updateBattle2ndStart();
    bool updateDeath();
    void updateJumpRotateToPlayer();
    bool isStartDemoGateOpen() const;
    bool updateBattle1stStartJumpToPlayer();

    inline ActorCameraInfo* getCameraInfo() {
        return mParent->mCameraInfo;
    }

    TombSpider* mParent;
    TPos3f mMtx;
    f32 mRotateSpeed;
};
