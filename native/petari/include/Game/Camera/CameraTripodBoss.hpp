#pragma once

#include "Game/Camera/CameraTower.hpp"

class CameraTripodBoss : public CameraTower {
public:
    CameraTripodBoss(const char* pName = "三脚ボスカメラ");

    virtual void setParamFromChunk(const CameraParamChunk*);
    virtual void arrangeRound();

    f32 mAngleY;
};
