#pragma once

#include "Game/NameObj/NameObj.hpp"

class SensorGroup;
class HitSensor;

class SensorHitChecker : public NameObj {
public:
    SensorHitChecker(const char*);

    virtual ~SensorHitChecker();

    virtual void init(const JMapInfoIter&) override;
    virtual void movement() override;

    void initGroup(HitSensor*);
    void doObjColGroup(SensorGroup*, SensorGroup*) const;
    void doObjColInSameGroup(SensorGroup*) const;

    void checkAttack(HitSensor*, HitSensor*) const;

    SensorGroup* mPlayerGroup;
    SensorGroup* mRideGroup;
    SensorGroup* mEyeGroup;
    SensorGroup* mSimpleGroup;
    SensorGroup* mMapObjGroup;
    SensorGroup* mCharacterGroup;
};

class SensorGroup {
public:
    SensorGroup(int, const char*);

    void add(HitSensor*);
    void remove(HitSensor*);
    void clear() const;

    s32 mMaxSensors;
    s32 mSensorCount;
    HitSensor** mSensors;
};

namespace MR {
    void initHitSensorGroup(HitSensor*);
};  // namespace MR
