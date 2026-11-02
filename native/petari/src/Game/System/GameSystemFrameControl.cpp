#include "Game/System/GameSystemFrameControl.hpp"

GameSystemFrameControl::GameSystemFrameControl() : mIs60fps(true) {
    setMovement60fps();
}

void GameSystemFrameControl::setMovement60fps() {
    mIs60fps = true;
}
