#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ValueControl;

struct Timing {
    u32 mScaleStartFrame;
    u32 mScaleKeepFrame;
    bool mIsScaleUp;
    bool mIsScaleDown;
    bool mIsFadeOut;  // after the scale-down the counter fades out instead of staying
};

class TimeUpLayout : public LayoutActor {
public:
    /// @brief Creates a new `TimeUpLayout`.
    TimeUpLayout() : LayoutActor("タイムアップ画面", true) {
    }

    virtual void init(const TableIter& rIter) override;
};

class TimeLimitLayout : public LayoutActor {
public:
    TimeLimitLayout(u32 timeLimit);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void setTimeLimit(u32);
    void setDisplayModeOnNormal(bool);
    bool isReadyToTimeUp() const;
    void resetFrame();
    void addFrame();
    void updateTextBox();
    void exeAppear();
    void exeCountDown();
    void exeScaleUp();
    void exeScaleKeep();
    void exeScaleDown();
    void exeFadeout();
    void exeTimeUpReady();
    const Timing* getCurrentTiming() const;
    bool updateNormal();
    // Steps counted and the whole length (plugins: NebulaMods).
    u32 getTime() const {
        return mTime;
    }
    u32 getTimeLimit() const {
        return mTimeLimit;
    }
    void setTime(u32 time) {
        mTime = time < mTimeLimit ? time : mTimeLimit;
        updateTextBox();
    }

private:
    u32 mTime;
    u32 mTimeLimit;
    ValueControl* mScaleControl;
    ValueControl* mFadeControl;
    const Timing* mCurrentTiming;
    bool mIsSuspend;
    bool mIsNormalDisplay;  // stays up instead of fading out after the scale down
};
