#pragma once

#include <nebula_geometry.hpp>

class LiveActor;
class SpiderThreadPart;

class SpiderThreadHangInfo {
public:
    SpiderThreadHangInfo();

    void startBind(LiveActor*, const Vec3f*, SpiderThreadPart*, s32, const Vec3f&, s32);
    void endBind();
    void startHang();
    void endHang(bool isCancel);
    void updateHang();
    void copyInfo(const SpiderThreadHangInfo*);

    LiveActor* mBindedActor;
    const Vec3f* mPosition;
    SpiderThreadPart* mThreadPart;
    s32 mHangIndex;
    bool mIsHanging;
    s32 mPriority;
};
