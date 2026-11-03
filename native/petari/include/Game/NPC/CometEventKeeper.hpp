#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class CometEventExecutorTimeLimit;
class GalaxyCometScreenFilter;
struct GalaxyTimeLimitInfo;

class CometEventKeeper {
public:
    /// @brief Creates a new `CometEventKeeper`.
    CometEventKeeper();

    void init();
    bool isStartEvent(const char*) const;
    bool isStartTimeLimitEvent() const;
    void startCometEventIfExist();
    void endCometEvent();
    static u32 getTimeLimitFromTable(const GalaxyTimeLimitInfo*, int);
    void initCometStatus();

private:
    CometEventExecutorTimeLimit* mExecutorTimeLimit;
    GalaxyCometScreenFilter* mScreenFilter;
    const char* mCometName;
    s32 mCometStateIndex;
};
