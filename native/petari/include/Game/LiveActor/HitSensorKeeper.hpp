#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitSensor;
class HitSensorInfo;
class LiveActor;

class HitSensorKeeper {
public:
    HitSensorKeeper(int);

    HitSensor* add(const char*, u32, u16, f32, LiveActor*, const Vec3f&);
    HitSensor* addPos(const char*, u32, u16, f32, LiveActor*, const Vec3f*, const Vec3f&);
    HitSensor* addMtx(const char*, u32, u16, f32, LiveActor*, Mtx, const Vec3f&);
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
