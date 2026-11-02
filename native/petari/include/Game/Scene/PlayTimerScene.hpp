#pragma once

#include "Game/Scene/Scene.hpp"

class TimeLimitLayout;
class TimeUpLayout;
class ValueControl;

class PlayTimerScene : public Scene {
public:
    /// @brief Creates a new `PlayTimerScene`.
    PlayTimerScene();

    virtual void init();
    virtual void start();
    virtual void update();
    virtual void draw() const;

    bool isActive() const;
    bool isEndGlobalTimer() const;
    void stop();
    void startTimeUp();
    void exeNormal();
    void exeTimeUp();
    void exeFadeoutAfterTimeUp();

private:
    TimeLimitLayout* mTimeLimitLayout;
    TimeUpLayout* mTimeUpLayout;
    u32 mTimeUpWaitFrame;
    ValueControl* _20;
};
