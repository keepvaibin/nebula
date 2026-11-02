#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class LiveActor;
class SpiderThreadPart;

class SpiderThreadHangInfo {
public:
    SpiderThreadHangInfo();

    void startBind(LiveActor*, const TVec3f*, SpiderThreadPart*, s32, const TVec3f&, s32);
    void endBind();
    void startHang();
    void endHang(bool);
    void updateHang();
    void copyInfo(const SpiderThreadHangInfo*);

    LiveActor* mBindedActor;
    const TVec3f* mPosition;
    SpiderThreadPart* mThreadPart;
    s32 mHangIndex;
    bool mIsHanging;
    s32 _14;
};
