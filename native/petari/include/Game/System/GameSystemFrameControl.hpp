#pragma once

class GameSystemFrameControl {
public:
    /// @brief Creates a new `GameSystemFrameControl`.
    GameSystemFrameControl();

    void setMovement60fps();

private:
    bool mIs60fps;
};
