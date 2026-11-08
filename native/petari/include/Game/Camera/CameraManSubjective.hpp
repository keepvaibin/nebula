#pragma once

#include "Game/Camera/CameraMan.hpp"

class CameraSubjective;

class CameraManSubjective : public CameraMan {
public:
    CameraManSubjective(const char*);

    virtual void calc();
    virtual void notifyActivate();

    CameraSubjective* mCamera;
};
