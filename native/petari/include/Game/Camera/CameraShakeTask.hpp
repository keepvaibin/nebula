#pragma once

#include <nebula_geometry.hpp>

class CameraShakePattern;

class CameraShakeTask {
public:
    CameraShakeTask(CameraShakePattern*);

    void start(u32, u32);
    void startInfinity(u32);
    void endForce();
    void movement();
    void getOffset(Vec2f*) const;
    bool isEnd() const;
    void startCommon(u32);
    void updatePattern();
    void updateInterval();

    CameraShakePattern* mPattern;
    bool mHasEnded;
    bool mIsInfinite;
    u32 mTime;
    u32 mDelay;
    u32 mDelayTimer;
};
