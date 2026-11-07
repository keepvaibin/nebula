#pragma once

#include "Game/Scene/Scene.hpp"
#include "Game/Util/StringUtil.hpp"

class IntermissionScene : public Scene {
public:
    IntermissionScene();

    virtual void update();
    virtual void draw() const;

    void setCurrentSceneControllerState(const char*, ...) ATTRIBUTE_PRINTF(2, 3);

    MR::FormattedName mState;
    u32 _54;
};
