#pragma once

#include "Game/MapObj/SpiderThreadPoint.hpp"
#include <nebula_geometry.hpp>

class SpiderThreadPart;
class SpiderThreadRadialLine;
class SpiderThreadMainPoint;

class SpiderThreadPointNearInfo {
public:
    SpiderThreadPointNearInfo();

    void setInfo(f32, SpiderThreadPart*, const Vec3f&);
    void clear();
    const Vec3f* getPos() const;

    SpiderThreadMainPoint* mMainPoint;
    SpiderThreadPoint* mPartPoint;
    f32 mDistToPoint;
    f32 mSpringFactor;
    SpiderThreadPart* mThreadPart;
};

class SpiderThreadMainPoint : public SpiderThreadPoint {
public:
    SpiderThreadMainPoint(const Vec3f&, int);

    void addNearMainPoint(SpiderThreadMainPoint*, SpiderThreadPart*);
    void addNearPartPoint(SpiderThreadPoint*, f32);
    void removeNearPoint(const SpiderThreadPoint*);
    void addNearPointToRadial(SpiderThreadPoint*);
    void removeNearPointFromRadial(const SpiderThreadPoint*);
    void cutNearPoints(s32*, SpiderThreadMainPoint**);
    void updateVelocity();
    void closeToNearPoint(const SpiderThreadPointNearInfo*);

    SpiderThreadPointNearInfo* getNearPointInfo(int idx) const {
        return &mNearInfos[idx];
    }

    inline SpiderThreadPointNearInfo* getLastInfo() const {
        return &mNearInfos[mNumNearInfos - 1];
    }

    SpiderThreadRadialLine* mRadialLine;
    s32 mNumNearInfos;
    SpiderThreadPointNearInfo* mNearInfos;  // array
};
