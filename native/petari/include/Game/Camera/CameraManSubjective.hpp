#pragma once

#include "Game/Camera/CameraMan.hpp"

class CameraSubjective;

class CameraManSubjective : public CameraMan {
public:
    CameraManSubjective(const char*);

    virtual void calc() override;
    virtual void notifyActivate() override;

    CameraSubjective* mCamera;
};
