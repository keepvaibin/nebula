#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class Polta;
class HitSensor;

class PoltaSensorCtrl {
public:
    PoltaSensorCtrl(Polta*);

    s32 getSensorCount() const;
    inline s32 getCoreSensorCount() const {
        return 1;
    };
    inline s32 getBodySensorCount() const {
        return 3;
    };
    inline s32 getArmSensorCount() const {
        return 11;
    };
    void setScreamSensorSize(f32);
    void initSensor();
    bool isCoreSensor(const HitSensor*) const;
    bool isBodySensor(const HitSensor*) const;
    bool isLeftArmSensor(const HitSensor*) const;
    bool isRightArmSensor(const HitSensor*) const;
    bool isHeadSensor(const HitSensor*) const;
    bool isScreamSensor(const HitSensor*) const;

    Polta* mPoltaPtr;
    HitSensor* mScreamSensor;
    HitSensor* mHeadSensor;
    HitSensor** mCoreSensors;      // (1 element)
    HitSensor** mBodySensors;      // (3 elements)
    HitSensor** mLeftArmSensors;   // (11 elements)
    HitSensor** mRightArmSensors;  // (11 elements)
};

struct PoltaSensorListEntry {
    const char* mName;
    const char* mJointName;
    f32 mRadius;
    f32 mOffsetX;
    f32 mOffsetY;
    f32 mOffsetZ;
};
