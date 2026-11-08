#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"

class AreaObj;
class LiveActor;
class SleepController;
class SwitchEventListener;

class SleepControllerHolder : public NameObj {
public:
    SleepControllerHolder();

    virtual void movement();

    void add(NameObj*, const TableIter&, SwitchEventListener*);
    void initSync();

private:
    MR::InlineVector< SleepController*, 1024 > mSleepControl;
};

namespace SleepControlFunc {
    void initSyncSleepController();
    void addSleepControl(AreaObj*, const TableIter&);
    void addSleepControl(LiveActor*, const TableIter&);
};  // namespace SleepControlFunc
