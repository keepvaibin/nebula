#pragma once

#include "Game/Boss/TombSpider.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;

class TombSpiderDemo {
public:
    TombSpiderDemo(TombSpider* pParent);

    bool updateGateOpen();
    bool updateCocoonBreak();
    bool updateBattle1stStart();
    bool updateBattle1stEnd();
    bool updateBattle2ndStart();
    bool updateDeath();
    void updateJumpRotateToPlayer();
    bool isStartDemoGateOpen() const;
    bool updateBattle1stStartJumpToPlayer();
    void updateRotateBetween(s32 startStep, s32 endStep);

    inline ActorCameraInfo* getCameraInfo() {
        return mParent->mCameraInfo;
    }

    TombSpider* mParent;
    Pos3f mMtx;
    f32 mRotateSpeed;
};
