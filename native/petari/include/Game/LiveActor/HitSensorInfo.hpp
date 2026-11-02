#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitSensor;

class HitSensorInfo {
public:
    HitSensorInfo(const char*, HitSensor*, const TVec3f*, MtxPtr, const TVec3f&, bool);

    void update();
    void doObjCol();

    void setOffset(const TVec3f& rOffset) {
        _C = rOffset;
    }

    const char* mName;
    s32 mHashCode;
    HitSensor* mSensor;
    TVec3f _C;
    const TVec3f* _18;
    MtxPtr _1C;
    bool _20;
    u8 _21;
    u8 _22;
    u8 _23;
};
