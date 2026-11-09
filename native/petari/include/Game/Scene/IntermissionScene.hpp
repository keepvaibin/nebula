#pragma once

#include "Game/Scene/Scene.hpp"
#include "Game/Util/StringUtil.hpp"

class IntermissionScene : public Scene {
public:
    IntermissionScene();

    virtual void draw() const override;

    void setCurrentSceneControllerState(const char*, ...) ATTRIBUTE_PRINTF(2, 3);

    MR::FormattedName mState;
};
