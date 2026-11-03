#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

class CameraRotChecker {
public:
    CameraRotChecker();

    void update();

    TPos3f mMtx;
    bool mIsRotatingHard;
};
