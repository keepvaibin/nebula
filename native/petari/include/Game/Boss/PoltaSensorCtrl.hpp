#pragma once

#include <nebula_geometry.hpp>

class Polta;
class HitSensor;

class PoltaSensorCtrl {
public:
    // The sizes of the sensor tables in PoltaSensorCtrl.cpp.
    static const s32 cCoreSensorNum = 1;
    static const s32 cBodySensorNum = 3;
    static const s32 cArmSensorNum = 11;

    PoltaSensorCtrl(Polta* pPolta);

    // Every sensor initSensor adds: the tables plus scream and head.
    s32 getSensorCount() const {
        return cCoreSensorNum + cBodySensorNum + 2 * cArmSensorNum + 2;
    }

    void setScreamSensorSize(f32 screamSensorSize);
    void initSensor();
    bool isCoreSensor(const HitSensor* pSensor) const;
    bool isBodySensor(const HitSensor* pSensor) const;
    bool isLeftArmSensor(const HitSensor* pSensor) const;
    bool isRightArmSensor(const HitSensor* pSensor) const;
    bool isHeadSensor(const HitSensor* pSensor) const;
    bool isScreamSensor(const HitSensor* pSensor) const;

private:
    Polta* mPoltaPtr;
    HitSensor* mScreamSensor;
    HitSensor* mHeadSensor;
    HitSensor* mCoreSensors[cCoreSensorNum];
    HitSensor* mBodySensors[cBodySensorNum];
    HitSensor* mLeftArmSensors[cArmSensorNum];
    HitSensor* mRightArmSensors[cArmSensorNum];
};

struct PoltaSensorListEntry {
    const char* mName;
    const char* mJointName;
    f32 mRadius;
    f32 mOffsetX;
    f32 mOffsetY;
    f32 mOffsetZ;
};
