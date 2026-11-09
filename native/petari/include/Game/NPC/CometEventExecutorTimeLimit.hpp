#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TimeLimitLayout;

class CometEventExecutorTimeLimit : public LiveActor {
public:
    /// @param timeLimit The number of seconds to initialize the timer with.
    CometEventExecutorTimeLimit(u32 timeLimit);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;

    void exeStartEvent();
    u32 getTimeLimit() const;

private:
    /// @brief The pointer to the actor instance for the timer.
    TimeLimitLayout* mTimeLimitLayout;

    /// @brief The number of seconds to initialize the timer with.
    u32 mTimeLimit;
};
