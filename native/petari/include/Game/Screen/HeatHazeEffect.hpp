#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class HeatHazeEffect : public LiveActor {
    friend class HeatHazeDirector;

public:
    /// @brief Creates a new `HeatHazeEffect`.
    /// @param pName A pointer to the null-terminated name of the object.
    HeatHazeEffect(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

private:
    f32 mHazeDistance;  // distance in front of the camera
};

class HeatHazeDirector : public NameObj {
public:
    /// @brief Creates a new `HeatHazeDirector`.
    /// @param pName A pointer to the null-terminated name of the object.
    HeatHazeDirector(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;

private:
    HeatHazeEffect* mEffect;
};
