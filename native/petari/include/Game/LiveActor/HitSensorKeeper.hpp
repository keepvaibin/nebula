#pragma once

#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitSensor;
class HitSensorInfo;
class LiveActor;

class HitSensorKeeper {
public:
    // capacity: the sensors the actor declares; more grow the list.
    HitSensorKeeper(s32 capacity);

    HitSensor* add(const char*, u32, u32, f32, LiveActor*, const Vec3f& rOffset);
    HitSensor* addPos(const char*, u32, u32, f32, LiveActor*, const Vec3f*, const Vec3f& rOffset);
    HitSensor* addMtx(const char*, u32, u32, f32, LiveActor*, Mtx, const Vec3f& rOffset);
    HitSensor* addCallback(const char*, u32, u32, f32, LiveActor*);

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
    MR::GrowBuffer< HitSensorInfo* > mSensorInfos;
    HitSensor* mTaking;
    HitSensor* mTaken;
};
