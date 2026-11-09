#pragma once

#include <nebula_geometry.hpp>

class SpiderThreadMainPoint;

class SpiderThreadRadialLine {
public:
    SpiderThreadRadialLine(s32 bufferSize);

    void addPoint(SpiderThreadMainPoint* pPoint);
    void updateVelocity();
    void updatePos();

    SpiderThreadMainPoint* getPoint(s32 idx) const {
        return mPoints[idx];
    }

    s32 mNumPoints;
    SpiderThreadMainPoint** mPoints;
    s32 mBufferSize;
};
