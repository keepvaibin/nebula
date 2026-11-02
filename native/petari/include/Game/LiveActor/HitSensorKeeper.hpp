#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitSensor;
class HitSensorInfo;
class LiveActor;

class HitSensorKeeper {
public:
    HitSensorKeeper(int);

    HitSensor* add(const char*, u32, u16, f32, LiveActor*, const TVec3f&);
    HitSensor* addPos(const char*, u32, u16, f32, LiveActor*, const TVec3f*, const TVec3f&);
    HitSensor* addMtx(const char*, u32, u16, f32, LiveActor*, Mtx, const TVec3f&);
    HitSensor* addCallback(const char*, u32, u16, f32, LiveActor*);

    HitSensor* getSensor(const char*) const;
    void update();
    void doObjCol();
    void clear();
    void validate();
    void invalidate();
    void validateBySystem();
    void invalidateBySystem();
    HitSensorInfo* getNthSensorInfo(int) const;
    HitSensorInfo* getSensorInfo(const char*) const;

    void registHitSensorInfo(HitSensorInfo*);

    s32 mSensorInfosSize;
    s32 mSensorCount;
    HitSensorInfo** mSensorInfos;
    HitSensor* mTaking;
    HitSensor* mTaken;
};
