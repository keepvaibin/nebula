#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AirBubbleGenerator : public LiveActor {
public:
    /// @brief Creates a new `AirBubbleGenerator`.
    /// @param pName A pointer to the null-terminated name of the object.
    AirBubbleGenerator(const char* pName);

    /// @brief Destroys the `AirBubbleGenerator`.
    virtual ~AirBubbleGenerator();

    virtual void init(const JMapInfoIter&);
    virtual void control();

    void exeWait();
    void exeGenerate();

    u32 _8C;
    s32 mWaitTime;
    s32 mBubbleLifeTime;
};
