#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class SpiderThreadWindCtrl {
public:
    SpiderThreadWindCtrl();

    void update();
    void startWindBattle();
    void startWindChance();
    void startWindBattleEnd();

    s32 getTimeToStartWind() const;
    s32 getWindTime() const;

    f32 mWindTargetSpeed;
    f32 mWindSpeed;
    f32 mWindAccel;
    TVec3f mWindDirection;
    TVec3f mWind;
};
