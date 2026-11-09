#include "Game/MapObj/SpiderThreadRadialLine.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpiderThreadMainPoint.hpp"
#include "Game/Util.hpp"

SpiderThreadRadialLine::SpiderThreadRadialLine(s32 bufferSize)
    : mNumPoints(), mPoints(new SpiderThreadMainPoint*[bufferSize > 0 ? bufferSize : 0]()), mBufferSize(bufferSize > 0 ? bufferSize : 0) {
}

void SpiderThreadRadialLine::addPoint(SpiderThreadMainPoint* pPoint) {
    if (mNumPoints >= mBufferSize) {
        return;
    }

    mPoints[mNumPoints++] = pPoint;
}

void SpiderThreadRadialLine::updateVelocity() {
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        mPoints[idx]->updateVelocity();
    }
}

void SpiderThreadRadialLine::updatePos() {
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        mPoints[idx]->updatePos();
    }
}
