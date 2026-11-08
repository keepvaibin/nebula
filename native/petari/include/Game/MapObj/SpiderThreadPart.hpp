#pragma once

#include "Game/Render/TypedDraw.hpp"
#include <nebula_geometry.hpp>


class SpiderThreadMainPoint;
class SpiderThreadPoint;

class SpiderThreadPart {
public:
    SpiderThreadPart(SpiderThreadMainPoint*, SpiderThreadMainPoint*, f32);

    void cut(SpiderThreadMainPoint*, SpiderThreadMainPoint*);
    void updatePointerPos();
    void update();
    void draw(TypedDraw& rDraw) const;
    void startHang(s32);
    void updateHang(const Vec3f&, s32);
    void endHang(s32);
    void stopAllPoints();
    f32 calcNearestPointInfo(const Vec3f**, const Vec3f**, s32*, const Vec3f&);
    void updatePointPosToLine();
    void updatePointPosAndBasePosToLine();
    bool touchActor(s32, const Vec3f&);
    void tryTouch(f32, const Vec3f&, s32);
    s32 findTouchPoint(f32, const Vec3f&, s32);
    f32 findNearestPointNo(s32*, const Vec3f&) const;
    void updatePointPosHanging(s32);
    void updatePointVelocityTouch(s32, const Vec3f&);
    void updateBasePos();
    void updateForceAttach();
    void updateCutPoints();
    void updateDrawVec();

    void drawLine(TypedDraw& rDraw) const;
    void drawPoints(TypedDraw& rDraw) const;

    inline void padBox(f32 padAmount) {
        Vec3f padding(padAmount);
        mBoundBox.i.sub(padding);
        mBoundBox.f.add(padding);
    }

    SpiderThreadPoint* getPoint(int idx) const {
        return mPoints[idx];
    }

    bool mIsStill;
    bool mIsCut;
    bool mIsFree;
    s32 mNumHangingPoints;
    s32 mStopTime;
    s32 mTouchTimeout;
    Vec3f mPointerPosPad0;
    Vec3f mPointerPosPad1;
    SpiderThreadMainPoint* mPointA;
    SpiderThreadMainPoint* mPointB;
    s32 mNumPoints;
    SpiderThreadPoint** mPoints;
    Box3f mBoundBox;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    f32 mDistBetweenMainPoints;
    f32 mDistBetweenPoints;
    f32 mTextureOffset;
};
