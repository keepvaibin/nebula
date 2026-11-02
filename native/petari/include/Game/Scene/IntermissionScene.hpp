#pragma once

#include "Game/Scene/Scene.hpp"

class IntermissionScene : public Scene {
public:
    IntermissionScene();

    virtual void update();
    virtual void draw() const;

    void setCurrentSceneControllerState(const char*, ...) ATTRIBUTE_PRINTF(2, 3);

    char mState[0x40];
    u32 _54;
};
