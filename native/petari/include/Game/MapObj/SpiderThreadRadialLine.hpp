#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class SpiderThreadMainPoint;

class SpiderThreadRadialLine {
public:
    SpiderThreadRadialLine(int);

    void addPoint(SpiderThreadMainPoint*);
    void updateVelocity();
    void updatePos();

    SpiderThreadMainPoint* getPoint(int idx) const {
        return mPoints[idx];
    }

    int mBufferSize;
    s32 mNumPoints;
    SpiderThreadMainPoint** mPoints;
};
