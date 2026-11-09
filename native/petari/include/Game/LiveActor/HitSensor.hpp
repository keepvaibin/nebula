#pragma once

#include <nebula_geometry.hpp>

class LiveActor;
class SensorGroup;

class HitSensor {
public:
    HitSensor(u32 type, u32 groupSize, f32 radius, LiveActor* pHost);

    bool receiveMessage(u32 msg, HitSensor* pSender);
    void setType(u32);
    bool isType(u32) const;
    void validate();
    void invalidate();
    void validateBySystem();
    void invalidateBySystem();
    void addHitSensor(HitSensor* pSensor);

    const Vec3f& getPosition() const {
        return mPosition;
    }

    f32 getRadius() const {
        return mRadius;
    }

    inline bool isValid() const {
        return mValidByHost && mValidBySystem;
    }

    u32 mType;
    Vec3f mPosition;
    f32 mRadius;
    u32 mSensorCount;
    u32 mGroupSize;
    HitSensor** mSensors;
    SensorGroup* mSensorGroup;
    bool mValidBySystem;
    bool mValidByHost;
    LiveActor* mHost;
};
