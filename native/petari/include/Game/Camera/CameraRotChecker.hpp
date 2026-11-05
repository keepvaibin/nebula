#pragma once

#include <nebula_geometry.hpp>

class CameraRotChecker {
public:
    CameraRotChecker();

    void update();

    Pos3f mMtx;
    bool mIsRotatingHard;
};
