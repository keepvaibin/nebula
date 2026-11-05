#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitSensor;

class HitSensorInfo {
public:
    HitSensorInfo(const char*, HitSensor*, const Vec3f*, MtxPtr, const Vec3f&, bool);

    void update();
    void doObjCol();

    void setOffset(const Vec3f& rOffset) {
        _C = rOffset;
    }

    const char* mName;
    s32 mHashCode;
    HitSensor* mSensor;
    Vec3f _C;
    const Vec3f* _18;
    MtxPtr _1C;
    bool _20;
    u8 _21;
    u8 _22;
    u8 _23;
};
