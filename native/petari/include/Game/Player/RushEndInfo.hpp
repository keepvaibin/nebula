#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class LiveActor;

class RushEndInfo {
public:
    RushEndInfo(LiveActor*, u32, const TVec3f&, bool, u32);

    u32 _0;
    u32 _4;
    TVec3f _8;
    bool _14;
    u32 _18;
    LiveActor* _1C;
    union {
        u32 _20;
        struct {
            // _20 is read by shifts: fields from the least significant bit up,.
            u32 _8 : 24;
            u32 mDamageType : 4;
            u32 _0 : 4;
        } mFlags;
    };
};
