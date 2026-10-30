#include "Game/System/GameSystemFrameControl.hpp"
#include "Game/System/MainLoopFramework.hpp"

GameSystemFrameControl::GameSystemFrameControl() : mIs60fps(true) {
    setMovement60fps();
}

void GameSystemFrameControl::setMovement60fps() {
    mIs60fps = true;

    // The output runs at 60 Hz (never a 50 Hz PAL television).
    MainLoopFramework* pFramework = MainLoopFramework::sManager;
    pFramework->mRetraceCount = 1;
}
