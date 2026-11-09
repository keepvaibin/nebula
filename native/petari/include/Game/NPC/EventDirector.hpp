#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class CometEventKeeper;
class PowerStarEventKeeper;
class PowerStarHolder;
class StageStateKeeper;

class EventDirector : public NameObj {
public:
    EventDirector();

    virtual void init(const TableIter& rIter) override;

    PowerStarEventKeeper* mPowerStarEventKeeper;
    StageStateKeeper* mStageStateKeeper;
    PowerStarHolder* mPowerStarHolder;
    CometEventKeeper* mCometEventKeeper;
};

namespace EventFunction {
    PowerStarEventKeeper* getPowerStarEventKeeper();
    StageStateKeeper* getStageStateKeeper();
    PowerStarHolder* getPowerStarHolder();
    CometEventKeeper* getCometEventKeeper();
    bool isStartCometEvent(const char*);
    void startCometEvent();
    void endCometEvent();
};  // namespace EventFunction

namespace MR {
    void declareEventPowerStar(const char*, s32, bool);
    void initEventSystemAfterPlacement();
    void declareEventPowerStar(const NameObj*, s32, bool);
    bool isSuccessEventPowerStar(const char*, s32);
    bool isGreenEventPowerStar(const char*, s32);
    bool isRedEventPowerStar(const char*, s32);
    bool isGrandEventPowerStar(const char*, s32);
    void appearEventPowerStar(const char*, s32, const Vec3f*, bool, bool);
    bool isEndEventPowerStarAppearDemo(const char*);
};  // namespace MR
