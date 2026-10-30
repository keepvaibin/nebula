#include "Game/System/GameSystemDimmingWatcher.hpp"
#include "Game/Util/TriggerChecker.hpp"
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include "Game/System/NebulaFrameClock.hpp"

GameSystemDimmingWatcher::GameSystemDimmingWatcher() : mChecker(nullptr), _4(true), _5(false) {
    mChecker = new TriggerChecker();
}

void GameSystemDimmingWatcher::update() {
    mChecker->update(_4 || _5);

    if (mChecker->getOnTrigger()) {
        NebulaFrameClock::enableDimming(true);
    } else if (mChecker->getOffTrigger()) {
        NebulaFrameClock::enableDimming(false);
    }
}
