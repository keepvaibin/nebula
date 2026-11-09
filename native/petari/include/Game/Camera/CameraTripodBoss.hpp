#pragma once

#include "Game/Camera/CameraTower.hpp"

class CameraTripodBoss : public CameraTower {
public:
    CameraTripodBoss(const char* pName = "三脚ボスカメラ");

    virtual void setParamFromChunk(const CameraParamChunk*) override;
    virtual void arrangeRound() override;

    f32 mAngleY;
};
