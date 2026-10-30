#include "Game/Util/SchedulerUtil.hpp"
#include "Game/System/NebulaKernel.hpp"
#include <revolution/os.h>

MR::ProhibitSchedulerAndInterrupts::ProhibitSchedulerAndInterrupts(bool unused) {
    mInterruptsStatus = NebulaKernel::disableInterrupts();
    NebulaKernel::disableScheduler();
}

MR::ProhibitSchedulerAndInterrupts::~ProhibitSchedulerAndInterrupts() {
    NebulaKernel::enableScheduler();
    NebulaKernel::yield();
    NebulaKernel::restoreInterrupts(mInterruptsStatus);
}
