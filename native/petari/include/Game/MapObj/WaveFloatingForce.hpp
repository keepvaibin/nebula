#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class WaveFloatingForce {
public:
    WaveFloatingForce(const LiveActor*, s32, f32, bool);

    void update();
    f32 getCurrentValue() const;
    void updateNormal();
    void updateOnPlayer();

    const LiveActor* mActor;
    s32 mStepCount;
    f32 mAmplitude;
    f32 mStep;
    bool _10;
};
