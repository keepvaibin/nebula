#pragma once


class TripodBossStepPoint;

class TripodBossStepSequence {
public:
    /// @brief Creates a new `TripodBossStepSequence`.
    TripodBossStepSequence();

    void addStepPoint(TripodBossStepPoint*);
    s32 getCurrentLeg() const;
    s32 getCurrentWaitTime() const;
    TripodBossStepPoint* getCurrentStepPoint();
    void nextStep();
    bool isEmpty() const;
    void reset();

    TripodBossStepPoint* mStepPoints[32];
    s32 mCurrentPoint;
    s32 mPointNum;
    bool _88;
};
