#pragma once

#include "Game/Camera/CameraMan.hpp"

class CameraSubjective;

class CameraManSubjective : public CameraMan {
public:
    CameraManSubjective(const char*);

    virtual void init(const TableIter&);

    virtual void calc();
    virtual void notifyActivate();
    virtual void notifyDeactivate();

    CameraSubjective* mCamera;
};
